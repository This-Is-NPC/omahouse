#!/usr/bin/env python3
"""The CLI contract, end to end, as an ordinary user with nothing installed.

Every root the binary reads moves by variable -- the cgroup tree, /etc/omahouse
and /var/lib/omahouse -- so the whole of stage 4 can be driven without root, and
without a graphical session with a Chromium open in it. The fake cgroup tree is
built out of unit names that were measured: poc/findings.md for the flatpak scope
and the escaped one, the development machine for the rest.

A verb that parses and does nothing is indistinguishable from one that works
until somebody depends on it."""

import json
import os
import pwd
import re
import subprocess
import tempfile
from datetime import date, timedelta
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("OMAHOUSE_CLI", ROOT / "build/bin/omahouse"))

# A real account, because `status` resolves a name to a uid through NSS and an
# invented name is exit 2 by design. Whoever runs the suite is the one account
# every machine that runs it is guaranteed to have.
USER = pwd.getpwuid(os.getuid()).pw_name
UID = os.getuid()
TODAY = date.today()

# What the development machine had open while stage 4 was written, plus the two
# shapes only the PoC saw. The counts are what makes the table assertions mean
# something.
SCOPES = {
    "app-graphical.slice/app-Hyprland-chromium-031bdc27.scope": 19,
    "app-code-3579042.scope": 12,
    "app-org.chromium.Chromium-3735302.scope": 4,
    "app-graphical.slice/app-Hyprland-xdg\\x2dterminal\\x2dexec-151e8e07.scope": 4,
    "app-graphical.slice/app-flatpak-org.freedesktop.Platform-2351381583.scope": 4,
    # A scope on its way out: the unit is there, nothing is in it.
    "app-graphical.slice/app-Hyprland-sleep-7865852f.scope": 0,
    # Under app.slice and not an app scope name. This machine has thirty.
    "app-graphical.slice/tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope": 18,
    # Plumbing on the app side of the tree. Never an app.
    "dconf.service": 1,
}

SESSION = {
    "wayland-wm@hyprland.desktop.service": 8,
    "pipewire.service": 1,
    "wireplumber.service": 1,
    "dbus-broker.service": 2,
    "xdg-desktop-portal.service": 1,
}


def write_cgroup(path, pids):
    path.mkdir(parents=True, exist_ok=True)
    (path / "cgroup.procs").write_text("".join(f"{4000 + i}\n" for i in range(pids)))


def build_cgroup_tree(root):
    """A user@<uid>.service the way systemd lays one out."""
    manager = root / "user.slice" / f"user-{UID}.slice" / f"user@{UID}.service"
    for relative, pids in SCOPES.items():
        write_cgroup(manager / "app.slice" / relative, pids)
    for unit, pids in SESSION.items():
        write_cgroup(manager / "session.slice" / unit, pids)
    return root


def profile_document():
    return {
        "schemaVersion": 1,
        "profiles": [
            {
                "user": USER,
                "displayName": "Júlia",
                "enabled": True,
                "enforce": True,
                "default": "deny",
                "warnAt": [10, 5, 1],
                "grace": 20,
                "rules": [
                    {"match": "chromium", "verdict": "allow"},
                    {"match": "code", "verdict": "allow"},
                ],
                "budgets": [
                    {"id": "session", "match": "*", "dailyMinutes": 120,
                     "onExhausted": "logout"},
                    {"id": "chromium", "match": "chromium", "dailyMinutes": 45,
                     "onExhausted": "close"},
                    {"id": "code", "match": "code"},
                ],
            }
        ],
    }


def ledger_document(day, seconds=4210):
    stamp = f"{day.isoformat()}T19:12:04-03:00"
    return {
        "schemaVersion": 1,
        "user": USER,
        "date": day.isoformat(),
        "budgets": {"session": seconds, "chromium": 2700},
        "grants": [{"at": stamp, "by": "howl", "budget": "session", "minutes": 10}],
        "events": [
            {"at": stamp, "kind": "warn", "budget": "chromium", "minutes": 5},
            {"at": stamp, "kind": "exhausted", "budget": "chromium"},
            {"at": stamp, "kind": "denied",
             "scope": "app-Hyprland-steam-9f2c11ab.scope"},
        ],
    }


class Box:
    """A machine in a directory: a cgroup tree, an /etc and a /var."""

    def __init__(self, directory):
        self.root = Path(directory)
        self.cgroup = build_cgroup_tree(self.root / "cgroup")
        self.config = self.root / "etc"
        self.state = self.root / "var"
        self.config.mkdir()
        self.state.mkdir()

    def write_profiles(self, document=None):
        (self.config / "profiles.json").write_text(
            json.dumps(document if document is not None else profile_document()))

    def write_ledger(self, day, seconds=4210):
        directory = self.state / USER
        directory.mkdir(parents=True, exist_ok=True)
        (directory / f"{day.isoformat()}.json").write_text(
            json.dumps(ledger_document(day, seconds)))

    def run(self, *args, extra_env=None):
        env = {
            **os.environ,
            "OMAHOUSE_CGROUP_ROOT": str(self.cgroup),
            "OMAHOUSE_CONFIG_DIR": str(self.config),
            "OMAHOUSE_STATE_DIR": str(self.state),
        }
        env.pop("OMAHOUSE_JSON", None)
        if extra_env:
            env.update(extra_env)
        return subprocess.run(
            [str(CLI), *map(str, args)],
            cwd=str(ROOT), env=env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            # Never the terminal the suite was started from.
            stdin=subprocess.DEVNULL,
        )


def columns(line):
    return re.split(r" {2,}", line.strip())


def row_for(stdout, first_cell, after=None):
    """The row of a column table whose first cell is `first_cell`.

    `after` names the header the table starts with, because one screen holds
    several tables and `chromium` is a row of two of them: the apps that are
    running, and the budget that is about them.
    """
    looking = after is None
    for line in stdout.splitlines():
        cells = columns(line)
        if not cells:
            continue
        if not looking:
            looking = cells[0] == after
            continue
        if cells[0] == first_cell:
            return cells
    return None


# -- the screen ---------------------------------------------------------------

def check_version_is_said_once(box):
    """Every place that says a version says the same one.

    The declaration carries a copy of the number because `usage` has nowhere to
    read qmake/version.pri from, and a second copy is a second thing to forget.
    """
    spoken = box.run("--version")
    assert spoken.returncode == 0, spoken.stderr
    version = spoken.stdout.split()[-1]
    assert re.fullmatch(r"\d+\.\d+\.\d+", version), spoken.stdout

    declared = re.search(r'^version "([^"]+)"',
                         (ROOT / "omahouse.usage.kdl").read_text(), re.M)
    assert declared, "omahouse.usage.kdl declares no version"
    assert declared.group(1) == version, (declared.group(1), version)

    # And the help agrees, which is the copy a reader sees.
    assert box.run("--help").stdout.splitlines()[0].split()[1] == version


def check_help_and_refusals(box):
    helped = box.run("--help")
    assert helped.returncode == 0, helped.stderr
    for verb in ("status", "report", "profile list", "profile show"):
        assert verb.split()[0] in helped.stdout

    # No verb at all is a usage error even though the screen is the same: a
    # script that got here by accident has to be able to tell it from --help.
    bare = box.run()
    assert bare.returncode == 1
    assert bare.stdout.startswith("omahouse ")

    unknown = box.run("frobnicate")
    assert unknown.returncode == 1
    assert "unknown command" in unknown.stderr
    assert unknown.stdout == ""

    # The verbs of spec.md §7 that later stages build are named rather than
    # called typos.
    for verb, stage in (("watch", "stage 6"), ("allow", "stage 5"), ("grant", "stage 5")):
        later = box.run(verb, USER)
        assert later.returncode == 1, verb
        assert stage in later.stderr, later.stderr


# -- status -------------------------------------------------------------------

def check_status_scans_without_a_profile(box):
    """The mode that validates stages 2 and 3 against a machine as it is today.

    No profiles.json at all: it says so, in the words of somebody who has not
    installed anything yet, and goes on to list what is running.
    """
    scanned = box.run("status", USER)
    assert scanned.returncode == 0, scanned.stderr
    assert "Scanning only" in scanned.stdout
    assert str(box.config / "profiles.json") in scanned.stdout

    # The identity of an app is the scope, and the parser of the core is what
    # resolves it -- including the escaped one, which is the case that only turns
    # up by measuring.
    assert row_for(scanned.stdout, "chromium")[:2] == ["chromium", "19"]
    assert row_for(scanned.stdout, "code")[:2] == ["code", "12"]
    assert row_for(scanned.stdout, "xdg-terminal-exec")[:2] == ["xdg-terminal-exec", "4"]
    assert row_for(scanned.stdout, "org.freedesktop.Platform")[:2] == \
        ["org.freedesktop.Platform", "4"]
    assert row_for(scanned.stdout, "org.chromium.Chromium")[:2] == \
        ["org.chromium.Chromium", "4"]

    # A scope with nothing in it is not somebody's app running, and a .service
    # under app.slice is plumbing that happens to sit next to the apps.
    assert "sleep" not in scanned.stdout
    assert "dconf" not in scanned.stdout

    # No balance without a profile: there is no budget to have one.
    assert "BUDGET" not in scanned.stdout
    # And no verdict either: nothing has said what is allowed here.
    assert "VERDICT" not in scanned.stdout


def check_status_reports_what_it_cannot_see(box):
    """spec.md §5, both halves of it.

    A `tmux-spawn-<uuid>.scope` is seen and not named: omahouse knows it is there
    and could close it, but has no id to match a rule against. The compositor's
    processes are the real blind spot: an app started by a raw `exec` is in there
    and cannot be told from Hyprland, so it is neither counted nor closable.
    """
    seen = box.run("status", USER)
    assert "Out of reach" in seen.stdout
    assert "1 scope under app.slice omahouse could not name" in seen.stdout
    assert "tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope" in seen.stdout
    assert "8 processes in session.slice" in seen.stdout
    assert "wayland-wm@hyprland.desktop.service" in seen.stdout
    # Named, and not counted as an app: the tmux scope is not in the table.
    assert row_for(seen.stdout, "tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope") \
        is None
    # Session plumbing is a unit somebody's package declared, and is not in the
    # count.
    assert "pipewire" not in seen.stdout
    assert "dbus-broker" not in seen.stdout


def check_status_with_a_profile(box):
    box.write_profiles()
    box.write_ledger(TODAY)

    shown = box.run("status", USER)
    assert shown.returncode == 0, shown.stderr
    assert "Júlia" in shown.stdout
    assert "enforcing, default deny" in shown.stdout

    # The verdict each app would get: two rules, and everything else falling to
    # the default, which here is deny.
    assert row_for(shown.stdout, "chromium", after="APP") == \
        ["chromium", "19", "allow", "app-Hyprland-chromium-031bdc27.scope"]
    assert row_for(shown.stdout, "code", after="APP")[:3] == ["code", "12", "allow"]
    assert row_for(shown.stdout, "org.freedesktop.Platform", after="APP")[2] == "deny"

    # The balance, with the ten minutes an operator granted inside the limit:
    # 120m + 10m against 4210s spent leaves 3590s.
    assert row_for(shown.stdout, "session", after="BUDGET") == \
        ["session", "2h10m", "1h10m", "59m", "logs out"]
    # 45m of 45m: out, and what happens when it is.
    assert row_for(shown.stdout, "chromium", after="BUDGET") == \
        ["chromium", "45m", "45m", "0m", "closes"]
    # A budget with no dailyMinutes counts and never runs out.
    assert row_for(shown.stdout, "code", after="BUDGET") == \
        ["code", "—", "0m", "—", "never runs out"]


def check_status_json(box):
    box.write_profiles()
    box.write_ledger(TODAY)
    spoken = box.run("--json", "status", USER)
    assert spoken.returncode == 0, spoken.stderr
    document = json.loads(spoken.stdout)

    assert document["user"] == USER
    assert document["uid"] == UID
    assert document["date"] == TODAY.isoformat()
    assert document["session"] is True
    assert document["profile"]["displayName"] == "Júlia"

    ids = {scope["id"]: scope for scope in document["scopes"]}
    assert ids["chromium"]["processes"] == 19
    assert ids["chromium"]["verdict"] == "allow"
    assert ids["org.freedesktop.Platform"]["verdict"] == "deny"
    assert ids["chromium"]["unit"] == "app-Hyprland-chromium-031bdc27.scope"
    assert ids["chromium"]["cgroup"].startswith(str(box.cgroup))

    assert [u["unit"] for u in document["unnamed"]] == \
        ["tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope"]
    assert document["outOfReach"]["processes"] == 8

    budgets = {budget["id"]: budget for budget in document["budgets"]}
    assert budgets["session"]["limitSeconds"] == 120 * 60 + 600
    assert budgets["session"]["usedSeconds"] == 4210
    assert budgets["session"]["leftSeconds"] == 3590
    assert budgets["session"]["exhausted"] is False
    assert budgets["chromium"]["exhausted"] is True
    # Null and not zero: zero left is a budget that has run out, and a budget
    # with no limit has not.
    assert budgets["code"]["limitSeconds"] is None
    assert budgets["code"]["leftSeconds"] is None
    assert budgets["code"]["exhausted"] is False

    # The flag also arrives by environment, which is how a studio or a script
    # sets it once.
    from_env = box.run("status", USER, extra_env={"OMAHOUSE_JSON": "1"})
    assert json.loads(from_env.stdout)["user"] == USER


def check_status_about_nobody_in_particular(box):
    """No user named is whoever ran it."""
    mine = box.run("status")
    assert mine.returncode == 0, mine.stderr
    assert f"omahouse status — {USER}" in mine.stdout


def check_status_refuses_an_account_that_is_not_there(box):
    missing = box.run("status", "nobody-by-that-name")
    assert missing.returncode == 2, (missing.returncode, missing.stderr)
    assert "no account named" in missing.stderr
    assert missing.stdout == ""


def check_status_of_a_user_who_is_not_logged_in(box):
    """No app.slice is not a failure. It is somebody who is not logged in."""
    empty = Path(box.root / "empty-cgroup")
    empty.mkdir(exist_ok=True)
    away = box.run("status", USER, extra_env={"OMAHOUSE_CGROUP_ROOT": str(empty)})
    assert away.returncode == 0, away.stderr
    assert "is not logged in" in away.stdout


def check_a_broken_profiles_file_is_not_an_empty_one(box):
    """1 and not 2: a file that will not parse must never read as `no profile`.

    Fiscalising somebody by guessing what a half-written file meant is the one
    thing worse than refusing to start.
    """
    (box.config / "profiles.json").write_text('{"schemaVersion": 1, "profiles": [{}]}')
    broken = box.run("status", USER)
    assert broken.returncode == 1, (broken.returncode, broken.stderr)
    assert broken.stdout == ""
    assert "omahouse:" in broken.stderr

    unreadable = box.run("profile", "list")
    assert unreadable.returncode == 1
    (box.config / "profiles.json").unlink()


# -- report -------------------------------------------------------------------

def check_report_of_one_day(box):
    box.write_ledger(TODAY)
    reported = box.run("report", USER)
    assert reported.returncode == 0, reported.stderr
    assert TODAY.isoformat() in reported.stdout
    assert row_for(reported.stdout, "session") == ["session", "1h10m"]
    assert row_for(reported.stdout, "chromium") == ["chromium", "45m"]

    # The grants and the events of spec.md §4, which are not decoration: they are
    # where a once-only decision remembers that it has fired.
    assert "GRANTS" in reported.stdout
    assert "+10m" in reported.stdout
    assert "EVENTS" in reported.stdout
    assert "5 minutes left" in reported.stdout
    assert "app-Hyprland-steam-9f2c11ab.scope" in reported.stdout


def check_report_of_a_range(box):
    earlier = TODAY - timedelta(days=2)
    box.write_ledger(earlier, seconds=600)
    box.write_ledger(TODAY)
    ranged = box.run("report", USER, "--since", earlier.isoformat())
    assert ranged.returncode == 0, ranged.stderr
    assert earlier.isoformat() in ranged.stdout
    assert TODAY.isoformat() in ranged.stdout
    # The day between them has no file, and is left out rather than printed as a
    # row of zeroes.
    assert (TODAY - timedelta(days=1)).isoformat() not in ranged.stdout
    assert "TOTAL" in ranged.stdout

    document = json.loads(box.run("--json", "report", USER, "--since",
                                  earlier.isoformat()).stdout)
    assert [day["date"] for day in document["days"]] == \
        [earlier.isoformat(), TODAY.isoformat()]
    assert document["totals"]["session"] == 600 + 4210
    assert document["since"] == earlier.isoformat()
    # The per-file preamble is not repeated on every day of a range.
    assert "schemaVersion" not in document["days"][0]


def check_report_of_a_day_nobody_spent(box):
    """Useful words, not an empty table and not a stack trace."""
    quiet = box.run("report", "somebody-with-no-ledger")
    assert quiet.returncode == 0, quiet.stderr
    assert quiet.stdout == ""
    assert "nothing counted" in quiet.stderr
    assert str(box.state) in quiet.stderr

    # Under --json it is still one document: an empty answer is an empty array,
    # not no output at all.
    document = json.loads(box.run("--json", "report", "somebody-with-no-ledger").stdout)
    assert document["days"] == []
    assert document["totals"] == {}


def check_report_refuses_a_since_that_is_not_a_date(box):
    for bad in ("yesterday", "2026-13-40", (TODAY + timedelta(days=1)).isoformat(),
                (TODAY - timedelta(days=800)).isoformat()):
        refused = box.run("report", USER, "--since", bad)
        assert refused.returncode == 1, (bad, refused.returncode)
        assert refused.stdout == "", bad
        assert "--since" in refused.stderr, bad

    missing_value = box.run("report", USER, "--since")
    assert missing_value.returncode == 1
    assert "--since wants a date" in missing_value.stderr


def check_report_wants_a_user(box):
    vague = box.run("report")
    assert vague.returncode == 1
    assert "which user" in vague.stderr


# -- profile ------------------------------------------------------------------

def check_profile_list_before_anything_is_configured(box):
    empty = box.run("profile", "list")
    assert empty.returncode == 0, empty.stderr
    assert empty.stdout == ""
    assert "there is no" in empty.stderr
    assert str(box.config / "profiles.json") in empty.stderr

    # One document on stdout even so: an empty answer is `[]`.
    assert json.loads(box.run("--json", "profile", "list").stdout) == []


def check_profile_list(box):
    box.write_profiles()
    listed = box.run("profile", "list")
    assert listed.returncode == 0, listed.stderr
    assert columns(listed.stdout.splitlines()[0])[0] == "USER"
    assert row_for(listed.stdout, USER) == [USER, "Júlia", "yes", "yes", "deny", "2", "3"]

    document = json.loads(box.run("--json", "profile", "list").stdout)
    assert document == [{
        "user": USER, "displayName": "Júlia", "enabled": True, "enforce": True,
        "default": "deny", "rules": 2, "budgets": 3,
    }]


def check_profile_show(box):
    box.write_profiles()
    shown = box.run("profile", "show", USER)
    assert shown.returncode == 0, shown.stderr
    assert f"omahouse profile — {USER} (Júlia)" in shown.stdout
    assert row_for(shown.stdout, "default") == ["default", "deny"]
    assert row_for(shown.stdout, "grace") == ["grace", "20s"]
    assert "10m, 5m, 1m left" in shown.stdout
    assert row_for(shown.stdout, "allow") == ["allow", "chromium"]
    assert row_for(shown.stdout, "session") == ["session", "*", "2h00m", "logs out"]
    assert row_for(shown.stdout, "code") == ["code", "code", "—", "never runs out"]

    document = json.loads(box.run("--json", "profile", "show", USER).stdout)
    assert document["user"] == USER
    assert document["budgets"][0]["id"] == "session"

    absent = box.run("profile", "show", "somebody-else")
    assert absent.returncode == 2, (absent.returncode, absent.stderr)
    assert "no profile for" in absent.stderr
    assert absent.stdout == ""


def check_profile_refuses_what_it_does_not_do(box):
    vague = box.run("profile")
    assert vague.returncode == 1
    assert "list or show" in vague.stderr

    unknown = box.run("profile", "frobnicate")
    assert unknown.returncode == 1
    assert "unknown subcommand" in unknown.stderr

    writing = box.run("profile", "add", "julia")
    assert writing.returncode == 1
    assert "stage 5" in writing.stderr

    extra = box.run("profile", "list", USER)
    assert extra.returncode == 1
    assert "profile show" in extra.stderr


# -- the promise of the stage -------------------------------------------------

def check_nothing_was_written(box):
    """Stage 4 reads. Nothing in it writes to /etc or /var.

    Asserted by fingerprint rather than by reading the code: every file under
    both roots, its size and its mtime, before and after a run of every verb.
    """
    def fingerprint(root):
        return sorted(
            (str(path.relative_to(root)), path.stat().st_size, path.stat().st_mtime_ns)
            for path in root.rglob("*") if path.is_file())

    box.write_profiles()
    box.write_ledger(TODAY)
    before = (fingerprint(box.config), fingerprint(box.state))
    for args in (("status", USER), ("status",), ("report", USER),
                 ("report", USER, "--since", (TODAY - timedelta(days=3)).isoformat()),
                 ("profile", "list"), ("profile", "show", USER)):
        assert box.run(*args).returncode == 0, args
    assert (fingerprint(box.config), fingerprint(box.state)) == before


def main():
    assert CLI.is_file(), f"missing CLI at {CLI}"
    cases = [
        check_version_is_said_once,
        check_help_and_refusals,
        check_status_scans_without_a_profile,
        check_status_reports_what_it_cannot_see,
        check_status_about_nobody_in_particular,
        check_status_refuses_an_account_that_is_not_there,
        check_status_of_a_user_who_is_not_logged_in,
        check_status_with_a_profile,
        check_status_json,
        check_a_broken_profiles_file_is_not_an_empty_one,
        check_report_of_one_day,
        check_report_of_a_range,
        check_report_of_a_day_nobody_spent,
        check_report_refuses_a_since_that_is_not_a_date,
        check_report_wants_a_user,
        check_profile_list_before_anything_is_configured,
        check_profile_list,
        check_profile_show,
        check_profile_refuses_what_it_does_not_do,
        check_nothing_was_written,
    ]
    for case in cases:
        # A machine of its own per case: a profiles.json one case wrote is a
        # profiles.json the next one would have to know about.
        with tempfile.TemporaryDirectory(prefix="omahouse-cli-") as directory:
            case(Box(directory))
    print(f"test_cli.py: {len(cases)} cases passed")


if __name__ == "__main__":
    main()
