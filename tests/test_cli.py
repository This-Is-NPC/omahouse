#!/usr/bin/env python3
"""The CLI contract, end to end, as an ordinary user with nothing installed.

Every root the binary reads and writes moves by variable -- the cgroup tree, the
process table, /etc/omahouse and /var/lib/omahouse -- so a profile is created,
edited and read back here without root and without a graphical session with a
Chromium open in it. The fake cgroup tree is built out of unit names that were
measured: poc/findings.md for the flatpak scope, the escaped one and the shim of
round 4, the development machine for the rest.

Three things this suite must never do to the machine it runs on: create an
account, write under /etc, and put a notification on somebody's screen. The
first goes through an injected `useradd` that records the call, the second is
asserted by being refused, and the third through an injected `notify-send` that
every run gets, whether or not the case is about `watch`.

A verb that parses and does nothing is indistinguishable from one that works
until somebody depends on it."""

import grp
import json
import os
import pty
import pwd
import re
import subprocess
import struct
import tempfile
import time
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
#
# Each scope is (processes, first pid, what those processes are running). The
# pids do not overlap between scopes because the executable of a scope is read
# per process, and two scopes sharing a pid would be two scopes sharing an
# answer. `None` is a scope whose processes have no executable to read, which is
# what an unprivileged look at another account's process gets.
SCOPES = {
    "app-graphical.slice/app-Hyprland-chromium-031bdc27.scope":
        (19, 4000, "/usr/lib/chromium/chromium"),
    "app-code-3579042.scope": (12, 4100, "/usr/share/code/code"),
    "app-org.chromium.Chromium-3735302.scope": (4, 4200, "/usr/lib/chromium/chromium"),
    # Round 4: the terminal is named after the shim that opened it.
    "app-graphical.slice/app-Hyprland-xdg\\x2dterminal\\x2dexec-151e8e07.scope":
        (4, 4300, "/usr/bin/alacritty"),
    # The opposite failure, and the reason the executable is not a verdict: every
    # flatpak on a machine runs the same one.
    "app-graphical.slice/app-flatpak-org.freedesktop.Platform-2351381583.scope":
        (4, 4400, "/usr/bin/bwrap"),
    # A scope on its way out: the unit is there, nothing is in it.
    "app-graphical.slice/app-Hyprland-sleep-7865852f.scope": (0, 4500, None),
    # Under app.slice and not an app scope name. This machine has thirty.
    "app-graphical.slice/tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope":
        (18, 4600, "/usr/bin/bash"),
    # Plumbing on the app side of the tree. Never an app.
    "dconf.service": (1, 4700, None),
    # Round 4 of poc/findings.md, the whole reason the executable is read at all:
    # seven scopes of this name on this machine, and VS Code inside every one.
    "app-graphical.slice/app-Hyprland-gtk\\x2dlaunch-4f8ae1c3.scope":
        (7, 4800, "/usr/share/code/code"),
}

SESSION = {
    "wayland-wm@hyprland.desktop.service": 8,
    "pipewire.service": 1,
    "wireplumber.service": 1,
    "dbus-broker.service": 2,
    "xdg-desktop-portal.service": 1,
}


def write_cgroup(path, pids, first_pid=4000):
    path.mkdir(parents=True, exist_ok=True)
    (path / "cgroup.procs").write_text(
        "".join(f"{first_pid + i}\n" for i in range(pids)))


def write_process(proc_root, pid, executable):
    """A process, as far as the executable of a scope is concerned.

    The link dangles on purpose: the kernel's own one points at a file a package
    upgrade may have replaced since, so the code under test reads the link and
    never the file.
    """
    directory = proc_root / str(pid)
    directory.mkdir(parents=True, exist_ok=True)
    os.symlink(executable, directory / "exe")


def build_cgroup_tree(root, proc_root):
    """A user@<uid>.service the way systemd lays one out, and its processes."""
    manager = root / "user.slice" / f"user-{UID}.slice" / f"user@{UID}.service"
    for relative, (pids, first_pid, executable) in SCOPES.items():
        write_cgroup(manager / "app.slice" / relative, pids, first_pid)
        if executable is None:
            continue
        for i in range(pids):
            write_process(proc_root, first_pid + i, executable)
    for unit, pids in SESSION.items():
        write_cgroup(manager / "session.slice" / unit, pids, 9000)
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
        self.proc = self.root / "proc"
        self.cgroup = build_cgroup_tree(self.root / "cgroup", self.proc)
        self.config = self.root / "etc"
        self.state = self.root / "var"
        self.config.mkdir()
        self.state.mkdir()
        # The third root, and the one this suite would do the most damage
        # without: /etc/chromium/policies/managed is the browser policy of
        # whoever is running the suite. Pointed somewhere of its own for every
        # case, always, for the same reason `notify-send` is.
        self.chromium = self.root / "chromium"
        self.notified = self.root / "notified.log"
        self.notify_send = self.root / "notify-send"
        # A tab between the summary and the body, because both of them are
        # sentences with spaces in them and the assertions are about the words.
        self.notify_send.write_text(
            f'#!/bin/sh\n{{ printf "%s\\t" "$@"; printf "\\n"; }} >> {self.notified}\n')
        self.notify_send.chmod(0o755)
        # The fourth and fifth roots, and they arrived with presence: the DRM
        # connectors of /sys/class/drm and the `loginctl` that says which session
        # the seat is showing. Both are pointed somewhere of their own for every
        # case, always, and for a reason the other roots do not have -- the real
        # ones answer differently depending on whether whoever started the suite
        # has walked away from the machine since, and a suite whose result
        # depends on that is a suite nobody can read a failure of.
        self.drm = self.root / "drm"
        self.drm.mkdir()
        self.loginctl = self.root / "loginctl"
        self.screen("connected", "On")
        self.seat(uid=UID)
        # The sixth root, and the newest: /run/user, where the browser's native
        # messaging host writes the site in the front tab. Its own tree for the
        # same reason the others have one -- this suite has to be able to drive a
        # browser reporting a site without a browser, without a session, and
        # without touching the real /run/user of whoever is running it.
        self.runtime = self.root / "runtime"
        self.runtime.mkdir()

    def screen(self, status, dpms, name="card0-Virtual-1"):
        """One DRM connector, in the shape the VM's really has.

        `enabled` is written and never read: the measurement is that Hyprland
        turning a monitor off moves it to `disabled` in the same modeset that
        moves `dpms` to `Off`, so a reader that filtered on it would find no
        screens at exactly the moment there is a dark one to find.
        """
        connector = self.drm / name
        connector.mkdir(parents=True, exist_ok=True)
        (connector / "status").write_text(status + "\n")
        (connector / "enabled").write_text(
            ("enabled" if dpms == "On" else "disabled") + "\n")
        (connector / "dpms").write_text(dpms + "\n")

    def seat(self, uid=None, fails=False):
        """A `loginctl` that is a script, so the seat is the suite's to decide.

        The same door `$OMAHOUSE_USERADD` and `$OMAHOUSE_NOTIFY_SEND` are. Here
        it is doubly worth having: the real `loginctl` would answer about the
        session of whoever is running this, which on a build machine is nobody.
        """
        # Anything that is not one of the two reads refuses, loudly. This
        # variable is also the one `Enforce` ends a session through, and a fake
        # that answered `terminate-user` with a cheerful exit 0 would be a suite
        # that could not tell a refusal from a logout.
        refuse = '  *) echo "not this loginctl: $*" >&2; exit 1 ;;\n'
        if fails:
            body = "exit 1\n"
        elif uid is None:
            body = ('case "$1" in\n'
                    "  show-seat) echo ActiveSession= ;;\n"
                    + refuse + "esac\n")
        else:
            body = ('case "$1" in\n'
                    "  show-seat) echo ActiveSession=7 ;;\n"
                    f"  show-session) echo User={uid} ;;\n"
                    + refuse + "esac\n")
        self.loginctl.write_text("#!/bin/sh\n" + body)
        self.loginctl.chmod(0o755)

    def focus_file(self):
        return self.runtime / str(UID) / "omahouse" / "focus"

    def browsing(self, site, seconds_ago=1, raw=None):
        """The focus file, as the native messaging host would have left it.

        `raw` writes bytes of its own instead, which is how the ways the file can
        be wrong are exercised -- it is a file the person being measured owns and
        can put anything at all into.
        """
        path = self.focus_file()
        path.parent.mkdir(parents=True, exist_ok=True)
        if raw is not None:
            path.write_bytes(raw)
            return
        stamp = int(time.time()) - seconds_ago
        path.write_text(f"{stamp} {site}\n")

    def stopped_browsing(self):
        path = self.focus_file()
        if path.exists():
            path.unlink()

    def policy(self):
        """The managed policy on this box, or None when there is no file.

        None is the answer the whole of docs/design.md §11 turns on: no rules
        anywhere is no file, and not an empty one.
        """
        path = self.chromium / "omahouse.json"
        return json.loads(path.read_text()) if path.exists() else None

    def said(self):
        """Every notification the binary handed to `notify-send`, as it built it.

        The real one is never on the other end. `watch` warns, and a suite that
        proves it does has no business making the machine it runs on flash a
        message at whoever started it -- which is the same reasoning that keeps
        `useradd` behind a variable.
        """
        if not self.notified.exists():
            return []
        return [line.split("\t")[:-1]
                for line in self.notified.read_text().splitlines() if line]

    def write_profiles(self, document=None):
        (self.config / "profiles.json").write_text(
            json.dumps(document if document is not None else profile_document()))

    def write_ledger(self, day, seconds=4210):
        directory = self.state / USER
        directory.mkdir(parents=True, exist_ok=True)
        (directory / f"{day.isoformat()}.json").write_text(
            json.dumps(ledger_document(day, seconds)))

    def write_day(self, day, budgets):
        """A day with nothing in it but the seconds already spent.

        The one above carries grants and events, which is what `report` is asked
        to print. A cycle of `watch` is about what happens next, and it has to
        start from a day nobody has been warned in yet.
        """
        directory = self.state / USER
        directory.mkdir(parents=True, exist_ok=True)
        (directory / f"{day.isoformat()}.json").write_text(json.dumps({
            "schemaVersion": 1,
            "user": USER,
            "date": day.isoformat(),
            "budgets": budgets,
            "grants": [],
            "events": [],
        }))

    def day(self, day):
        path = self.state / USER / f"{day.isoformat()}.json"
        return json.loads(path.read_text()) if path.exists() else None

    def fake_useradd(self):
        """A useradd that records the call and creates nothing.

        plan.md says it in as many words: `useradd` is irreversible enough never
        to be exercised outside the VM, so stage 5 writes the verb and stage 7 is
        what runs it in the box. What is asserted here is that the right command
        is built -- the real one is never on the other end of it, and no account
        appears on the machine that runs this suite.
        """
        recorded = self.root / "useradd.log"
        script = self.root / "useradd"
        script.write_text(f'#!/bin/sh\necho "$@" >> {recorded}\n')
        script.chmod(0o755)
        return script, recorded

    def run(self, *args, extra_env=None, system_roots=False):
        env = {
            **os.environ,
            "OMAHOUSE_CGROUP_ROOT": str(self.cgroup),
            "OMAHOUSE_PROC_ROOT": str(self.proc),
            "OMAHOUSE_CONFIG_DIR": str(self.config),
            "OMAHOUSE_STATE_DIR": str(self.state),
            # Always, and not only for the cases about `watch`: a stray
            # notification on somebody's screen is the kind of thing a suite gets
            # to do exactly once before nobody runs it again.
            "OMAHOUSE_NOTIFY_SEND": str(self.notify_send),
            "OMAHOUSE_CHROMIUM_POLICY_DIR": str(self.chromium),
            # Presence: the screens and the seat, both of them the suite's own.
            "OMAHOUSE_DRM_ROOT": str(self.drm),
            "OMAHOUSE_LOGINCTL": str(self.loginctl),
            # Time per site: the file the browser's host writes, and never the
            # real one.
            "OMAHOUSE_RUNTIME_ROOT": str(self.runtime),
        }
        env.pop("OMAHOUSE_JSON", None)
        # The roots put back where the machine keeps them, which is how the
        # refusal to write without privilege is exercised.
        if system_roots:
            env.pop("OMAHOUSE_CONFIG_DIR")
            env.pop("OMAHOUSE_STATE_DIR")
        if extra_env:
            env.update(extra_env)
        return subprocess.run(
            [str(CLI), *map(str, args)],
            cwd=str(ROOT), env=env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            # Never the terminal the suite was started from.
            stdin=subprocess.DEVNULL,
            # `watch` is the one verb that can decide to keep running, and a
            # suite that hangs is a suite nobody can read the failure of.
            timeout=60,
        )


def columns(line):
    return re.split(r" {2,}", line.strip())


def below(stdout, heading):
    """Everything printed after a heading.

    One screen holds several tables and two of them start with an `APP` column,
    so the anchor for a row has to be the heading above the table and not the
    header row of it.
    """
    assert heading in stdout, heading
    return stdout.split(heading, 1)[1]


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

    # The writing verbs of docs/design.md §7 are on the screen too, now that they do
    # something.
    for verb in ("allow", "deny", "limit", "grant", "profile add"):
        assert verb in helped.stdout, verb

    # The loop is on the screen too, and so are both halves of what it does when
    # a budget runs out. `session.slice` is named because the promise that the
    # compositor survives is the promise somebody has to be able to read before
    # they switch enforcement on, and `blocked` is named because a logout that
    # were only `terminate-user` would be theatre on any machine with autologin.
    assert "watch" in helped.stdout
    assert "cgroup.kill" in helped.stdout
    assert "`session.slice` is never touched" in helped.stdout
    assert "/etc/omahouse/blocked" in helped.stdout

    # An option that belongs to another verb is refused rather than dropped: a
    # limit somebody asked for and did not get is worse than a usage error.
    stray = box.run("deny", USER, "code", "--limit", "45m")
    assert stray.returncode == 1
    assert "deny takes no --limit" in stray.stderr, stray.stderr


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
    """docs/design.md §5, and the two things it asks for are not the same thing.

    A `tmux-spawn-<uuid>.scope` is counted and not named: omahouse knows it is
    there, spends the session on it, and could close it, and what it has no id
    for is a limit of its own or a rule that lets it through by name. That is a
    section of its own and not a blind spot. The compositor's processes are the
    blind spot: an app started by a raw `exec` is in there and cannot be told
    from Hyprland, so it is neither counted nor closable.
    """
    seen = box.run("status", USER)

    assert "Counted, not named" in seen.stdout
    assert "1 scope under app.slice omahouse could not name" in seen.stdout
    assert "tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope" in seen.stdout
    # The new truth, in the words the screen uses: counted towards `*`, and
    # without a name there is no limit of its own and no allowing it by name.
    assert "These are in the total" in seen.stdout
    assert "cannot be given a limit of their own" in seen.stdout
    # It has no id, so no rule can reach it and the profile default is left.
    # There is no profile in this case, and the line says exactly that.
    assert "the verdict on them would be its default" in seen.stdout

    assert "Out of reach" in seen.stdout
    assert "8 processes in session.slice" in seen.stdout
    assert "wayland-wm@hyprland.desktop.service" in seen.stdout

    # Counted, and still not an app with a name: the tmux scope is not a row of
    # the table, because there is no id to put in the first column.
    assert row_for(seen.stdout, "tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope") \
        is None
    # Session plumbing is a unit somebody's package declared, and is not in the
    # count.
    assert "pipewire" not in seen.stdout
    assert "dbus-broker" not in seen.stdout


def check_status_says_what_the_default_verdict_does_to_a_nameless_scope(box):
    """The consequence of the rule, said on the screen that would have to
    explain it.

    A scope with no id cannot match a rule, so the profile's default is the whole
    of its verdict. Under an allowlist with the teeth in, that closes it -- which
    is coherent, and is exactly the sentence an operator needs to find before
    they wonder why the terminal shut.
    """
    box.write_profiles(watching_profile(enforce=False, default="allow"))
    allowed = box.run("status", USER)
    assert "they take the default verdict, allow" in allowed.stdout

    box.write_profiles(watching_profile(enforce=False, default="deny"))
    observing = box.run("status", USER)
    assert "they take the default verdict, deny" in observing.stdout
    assert "only observing" in observing.stdout

    box.write_profiles(watching_profile(enforce=True, default="deny"))
    enforcing = box.run("status", USER)
    assert "the teeth are in, so they are closed" in enforcing.stdout


def check_status_says_what_a_scope_really_holds(box):
    """poc/findings.md round 4: the shim erases the name of the app.

    A rule is written about an id, and on this machine seven ids read
    `gtk-launch` and hold VS Code. `status` says so, because an operator who is
    about to allow `gtk-launch` has to be told what that lets in. The verdict
    itself does not move: `Policy::evaluate` goes on matching by id, and this is
    a sentence rather than a decision.
    """
    seen = box.run("status", USER)
    assert seen.returncode == 0, seen.stderr
    inside = below(seen.stdout, "Not what the name says")

    assert row_for(inside, "gtk-launch", after="APP") == \
        ["gtk-launch", "/usr/share/code/code", "7 of 7", "1"]
    assert row_for(inside, "xdg-terminal-exec", after="APP") == \
        ["xdg-terminal-exec", "/usr/bin/alacritty", "4 of 4", "1"]
    # The flatpak reads as a disagreement too, and it is the opposite failure:
    # its processes really do all run /usr/bin/bwrap. Both are printed with what
    # is inside them so that whoever is writing the rule can tell them apart.
    assert row_for(inside, "org.freedesktop.Platform", after="APP")[1] == "/usr/bin/bwrap"

    # And an id that names its own program is not in the block at all.
    assert row_for(inside, "chromium", after="APP") is None
    assert row_for(inside, "code", after="APP") is None

    document = json.loads(box.run("--json", "status", USER).stdout)
    ids = {scope["id"]: scope for scope in document["scopes"]}
    assert ids["chromium"]["exe"] == "/usr/lib/chromium/chromium"
    assert ids["chromium"]["exeProcesses"] == 19
    assert ids["chromium"]["exeAgrees"] is True
    assert ids["org.chromium.Chromium"]["exeAgrees"] is True
    assert ids["gtk-launch"]["exe"] == "/usr/share/code/code"
    assert ids["gtk-launch"]["exeAgrees"] is False
    assert ids["org.freedesktop.Platform"]["exeAgrees"] is False
    # A scope with no id has nothing to agree with, and what it is running is
    # still the only thing anybody can say about it.
    assert document["unnamed"][0]["exe"] == "/usr/bin/bash"

    # Nothing readable is no opinion, and it must not read as a disagreement:
    # this is what an unprivileged run looking at another account gets.
    blind = box.run("status", USER, extra_env={"OMAHOUSE_PROC_ROOT": str(box.root / "nothing")})
    assert blind.returncode == 0, blind.stderr
    assert "Not what the name says" not in blind.stdout
    unread = json.loads(
        box.run("--json", "status", USER,
                extra_env={"OMAHOUSE_PROC_ROOT": str(box.root / "nothing")}).stdout)
    assert all(scope["exe"] is None and scope["exeAgrees"] is None
               for scope in unread["scopes"])


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


def check_status_says_who_is_in_front_of_the_machine(box):
    """Presence, on the screen an operator reads — the three states, measured.

    `.temp/spike-extension.md` §5 measured a browser answering `active` for
    twenty-five minutes with the monitor physically off. So the question is asked
    of the machine: which session the seat is showing, and whether the screens
    are lit. Both roots are this box's own, which is what lets the three states
    be produced here rather than only in a VM with somebody walking away from it.
    """
    box.write_profiles()

    # Using: the seat is showing this session and the screen is on.
    using = box.run("status", USER)
    assert using.returncode == 0, using.stderr
    assert f"{USER} is at the machine" in using.stdout, using.stdout
    assert "Screen on, seat showing uid" in using.stdout, using.stdout
    # And the sentence that stops the obvious misreading: a budget still being
    # spent beside `away` is not omahouse having quietly stopped counting.
    assert "takes nothing away" in using.stdout, using.stdout

    # Screen off: the connector is still connected, and dark. This is the state
    # the browser could not see.
    box.screen("connected", "Off")
    dark = box.run("status", USER)
    assert dark.returncode == 0, dark.stderr
    assert "away: every connected screen is off" in dark.stdout, dark.stdout
    assert json.loads(box.run("status", USER, "--json").stdout)["presence"] == {
        "present": False, "reason": "screen-off", "screen": "off",
        "seatRead": True, "seatUid": UID, "today": {},
    }

    # The seat showing somebody else, with the screen lit. Only logind knows the
    # difference, which is the whole reason it is asked.
    box.screen("connected", "On")
    box.seat(uid=UID + 1)
    elsewhere = box.run("status", USER)
    assert "away: the seat is showing another session" in elsewhere.stdout, elsewhere.stdout

    # A seat that could not be read is not an absence, and it says so rather
    # than deciding.
    box.seat(fails=True)
    blind = box.run("status", USER)
    assert "not known" in blind.stdout, blind.stdout
    assert json.loads(
        box.run("status", USER, "--json").stdout)["presence"]["present"] is None


def check_watch_writes_presence_beside_the_budgets_and_never_into_them(box):
    """The day's file gains the presence, and the budgets are what they were.

    This is the promise of the step in one case: presence is measured and
    reported and takes nothing away. `docs/design.md` §5 bills an app for
    running, and a screen going dark does not change that -- what will use this
    is the time per site, later.
    """
    box.write_profiles(watching_profile())

    lit = box.run("watch", "--once")
    assert lit.returncode == 0, lit.stderr
    day = box.day(TODAY)
    assert day["presence"] == {"using": 2}, day
    spent = dict(day["budgets"])

    box.screen("connected", "Off")
    dark = box.run("watch", "--once")
    assert dark.returncode == 0, dark.stderr
    # The journal says it on the same line as the counting, which is what makes
    # the disagreement legible.
    assert "screen-off" in dark.stderr, dark.stderr

    day = box.day(TODAY)
    assert day["presence"] == {"screen-off": 2, "using": 2}, day
    # Every budget gained exactly the tick it would have gained with somebody in
    # the room. That is the assertion this whole case exists for.
    assert day["budgets"] == {name: seconds + 2 for name, seconds in spent.items()}, day

    document = json.loads(box.run("watch", "--once", "--json").stdout)
    assert document["seat"] == {"read": True, "screen": "off", "uid": UID}
    assert document["users"][0]["presence"]["reason"] == "screen-off"
    assert document["users"][0]["presence"]["present"] is False

    # And it comes back out of `report`, beside the budgets and not among them.
    reported = box.run("report", USER)
    assert "PRESENCE" in reported.stdout, reported.stdout
    assert "screen-off" in below(reported.stdout, "PRESENCE"), reported.stdout


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

    # The grants and the events of docs/design.md §4, which are not decoration: they are
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

    extra = box.run("profile", "list", USER)
    assert extra.returncode == 1
    assert "profile show" in extra.stderr


# -- writing ------------------------------------------------------------------

def a_wheel_user():
    """Somebody on this machine who administers it, or None.

    Read from the machine rather than invented, because the refusal is about
    what NSS says and not about a name. `root` is asserted separately and is
    always there; a wheel member is what the rule of docs/design.md §1 is actually
    about, and a machine without one is a machine where that half is skipped
    out loud.
    """
    try:
        wheel = grp.getgrnam("wheel")
    except KeyError:
        return None
    if wheel.gr_mem:
        return wheel.gr_mem[0]
    for entry in pwd.getpwall():
        if entry.pw_gid == wheel.gr_gid:
            return entry.pw_name
    return None


def check_a_profile_from_nothing_to_read_back(box):
    """Create, edit and read one profile end to end, as an ordinary user.

    No root anywhere in here: every root the binary writes to has been moved to
    a directory of its own, which is the same door stage 4 read through. What
    lands on disk is asserted against docs/design.md §4 field by field, because a verb
    that writes a file only this program can read is a verb that has quietly
    invented its own format.
    """
    made = box.run("profile", "add", "julia", "--name", "Júlia")
    assert made.returncode == 0, made.stderr
    assert str(box.config / "profiles.json") in made.stdout
    # A new profile observes, and it allows -- docs/design.md §5 and §4. Somebody who
    # gets this far and stops has a profile that counts and reports and cannot
    # lock anybody out of their own machine.
    assert "observing" in made.stdout
    # And the account it names does not exist yet, which is said rather than
    # refused: a profile can be written before its account and outlive it.
    assert "no account named julia" in made.stderr

    written = json.loads((box.config / "profiles.json").read_text())
    assert written["schemaVersion"] == 1
    assert written["profiles"][0] == {
        "user": "julia", "displayName": "Júlia", "enabled": True, "enforce": False,
        "default": "allow", "warnAt": [10, 5, 1], "grace": 20,
        "rules": [], "budgets": [],
    }

    # The four editing verbs of docs/design.md §7, in the order somebody configuring
    # would reach for them.
    assert box.run("profile", "default", "julia", "--deny").returncode == 0
    assert box.run("allow", "julia", "chromium", "--limit", "45m").returncode == 0
    assert box.run("allow", "julia", "code").returncode == 0
    assert box.run("deny", "julia", "steam").returncode == 0
    assert box.run("limit", "julia", "--session", "2h").returncode == 0
    assert box.run("limit", "julia", "--budget", "code=1h30m").returncode == 0
    assert box.run("profile", "enforce", "julia", "--on").returncode == 0

    profile = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    assert profile["default"] == "deny"
    assert profile["enforce"] is True
    # In the order they were written, because the first rule that names an app is
    # the one that wins and an operator points at the line they typed.
    assert profile["rules"] == [
        {"match": "chromium", "verdict": "allow"},
        {"match": "code", "verdict": "allow"},
        {"match": "steam", "verdict": "deny"},
    ]
    # `allow --limit` is sugar for the rule and the budget at once, docs/design.md §7.
    # The session is the budget whose selector is `*` and nothing else, §2.
    assert profile["budgets"] == [
        {"id": "chromium", "match": "chromium", "dailyMinutes": 45, "onExhausted": "close"},
        {"id": "session", "match": "*", "dailyMinutes": 120, "onExhausted": "logout"},
        {"id": "code", "match": "code", "dailyMinutes": 90, "onExhausted": "close"},
    ]

    # And it reads back through the verbs that were written before it existed.
    shown = box.run("profile", "show", "julia")
    assert shown.returncode == 0, shown.stderr
    assert row_for(shown.stdout, "session") == ["session", "*", "2h00m", "logs out"]
    assert row_for(shown.stdout, "chromium") == ["chromium", "chromium", "45m", "closes"]
    assert json.loads(box.run("--json", "profile", "show", "julia").stdout) == profile
    assert row_for(box.run("profile", "list").stdout, "julia")[:5] == \
        ["julia", "Júlia", "yes", "yes", "deny"]

    # Editing a rule that is already there changes it in place. Two lines about
    # one app would leave the second dead.
    assert box.run("deny", "julia", "chromium").returncode == 0
    edited = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    assert edited["rules"][0] == {"match": "chromium", "verdict": "deny"}
    assert len(edited["rules"]) == 3
    # And a new limit on a budget that exists is only the number: what it does
    # when it runs out was decided once.
    assert box.run("limit", "julia", "--budget", "chromium=30m").returncode == 0
    edited = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    assert edited["budgets"][0] == {"id": "chromium", "match": "chromium",
                                    "dailyMinutes": 30, "onExhausted": "close"}

    # Taken off the books again, and what was counted is left where it is.
    removed = box.run("profile", "remove", "julia", "--keep-account")
    assert removed.returncode == 0, removed.stderr
    assert json.loads((box.config / "profiles.json").read_text())["profiles"] == []
    assert box.run("profile", "show", "julia").returncode == 2


def check_grant_writes_the_days_ledger(box):
    """The verb docs/design.md §7 says is the difference between an operator and a form.

    Ten minutes, with the app open, without restarting anything. It lands in the
    day's own file, so it expires when the file does.
    """
    box.run("profile", "add", "julia")
    box.run("limit", "julia", "--session", "2h")

    given = box.run("grant", "julia", "--session", "10m")
    assert given.returncode == 0, given.stderr
    assert "+10m" in given.stdout
    assert "2h10m left today" in given.stdout

    ledger = json.loads((box.state / "julia" / f"{TODAY.isoformat()}.json").read_text())
    assert ledger["schemaVersion"] == 1
    assert ledger["user"] == "julia"
    assert ledger["date"] == TODAY.isoformat()
    assert len(ledger["grants"]) == 1
    grant = ledger["grants"][0]
    assert grant["budget"] == "session"
    assert grant["minutes"] == 10
    assert grant["by"] == USER
    assert grant["at"].startswith(TODAY.isoformat())

    # A second one adds to the first rather than replacing it.
    assert box.run("grant", "julia", "--budget", "session=5m").returncode == 0
    ledger = json.loads((box.state / "julia" / f"{TODAY.isoformat()}.json").read_text())
    assert [g["minutes"] for g in ledger["grants"]] == [10, 5]

    # And the report reads it back.
    reported = box.run("report", "julia")
    assert "GRANTS" in reported.stdout
    assert "+10m" in reported.stdout

    document = json.loads(box.run("--json", "grant", "julia", "--session", "5m").stdout)
    assert document["budget"] == "session"
    assert document["minutes"] == 5
    assert document["limitSeconds"] == 120 * 60 + 20 * 60

    # A budget nobody wrote is refused rather than invented: time added to a
    # counter the daemon never looks at would read on the report as though it had
    # been given.
    nowhere = box.run("grant", "julia", "--budget", "minecraft=15m")
    assert nowhere.returncode == 2, nowhere.stderr
    assert "no budget called minecraft" in nowhere.stderr


def check_allow_warns_about_what_is_really_inside(box):
    """poc/findings.md round 4, at the moment it matters most.

    Allowing `gtk-launch` is allowing whatever gtk-launch launches next. The CLI
    says what is in there and writes the rule anyway: it may be exactly what
    somebody meant, and the same reading calls a flatpak a shim.
    """
    # Written by hand rather than by `profile add`, because the scopes in the
    # fake tree belong to whoever runs the suite and that account is usually in
    # wheel -- which `profile add` refuses, and rightly. The warning is about
    # what is open in a session, so it has to be that account's session.
    box.write_profiles({"schemaVersion": 1, "profiles": [{"user": USER}]})

    warned = box.run("allow", USER, "gtk-launch")
    assert warned.returncode == 0, warned.stderr
    assert "gtk-launch" in warned.stdout
    assert "is not the name of one program" in warned.stderr
    # It says what is inside, which is the whole point of warning.
    assert "/usr/share/code/code" in warned.stderr
    assert "7 of 7 processes in 1 scope" in warned.stderr
    assert f"omahouse deny {USER} gtk-launch" in warned.stderr
    # Warned, not refused: the rule is on disk.
    profile = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    assert {"match": "gtk-launch", "verdict": "allow"} in profile["rules"]

    # An id that names its own program says nothing at all.
    quiet = box.run("allow", USER, "chromium")
    assert quiet.returncode == 0, quiet.stderr
    assert quiet.stderr == "", quiet.stderr

    # Neither does an id with nothing open under it: with no session there is no
    # evidence, and with no evidence there is nothing to say.
    unopened = box.run("allow", USER, "minecraft-launcher")
    assert unopened.stderr == "", unopened.stderr

    # And `deny` never warns: taking something off the list needs no second
    # thoughts about what it contains.
    assert box.run("deny", USER, "gtk-launch").stderr == ""


def check_it_refuses_a_profile_for_an_administrator(box):
    """docs/design.md §1: the operator is whoever is in wheel.

    A profile for one of them is somebody fiscalising themselves by accident,
    and the person who could undo it is the person it would be imposed on.
    """
    refused = box.run("profile", "add", "root")
    assert refused.returncode == 1, refused.stderr
    assert "root is root" in refused.stderr
    assert "does not fiscalise themselves" in refused.stderr
    assert not (box.config / "profiles.json").exists()

    administrator = a_wheel_user()
    if administrator is None:
        print("test_cli.py: no wheel group on this machine, so only root was asserted")
        return
    in_wheel = box.run("profile", "add", administrator)
    assert in_wheel.returncode == 1, in_wheel.stderr
    assert "wheel" in in_wheel.stderr
    assert not (box.config / "profiles.json").exists()


def check_it_refuses_to_write_without_privilege(box):
    """/etc/omahouse belongs to root, and the refusal says so and says what to do.

    Never a stack trace and never a bare `Permission denied`: the first names
    nothing and the second names the thing that failed rather than the thing to
    do about it.
    """
    if os.geteuid() == 0:
        print("test_cli.py: running as root, so the refusal to write could not be asserted")
        return

    for args in (("profile", "add", "julia"), ("allow", "julia", "code"),
                 ("limit", "julia", "--session", "2h"),
                 ("grant", "julia", "--session", "10m")):
        refused = box.run(*args, system_roots=True)
        assert refused.returncode == 1, (args, refused.returncode)
        assert refused.stdout == "", args
        assert "needs root" in refused.stderr, args
        assert "pkexec omahouse" in refused.stderr, args
        # The line it suggests is the line that was typed.
        assert " ".join(args) in refused.stderr, args
    # Moving one root does not excuse the other: a ledger in $TMPDIR is not a
    # licence to write /etc.
    half = box.run("profile", "add", "julia", system_roots=True,
                   extra_env={"OMAHOUSE_STATE_DIR": str(box.state)})
    assert half.returncode == 1, half.stderr
    assert "needs root" in half.stderr

    # Reading is free, and stays free.
    assert box.run("profile", "list", system_roots=True).returncode == 0


def check_create_user_is_built_but_never_run_here(box):
    """plan.md: `useradd` is never exercised outside the VM.

    So the program it runs comes in by variable, and what is asserted is the
    command that would be run. Stage 7 is what puts the real one on the other
    end of it, in the nspawn box of testing.md.
    """
    script, recorded = box.fake_useradd()
    made = box.run("profile", "add", "julia", "--create-user",
                   extra_env={"OMAHOUSE_USERADD": str(script)})
    assert made.returncode == 0, made.stderr
    assert recorded.read_text().split() == ["-m", "julia"]
    assert str(script) in made.stderr
    assert json.loads((box.config / "profiles.json").read_text())["profiles"][0]["user"] \
        == "julia"

    # And no account was created on the machine that ran the suite, which is the
    # thing the injection exists to guarantee.
    try:
        pwd.getpwnam("julia")
        raise AssertionError("the suite created a real account")
    except KeyError:
        pass

    # A useradd that fails is the profile not being written.
    failing = box.root / "useradd-that-fails"
    failing.write_text("#!/bin/sh\necho 'useradd: user julia already exists' >&2\nexit 9\n")
    failing.chmod(0o755)
    box.run("profile", "remove", "julia", "--keep-account")
    refused = box.run("profile", "add", "otavio", "--create-user",
                      extra_env={"OMAHOUSE_USERADD": str(failing)})
    assert refused.returncode == 1, refused.stderr
    assert "failed (9)" in refused.stderr
    assert "already exists" in refused.stderr
    assert [p["user"] for p in
            json.loads((box.config / "profiles.json").read_text())["profiles"]] == []


def check_a_length_of_time_is_refused_rather_than_guessed(box):
    """`--limit 2h` read as two minutes is a session that closes at nine a.m.

    Refusing costs one retyped word, and the message says which word.
    """
    box.run("profile", "add", "julia")
    written = {"2h": 120, "45m": 45, "90": 90, "1h30m": 90}
    for text, minutes in written.items():
        assert box.run("limit", "julia", "--budget", f"code={text}").returncode == 0
        budget = json.loads((box.config / "profiles.json").read_text())["profiles"][0]["budgets"]
        assert budget[0]["dailyMinutes"] == minutes, text

    for bad in ("forever", "1h30", "30s", "1.5h", "0m", "25h", "-45m", "3d"):
        refused = box.run("limit", "julia", "--budget", f"code={bad}")
        assert refused.returncode == 1, (bad, refused.returncode)
        assert refused.stdout == "", bad
        assert "not a length of time" in refused.stderr, (bad, refused.stderr)
    # The budget is what it was before any of that.
    budget = json.loads((box.config / "profiles.json").read_text())["profiles"][0]["budgets"]
    assert budget[0]["dailyMinutes"] == 90

    # And an option with no value at all says what it wanted.
    empty = box.run("limit", "julia", "--session")
    assert empty.returncode == 1
    assert "--session wants a length of time" in empty.stderr


def check_the_writing_verbs_want_a_profile_that_is_there(box):
    """2 and not 1: a script has to tell `no such profile` from `bad command`."""
    for args in (("allow", "julia", "code"), ("deny", "julia", "code"),
                 ("limit", "julia", "--session", "2h"),
                 ("grant", "julia", "--session", "10m"),
                 ("profile", "enforce", "julia", "--on"),
                 ("profile", "default", "julia", "--deny")):
        missing = box.run(*args)
        assert missing.returncode == 2, (args, missing.returncode)
        assert "no profile" in missing.stderr, args
        assert "profile add julia" in missing.stderr, args

    gone = box.run("profile", "remove", "julia")
    assert gone.returncode == 2
    assert "no profile for julia" in gone.stderr

    # Two profiles for one account is a profile nobody could point at.
    box.run("profile", "add", "julia")
    twice = box.run("profile", "add", "julia")
    assert twice.returncode == 1
    assert "already has a profile" in twice.stderr
    assert len(json.loads((box.config / "profiles.json").read_text())["profiles"]) == 1

    # A switch verb wants to be told which way.
    for args in (("profile", "enforce", "julia"), ("profile", "enforce", "julia", "--on", "--off"),
                 ("profile", "default", "julia"),
                 ("profile", "default", "julia", "--allow", "--deny")):
        vague = box.run(*args)
        assert vague.returncode == 1, args
        assert "one of them" in vague.stderr, args


# -- the sites ----------------------------------------------------------------
#
# docs/design.md §11. What the file holds is proved in microseconds by
# tst_webpolicy.cpp, which needs no disk at all; what is proved here is the other
# half -- that the verbs write it, that they take it away again, and that a run
# pointed at a tree of its own never goes near the browser policy of the machine
# it is running on.
#
# That last one is not a nicety. This suite runs on the developer's own laptop,
# with the developer's own Chromium open, and a bug that wrote
# /etc/chromium/policies/managed/omahouse.json from `mise run verify` would take
# somebody's browser away in the middle of an afternoon.


def check_web_writes_a_policy_and_takes_it_away_again(box):
    """One site blocked is one file; the block taken back is no file.

    The undoing path of docs/design.md §11 that does not need `pacman -R`: a
    parental control that leaves a restriction behind after the rule is gone is
    the failure this whole slice is written against, and the smallest version of
    it is a managed policy left on the machine holding nothing.
    """
    assert box.run("profile", "add", "julia").returncode == 0
    assert box.policy() is None, "a fresh profile asks the browser for nothing"

    blocked = box.run("web", "block", "julia", "youtube.com")
    assert blocked.returncode == 0, blocked.stderr
    assert box.policy() == {"URLBlocklist": ["youtube.com"]}, box.policy()
    assert "is blocked" in blocked.stdout

    # Said once, and in the words docs/design.md §11 uses. The operator has to
    # read this before they wonder why their own browser changed.
    assert "one file for the whole machine" in blocked.stderr
    assert "including you" in blocked.stderr

    # And it is in the profile, in the shape the app half has.
    written = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    assert written["web"] == {
        "default": "allow",
        "rules": [{"match": "youtube.com", "verdict": "deny"}],
    }, written["web"]

    # 0644: Chromium reads the managed policy as whoever started the browser,
    # which is never root. A mode only root can read is a policy that does not
    # apply.
    assert oct((box.chromium / "omahouse.json").stat().st_mode)[-3:] == "644"

    back = box.run("web", "allow", "julia", "youtube.com")
    assert back.returncode == 0, back.stderr
    assert box.policy() is None, "the last block taken back has to take the file with it"
    # An allowlist with nothing blocked beside it is inert, and the line says so
    # rather than reading as a rule that is doing something.
    assert "blocks nothing on its own" in back.stdout

    # The profile keeps the decision even though the machine has no file: what
    # the operator said is a record, and the file is a consequence of it.
    written = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    assert written["web"]["rules"] == [{"match": "youtube.com", "verdict": "allow"}]


def check_web_only_listed_and_incognito(box):
    """The two switches, and what each of them puts in the file."""
    box.run("profile", "add", "julia")

    closed = box.run("web", "julia", "--only-listed")
    assert closed.returncode == 0, closed.stderr
    assert "only the listed sites open" in closed.stdout
    assert box.policy() == {"URLBlocklist": ["*"]}, box.policy()

    box.run("web", "allow", "julia", "wikipedia.org")
    assert box.policy() == {"URLBlocklist": ["*"], "URLAllowlist": ["wikipedia.org"]}

    denied = box.run("web", "incognito", "julia", "--deny")
    assert denied.returncode == 0, denied.stderr
    assert box.policy()["IncognitoModeAvailability"] == 1

    # `--allow` asks the browser for nothing rather than writing a 0: omahouse
    # being more permissive than it was asked to be -- overriding somebody
    # else's managed policy to turn incognito back on -- is a direction it never
    # takes by itself.
    allowed = box.run("web", "incognito", "julia", "--allow")
    assert allowed.returncode == 0, allowed.stderr
    assert "IncognitoModeAvailability" not in box.policy()
    assert "hide which site, not the time" in allowed.stdout

    # And the whole way back: every site opens, no rules left that block, no file.
    box.run("web", "julia", "--all-but-listed")
    assert box.policy() is None, box.policy()


def check_web_composes_profiles_that_disagree(box):
    """The most restrictive wins, and the verb says who else had a say.

    tst_webpolicy.cpp proves the arithmetic. What is proved here is that two
    profiles in one file really do land in one policy, and that the profile
    which was overruled is told rather than left to find out.
    """
    box.write_profiles({
        "schemaVersion": 1,
        "profiles": [
            {"user": "julia", "web": {"default": "allow",
                                   "rules": [{"match": "youtube.com", "verdict": "allow"}]}},
            {"user": "pedro", "web": {"default": "allow",
                                      "rules": [{"match": "youtube.com", "verdict": "deny"}]}},
        ],
    })

    overruled = box.run("web", "allow", "julia", "youtube.com")
    assert overruled.returncode == 0, overruled.stderr
    assert "pedro disagrees" in overruled.stderr, overruled.stderr
    assert "no precedence" in overruled.stderr

    # Blocked, and never also allowlisted: Chromium gives the allowlist the tie,
    # so a domain in both lists is a domain that opens.
    assert box.policy() == {"URLBlocklist": ["youtube.com"]}, box.policy()


def check_removing_the_last_profile_removes_the_policy(box):
    """`profile remove` is the other way the machine comes back to nothing."""
    box.run("profile", "add", "julia")
    box.run("web", "block", "julia", "youtube.com")
    assert box.policy() is not None

    gone = box.run("profile", "remove", "julia")
    assert gone.returncode == 0, gone.stderr
    assert box.policy() is None, "the last profile's web rules went with it"


def check_web_refuses_what_it_does_not_do(box):
    """A whole URL, a bare word, `*`, no switch, both switches, no profile."""
    box.run("profile", "add", "julia")

    for typed in ("https://youtube.com", "youtube.com/watch", "youtube com"):
        refused = box.run("web", "block", "julia", typed)
        assert refused.returncode == 1, typed
        assert "is not a domain" in refused.stderr, typed
        assert box.policy() is None, typed

    dotless = box.run("web", "block", "julia", "youtube")
    assert dotless.returncode == 1
    assert "youtube.com?" in dotless.stderr

    # `*` is a mode and not a rule, and there is one way to say it.
    star = box.run("web", "block", "julia", "*")
    assert star.returncode == 1
    assert "--only-listed" in star.stderr

    for args in (("web", "julia"), ("web", "julia", "--only-listed", "--all-but-listed"),
                 ("web", "incognito", "julia"),
                 ("web", "incognito", "julia", "--allow", "--deny")):
        vague = box.run(*args)
        assert vague.returncode == 1, args
        assert "one of them" in vague.stderr or "which user" in vague.stderr, args

    # An option that belongs to another verb is a usage error and not a word
    # quietly dropped.
    stray = box.run("web", "block", "julia", "youtube.com", "--limit", "45m")
    assert stray.returncode == 1
    assert "takes no --limit" in stray.stderr

    for args in (("web", "block", "nobody", "youtube.com"),
                 ("web", "allow", "nobody", "youtube.com"),
                 ("web", "nobody", "--only-listed"),
                 ("web", "incognito", "nobody", "--deny")):
        missing = box.run(*args)
        assert missing.returncode == 2, args
        assert "no profile" in missing.stderr, args

    assert box.policy() is None, "nothing that was refused reached the machine"


def check_the_browser_policy_of_this_machine_is_never_touched(box):
    """A run pointed at a configuration of its own leaves /etc/chromium alone.

    The mirror of the two refusals docs/design.md §7 already has -- a `close`
    that will not signal a cgroup tree that is not /sys/fs/cgroup, and a
    `terminate-user` that will not run behind a configuration that is not
    /etc/omahouse. This is the third, and it is the one that protects the
    machine this suite runs on: the policy directory is only written by a run
    that is also managing this machine's own /etc/omahouse.

    Nothing is asserted about /etc/chromium itself, because the point of the
    case is that nothing goes near it. What is asserted is the refusal, by name.
    """
    box.run("profile", "add", "julia")
    left_alone = box.run("web", "block", "julia", "youtube.com",
                         extra_env={"OMAHOUSE_CHROMIUM_POLICY_DIR":
                                    "/etc/chromium/policies/managed"})

    # The rule is still written -- the profile is the operator's decision, and
    # this run simply is not the one that carries it out.
    assert left_alone.returncode == 0, left_alone.stderr
    assert "/etc/chromium/policies/managed" in left_alone.stderr
    assert "was left alone" in left_alone.stderr
    written = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    assert written["web"]["rules"] == [{"match": "youtube.com", "verdict": "deny"}]


def check_status_says_the_policy_is_for_the_whole_machine(box):
    """`status` prints the sites, and says the reach once and without hedging.

    Over the account that ran the suite, because `status` resolves a name
    through NSS and `julia` is not on this machine. The profile is written to
    the file rather than through `profile add`, which refuses an account in
    wheel.
    """
    box.write_profiles({
        "schemaVersion": 1,
        "profiles": [{
            "user": USER, "enabled": True, "enforce": False, "default": "allow",
            "rules": [], "budgets": [],
            "web": {"default": "allow", "incognito": "deny",
                    "rules": [{"match": "youtube.com", "verdict": "deny"}]},
        }],
    })

    screen = box.run("status", USER)
    assert screen.returncode == 0, screen.stderr
    sites = below(screen.stdout, "SITES")
    assert "every site opens except the blocked ones" in sites
    assert "incognito: does not open" in sites
    assert "youtube.com" in sites
    assert "one file for the whole machine" in sites
    # Once, and not once per rule.
    assert screen.stdout.count("one file for the whole machine") == 1

    # The composed policy is in the document a script reads, and it is the
    # machine's and not this profile's half of it.
    document = json.loads(box.run("status", USER, "--json").stdout)
    assert document["webPolicy"]["wholeMachine"] is True
    assert document["webPolicy"]["contents"] == {
        "URLBlocklist": ["youtube.com"], "IncognitoModeAvailability": 1}
    assert document["profile"]["web"]["rules"] == [
        {"match": "youtube.com", "verdict": "deny"}]

    # And a profile with nothing to say about the web says so in one line
    # rather than printing an empty table, which is the same choice `No
    # budgets` makes above it.
    box.write_profiles({
        "schemaVersion": 1,
        "profiles": [{"user": USER, "rules": [], "budgets": []}],
    })
    quiet = box.run("profile", "show", USER)
    assert "has no web rules" in below(quiet.stdout, "SITES")
    assert json.loads(box.run("status", USER, "--json").stdout)["webPolicy"]["contents"] is None


# -- watch --------------------------------------------------------------------
#
# The loop of docs/design.md §5, driven the same way everything else here is: the four
# roots in a temporary directory, no root, no session of the fiscalised user, and
# `notify-send` pointed at a script that writes down what it was handed.
#
# `--once` is what makes it drivable at all -- one cycle and out, with no event
# loop behind it -- and it is also what stage 7 will run in the nspawn box of
# testing.md.


def watching_profile(enforce=False, default="deny"):
    """A profile over the fake session: two hours of it, forty-five minutes of
    Chromium, and an editor that is counted and never runs out."""
    return {
        "schemaVersion": 1,
        "profiles": [{
            "user": USER,
            "displayName": "Júlia",
            "enabled": True,
            "enforce": enforce,
            "default": default,
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
        }],
    }


def check_watch_says_when_there_is_nobody_to_watch(box):
    """No profiles is not a loop with nothing in it.

    Both spellings of it: no profiles.json at all, which is every machine before
    the first `profile add`, and a file that holds an empty list. And without
    `--once`, so that what is asserted is that it really does come back rather
    than spinning every two seconds over nobody.
    """
    nobody = box.run("watch")
    assert nobody.returncode == 0, nobody.stderr
    assert "nothing to watch" in nobody.stderr
    assert nobody.stdout == ""

    box.write_profiles({"schemaVersion": 1, "profiles": []})
    empty = box.run("watch")
    assert empty.returncode == 0, empty.stderr
    assert "holds no profiles" in empty.stderr


def check_watch_dry_run_counts_and_touches_nothing(box):
    """The mode that makes stage 6 safe to try on a development machine.

    It reads the tree, debits the tick, and prints the accounting -- and writes
    no ledger, sends no notification, and leaves the day exactly as it found it.
    """
    box.write_profiles(watching_profile())
    ran = box.run("watch", "--once", "--dry-run")
    assert ran.returncode == 0, ran.stderr
    assert "dry run" in ran.stdout

    # One tick of two seconds, once per budget with a live app matching it and
    # never once per process: the nineteen processes of the Chromium scope are
    # one app.
    assert row_for(ran.stdout, "session", after="BUDGET")[:4] \
        == ["session", "2h00m", "2s", "1h59m"]
    assert row_for(ran.stdout, "chromium", after="BUDGET")[:4] \
        == ["chromium", "45m", "2s", "44m"]

    assert not (box.state / USER).exists()
    assert box.said() == []

    # The same cycle as one document, for whatever reads a stream of them.
    document = json.loads(box.run("watch", "--once", "--dry-run", "--json").stdout)
    assert document["dryRun"] and document["tickSeconds"] == 2
    watched = document["users"][0]
    assert watched["user"] == USER and watched["session"] and not watched["wrote"]
    assert watched["debited"] == ["chromium", "code", "session"]
    # A scope with no id is counted and has no name to be counted under. It is
    # not in `apps`, because there is no word to put there, and it is reported as
    # itself rather than left out of the cycle altogether.
    assert "chromium" in watched["apps"]
    assert not any(app.startswith("tmux-spawn") for app in watched["apps"])
    assert watched["unnamedScopes"] == 1


def check_watch_counts_a_session_of_nothing_but_nameless_scopes(box):
    """The bug this fixture was written after, end to end.

    Measured on the development machine: 46 `tmux-spawn-<uuid>.scope` holding
    more than a hundred processes, and an afternoon inside them debited the two
    hour session zero seconds -- `howl: no apps, nothing on the clock`. For a
    child's profile that is the obvious way out of the house. The session budget
    is time on the machine, and a scope with processes in it is somebody using
    the machine whether or not anything can name it.
    """
    # A session that is nothing but a terminal: two scopes the parser refuses,
    # thirty-nine processes between them, and not one app with a name.
    terminal = box.root / "terminal-cgroup"
    manager = terminal / "user.slice" / f"user-{UID}.slice" / f"user@{UID}.service"
    write_cgroup(manager / "app.slice" / "app-graphical.slice"
                 / "tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope", 20, 4600)
    write_cgroup(manager / "app.slice" / "app-graphical.slice"
                 / "tmux-spawn-ef186a82-fc3d-4946-ab27-107f807a948c.scope", 19, 4700)

    box.write_profiles(watching_profile(default="allow"))
    ran = box.run("watch", "--once", "--dry-run",
                  extra_env={"OMAHOUSE_CGROUP_ROOT": str(terminal)})
    assert ran.returncode == 0, ran.stderr

    # The two seconds are on the clock, and they are two and not four: the debit
    # is once per budget, not once per scope and not once per process.
    assert row_for(ran.stdout, "session", after="BUDGET")[:4] \
        == ["session", "2h00m", "2s", "1h59m"]
    # And the budget that names an app is not billed for a scope that is not that
    # app: `*` counts everything alive, a name needs a name.
    assert row_for(ran.stdout, "chromium", after="BUDGET")[:4] \
        == ["chromium", "45m", "0m", "45m"]

    # The journal used to read `no apps, nothing on the clock` over exactly this.
    assert "no apps, nothing on the clock" not in ran.stdout
    assert "no named apps and 2 scopes it cannot name, counting session" in ran.stdout

    document = json.loads(box.run("watch", "--once", "--dry-run", "--json",
                                  extra_env={"OMAHOUSE_CGROUP_ROOT": str(terminal)}).stdout)
    watched = document["users"][0]
    assert watched["apps"] == []
    assert watched["unnamedScopes"] == 2
    assert watched["debited"] == ["session"]


def check_watch_debits_the_day_and_warns_once(box):
    """A cycle writes the day, and the mark fires once.

    Seeded five minutes from the end of a two hour session, which is a warnAt
    mark the profile carries. The second cycle debits again and says nothing:
    the ledger is where a once-only decision remembers it has fired, and without
    that a two second loop is a notification every two seconds for five minutes.
    """
    # Allowing by default, so that the only thing said is the one about time: a
    # profile is born this way, and the refusals have a case of their own.
    box.write_profiles(watching_profile(default="allow"))
    box.write_day(TODAY, {"session": 120 * 60 - 300})

    first = box.run("watch", "--once")
    assert first.returncode == 0, first.stderr
    day = box.day(TODAY)
    assert day["budgets"]["session"] == 120 * 60 - 298
    assert day["budgets"]["chromium"] == 2

    said = box.said()
    assert len(said) == 1, said
    assert said[0][0] == "5 minutes left"
    # The clock time of docs/design.md §6's example, worked out by the caller from the
    # seconds the core handed back.
    assert re.fullmatch(r"Your session runs out at \d\d:\d\d\.", said[0][1]), said

    # And both marks are in the day, which is what a restarted daemon reads.
    # The seeded ledger crossed the ten minute mark and the five in the same
    # tick, so both are written down and neither can fire later at a time that
    # would be a lie -- and one thing is said, the smaller of them, because two
    # notifications in the same second saying different numbers is worse than
    # one saying the number that matters.
    assert [(event["kind"], event["budget"], event["minutes"]) for event in day["events"]] \
        == [("warn", "session", 10), ("warn", "session", 5)]

    second = box.run("watch", "--once")
    assert second.returncode == 0, second.stderr
    assert box.day(TODAY)["budgets"]["session"] == 120 * 60 - 296
    assert box.said() == said


def check_watch_decides_the_teeth_and_never_bites_a_tree_it_was_lent(box):
    """testing.md §6, proved on the machine it is about.

    The cgroup tree here is a directory in $TMPDIR whose `cgroup.procs` hold pids
    somebody typed -- 4000, 4100, 4200 -- and those are real pids on the machine
    running this suite. So with the teeth fully on, every close is decided,
    named, and refused, and the refusal says which tree it was and which tree it
    would have had to be. A build that got this wrong would be a test suite
    killing whatever happened to be process 4000.
    """
    box.write_profiles(watching_profile(enforce=True))
    ran = box.run("watch", "--once")
    assert ran.returncode == 0, ran.stderr

    summaries = [line[0] for line in box.said()]
    assert "xdg-terminal-exec is not allowed" in summaries, summaries
    assert "org.freedesktop.Platform is not allowed" in summaries, summaries
    # The ones a rule allows are not refused, whatever else is true of them.
    assert not any(said.startswith("chromium is not") for said in summaries)
    for said in box.said():
        assert said[1] == "It is not one of the programs released for Júlia."

    # Decided, named, and not done. The unit is in the line because it is the
    # directory `cgroup.kill` would have been written in.
    assert "SIGTERM into" in ran.stderr, ran.stderr
    assert "not this machine's to signal" in ran.stderr, ran.stderr
    assert str(box.cgroup) in ran.stderr, ran.stderr

    document = json.loads(box.run("watch", "--once", "--json").stdout)
    done = document["users"][0]["done"]
    assert done, document
    for act in done:
        assert act["what"] in ("terminate", "kill", "block", "unblock", "end-session"), act
        assert not act["carriedOut"], act
        assert "/sys/fs/cgroup" in act["error"], act

    # Observing is the default of a new profile, and under it the core emits no
    # Close at all -- so there is nothing to refuse and nothing to say.
    box.write_profiles(watching_profile(enforce=False))
    observing = box.run("watch", "--once")
    assert observing.returncode == 0, observing.stderr
    assert "SIGTERM" not in observing.stderr, observing.stderr


def check_watch_writes_the_block_and_refuses_to_end_a_session_behind_it(box):
    """docs/design.md §2: `logout` is two things, and the second needs the first.

    The name goes into `blocked` -- one per line, because `pam_listfile` reads it
    -- and the session is only ended for a user who is really in that file, on a
    machine whose PAM stack really reads it. Here it is a file in $TMPDIR, so the
    block is written, the termination is refused, and the sentence says why.
    """
    box.write_profiles(watching_profile(enforce=True, default="allow"))
    # The session budget spent, with no grace, so the logout lands on this tick.
    box.write_day(TODAY, {"session": 120 * 60})
    profiles = watching_profile(enforce=True, default="allow")
    profiles["profiles"][0]["grace"] = 0
    box.write_profiles(profiles)

    ran = box.run("watch", "--once")
    assert ran.returncode == 0, ran.stderr

    blocked = box.config / "blocked"
    assert blocked.exists(), ran.stderr
    assert blocked.read_text() == f"{USER}\n"
    assert oct(blocked.stat().st_mode)[-3:] == "644"

    assert "refused at the next login" in ran.stderr, ran.stderr
    assert "loginctl terminate-user" in ran.stderr, ran.stderr
    assert "no PAM stack reads" in ran.stderr, ran.stderr

    document = json.loads(box.run("watch", "--once", "--json").stdout)
    assert document["blocked"] == [USER]
    assert document["users"][0]["blocked"]
    acts = {act["what"]: act for act in document["users"][0]["done"]}
    assert "end-session" in acts and not acts["end-session"]["carriedOut"]

    # And the name comes out on its own, with nothing having to know the file
    # exists. An operator hands over ten minutes -- docs/design.md §1, with the game
    # still running -- and the next cycle lets them back in.
    granted = box.run("grant", USER, "--session", "10m")
    assert granted.returncode == 0, granted.stderr
    after = box.run("watch", "--once")
    assert after.returncode == 0, after.stderr
    assert blocked.read_text() == ""
    assert "let back in" in after.stderr, after.stderr


def check_watch_dry_run_never_writes_the_block(box):
    """The mode that makes the loop safe to point anywhere, kept true of the half
    of it that can lock a door."""
    profiles = watching_profile(enforce=True, default="allow")
    profiles["profiles"][0]["grace"] = 0
    box.write_profiles(profiles)
    box.write_day(TODAY, {"session": 120 * 60})

    ran = box.run("watch", "--once", "--dry-run")
    assert ran.returncode == 0, ran.stderr
    assert not (box.config / "blocked").exists()
    assert "refused at the next login" in ran.stdout, ran.stdout
    assert "not done: dry run" in ran.stdout, ran.stdout


def check_watch_refuses_what_it_does_not_do(box):
    box.write_profiles(watching_profile())

    for bad in ("0", "-1", "abc", "2.5", "5000"):
        wrong = box.run("watch", "--once", "--interval", bad)
        assert wrong.returncode == 1, bad
        assert "--interval wants whole seconds" in wrong.stderr, bad

    # It watches everybody with a profile. A user is what `status` takes.
    one = box.run("watch", USER)
    assert one.returncode == 1
    assert "takes no user" in one.stderr

    # The ledger is the one thing it writes, so it is the only privilege it asks
    # for -- and it asks before it reads anything, so the answer is about
    # privilege rather than about a file that was not there.
    system = box.run("watch", "--once", system_roots=True)
    assert system.returncode == 1
    assert "needs root" in system.stderr
    assert "pkexec omahouse watch --once" in system.stderr

    # A dry run writes nothing, so it needs nothing.
    dry = box.run("watch", "--once", "--dry-run", system_roots=True)
    assert "needs root" not in dry.stderr


def check_watch_counts_a_faster_tick(box):
    """The interval and the debit are one number.

    A loop that wakes every five seconds and debits two is a day that never ends,
    and one that wakes every two and debits five is a day that ends at teatime.
    """
    box.write_profiles(watching_profile())
    assert box.run("watch", "--once", "--interval", "5").returncode == 0
    assert box.day(TODAY)["budgets"]["session"] == 5


# -- time per site ------------------------------------------------------------
#
# docs/design.md §5.2, end to end, with no browser on the machine and no root.
# The extension and the native messaging host are both driven here: the host is
# `omahouse meter`, spoken to over a pipe the way Chromium speaks to it, and the
# file it leaves is the same file `watch` then reads.


def check_the_meter_writes_what_the_browser_told_it(box):
    """The host's half: frames in on stdin, one line out per frame.

    It is stupid on purpose. `.temp/spike-extension.md` §1 measured this process
    running as the child with /var/lib/omahouse root's, so it has no path to the
    ledger and is given none -- it appends to a file in her own runtime directory
    and stops.
    """
    def frame(document):
        payload = json.dumps(document).encode()
        return struct.pack("=I", len(payload)) + payload

    said = (frame({"site": "www.youtube.com"})
            # Revalidated on this side and not trusted: a compromised extension
            # must not be able to push a whole URL through by putting one in the
            # field. Anything that is not a domain is written as `-`.
            + frame({"site": "youtube.com/watch?v=dQw4w9WgXcQ"})
            + frame({"site": "-"})
            + frame({"nothing": "of ours"})
            + frame({"site": "en.wikipedia.org"}))
    ran = subprocess.run([str(CLI), "meter", "chrome-extension://whatever/"],
                         input=said, cwd=str(ROOT),
                         env={**os.environ, "OMAHOUSE_RUNTIME_ROOT": str(box.runtime)},
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    assert ran.returncode == 0, ran.stderr

    lines = box.focus_file().read_text().splitlines()
    sites = [line.split(" ", 1)[1] for line in lines]
    # Four lines for five frames: the one that was not a message of ours is
    # dropped rather than written as anything.
    assert sites == ["youtube.com", "-", "-", "wikipedia.org"], lines
    # And it says nothing back. The extension does not read, and a host that
    # chattered would be a host with a protocol to keep.
    assert ran.stdout == b"", ran.stdout

    # Typed by a person rather than started by a browser. Native messaging is
    # always a pipe (`.temp/spike-extension.md` §1 measured `STDIN_ISATTY=False`),
    # so a terminal here is somebody wondering what the verb does -- and what it
    # would do is sit there silently for ever. A real pty, because the whole
    # refusal is about what stdin is.
    parent, child = pty.openpty()
    try:
        told = subprocess.run([str(CLI), "meter"], stdin=child, cwd=str(ROOT),
                              env={**os.environ, "OMAHOUSE_RUNTIME_ROOT": str(box.runtime)},
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=30)
    finally:
        os.close(parent)
        os.close(child)
    assert told.returncode == 1, told.returncode
    assert "native messaging host" in told.stderr, told.stderr


def check_watch_counts_a_site_only_while_somebody_is_there(box):
    """The crossing, and the case that decides whether this is worth having.

    A browser is a witness to what is on the screen and a proven liar about
    whether anybody is looking at it: `.temp/spike-extension.md` §5 asked
    `chrome.idle` ninety-four times through half an hour of an empty room, with
    the monitor off for twenty-five minutes of it, and got `active` every time.
    So the name comes from the browser and the presence comes from the kernel,
    and a second is billed only where the two agree.
    """
    box.write_profiles(watching_profile())
    box.browsing("www.youtube.com")

    lit = box.run("watch", "--once")
    assert lit.returncode == 0, lit.stderr
    day = box.day(TODAY)
    assert day["sites"] == {"youtube.com": 2}, day
    # The journal says it on the same line as the counting.
    assert "youtube.com" in lit.stderr, lit.stderr

    # The screen goes dark with the same tab in front, and the browser goes on
    # saying so, because it does not know either.
    box.screen("connected", "Off")
    dark = box.run("watch", "--once")
    assert dark.returncode == 0, dark.stderr
    assert "youtube.com not counted" in dark.stderr, dark.stderr

    day = box.day(TODAY)
    assert day["sites"] == {"youtube.com": 2}, day
    # And the app half is untouched by any of it. docs/design.md §5 bills running
    # time, and a screen going dark does not change that.
    assert day["budgets"]["chromium"] == 4, day

    document = json.loads(box.run("watch", "--once", "--json").stdout)
    assert document["users"][0]["site"]["now"] == "youtube.com"
    assert document["users"][0]["site"]["counted"] is False


def check_watch_bills_nothing_for_a_focus_file_that_is_wrong(box):
    """The file is the person's own, and every way it can be wrong is one answer.

    She can delete it, fill it with rubbish, date it into the future or leave it
    to go stale. All of them come back as nothing billed, and none of them stops
    the day being counted -- which is the half that makes evading this pointless.
    She wins anonymity, not minutes.
    """
    box.write_profiles(watching_profile())
    now = int(time.time())
    wrong = {
        "deleted": None,
        "rubbish": b"nonsense\n",
        "a whole URL": f"{now} youtube.com/watch?v=x\n".encode(),
        "a name that is a sentence": f"{now} your time is up\n".encode(),
        "an escape in the name": (f"{now} you".encode() + b"\x1b[2J" + b"tube.com\n"),
        "stale": f"{now - 600} youtube.com\n".encode(),
        "dated ahead": f"{now + 600} youtube.com\n".encode(),
        "half written": f"{now} youtube.co".encode(),
        "nothing in front": f"{now} -\n".encode(),
    }
    spent = 0
    for what, raw in wrong.items():
        if raw is None:
            box.stopped_browsing()
        else:
            box.browsing(None, raw=raw)
        ran = box.run("watch", "--once")
        assert ran.returncode == 0, (what, ran.stderr)
        spent += 2
        day = box.day(TODAY)
        assert "sites" not in day, (what, day)
        # Counted throughout, whatever the file said.
        assert day["budgets"]["session"] == spent, (what, day)


def check_limit_writes_a_budget_about_a_site(box):
    """`--site` beside `--budget`, and the same noun underneath.

    What has to come out of the file is a budget with `kind: "site"` on it and
    nothing else new: the id, the match, the daily minutes and the action are the
    fields an app budget already had. `kind` is there because there is no shape
    that tells `org.freedesktop.Platform` from `youtube.com`.
    """
    box.run("profile", "add", "julia", "--name", "Júlia")
    written = box.run("limit", "julia", "--site", "youtube.com=30m")
    assert written.returncode == 0, written.stderr
    assert "stops opening" in written.stdout, written.stdout
    # Said once, where somebody is deciding it, and on stderr where every other
    # note is: the browser's policy is one file for the whole machine.
    assert "whole machine" in written.stderr, written.stderr

    profile = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    budgets = {one["id"]: one for one in profile["budgets"]}
    assert budgets["youtube.com"]["kind"] == "site", budgets
    assert budgets["youtube.com"]["match"] == "youtube.com", budgets
    assert budgets["youtube.com"]["dailyMinutes"] == 30, budgets
    assert budgets["youtube.com"]["onExhausted"] == "block", budgets

    # An app budget beside it, and it says nothing new about its kind: a
    # `"kind": "app"` on every budget would rewrite every profiles.json there is.
    box.run("limit", "julia", "--budget", "chromium=45m")
    profile = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    budgets = {one["id"]: one for one in profile["budgets"]}
    assert "kind" not in budgets["chromium"], budgets

    # A domain read the way `web block` reads one, by the same routine, so that a
    # site cannot be named one way here and another way there.
    box.run("limit", "julia", "--site", "WWW.Reddit.com=10m")
    profile = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    assert any(one["id"] == "www.reddit.com" for one in profile["budgets"]), profile


def check_limit_refuses_what_it_cannot_do_about_a_site(box):
    """The refusals, and the one that matters is the id that already means the
    other thing."""
    box.run("profile", "add", "julia", "--name", "Júlia")

    both = box.run("limit", "julia", "--session", "2h", "--site", "youtube.com=30m")
    assert both.returncode != 0
    assert "One of them" in both.stderr, both.stderr

    url = box.run("limit", "julia", "--site", "https://youtube.com/watch=30m")
    assert url.returncode != 0
    assert "not a domain" in url.stderr, url.stderr

    nodot = box.run("limit", "julia", "--site", "youtube=30m")
    assert nodot.returncode != 0
    assert "no dot in it" in nodot.stderr, nodot.stderr

    star = box.run("limit", "julia", "--site", "*=30m")
    assert star.returncode != 0, star.stdout

    # One id, one thing. There is no shape that tells an app id from a domain, so
    # the same id meaning both would be two rows of the report that are one row.
    box.run("limit", "julia", "--site", "youtube.com=30m")
    clash = box.run("limit", "julia", "--budget", "youtube.com=45m")
    assert clash.returncode != 0
    assert "about a site" in clash.stderr, clash.stderr


def check_a_site_that_ran_out_is_blocked_and_comes_back_on_its_own(box):
    """The whole of the site budget through the machine, and back again.

    It is spent by the site in front crossed with presence, it warns, it blocks,
    and it comes back -- by the turn of the day and by a grant -- with nothing
    on the machine having been asked to undo anything. That last half is the one
    worth having: it is `/etc/omahouse/blocked` of docs/design.md §2, said about
    a domain.
    """
    document = watching_profile(enforce=True, default="allow")
    document["profiles"][0]["grace"] = 0
    document["profiles"][0]["budgets"].append(
        {"id": "youtube.com", "match": "youtube.com", "kind": "site",
         "dailyMinutes": 1, "onExhausted": "block"})
    box.write_profiles(document)
    box.write_day(TODAY, {"youtube.com": 56})
    box.browsing("www.youtube.com")

    # Two seconds left: counted, warned about, and opening.
    left = box.run("watch", "--once")
    assert left.returncode == 0, left.stderr
    assert box.day(TODAY)["budgets"]["youtube.com"] == 58, box.day(TODAY)
    assert box.policy() is None, box.policy()
    assert any("youtube.com" in " ".join(one) for one in box.said()), box.said()

    # And out. The domain lands in the browser's own file, under the key
    # Chromium reads.
    out = box.run("watch", "--once")
    assert out.returncode == 0, out.stderr
    assert box.policy() == {"URLBlocklist": ["youtube.com"]}, box.policy()
    assert "block-site" in out.stderr or "stopped opening" in out.stderr, out.stderr

    # An operator hands over ten minutes with the tab still open, and the site
    # opens again on the next cycle. Nothing was asked to unblock it.
    granted = box.run("grant", USER, "--budget", "youtube.com=10m")
    assert granted.returncode == 0, granted.stderr
    # `grant` writes the day's own file and nothing else. The site opens again on
    # the next cycle, and that is the mechanism rather than an omission: there is
    # one place that works out what is blocked right now, it is the loop, and a
    # verb that reached into the browser's policy itself would be a second
    # answer to the same question -- which is exactly what `blocked` in
    # docs/design.md §2 refuses to have.
    back = box.run("watch", "--once")
    assert back.returncode == 0, back.stderr
    assert box.policy() is None, box.policy()
    assert "unblock-site" in back.stderr or "opens again" in back.stderr, back.stderr

    # And the turn of the day, which is the path that needs nobody at all. The
    # ledger of a day that has run out is left where it is and `watch` is asked
    # about tomorrow: the balance is a new file, so no budget is out, so the
    # policy is removed rather than emptied.
    ledger = box.state / USER / f"{TODAY.isoformat()}.json"
    yesterday = box.state / USER / f"{(TODAY - timedelta(days=1)).isoformat()}.json"
    spent = json.loads(ledger.read_text())
    spent["grants"] = []
    ledger.write_text(json.dumps(spent))
    shut = box.run("watch", "--once")
    assert shut.returncode == 0, shut.stderr
    assert box.policy() == {"URLBlocklist": ["youtube.com"]}, box.policy()
    # The same day's file, moved to yesterday: today has nothing spent on it.
    yesterday.write_text(ledger.read_text())
    ledger.unlink()
    tomorrow = box.run("watch", "--once")
    assert tomorrow.returncode == 0, tomorrow.stderr
    assert box.policy() is None, box.policy()


def check_a_site_budget_composes_with_the_web_rules(box):
    """One file, and the clock has the last word in it.

    A profile that allows a site is allowing it in general and not for the
    thirty-first minute, so a site that has run out is blocked even where a rule
    lets it through -- and it is never also allowlisted, because Chromium gives
    the allowlist the tie.
    """
    document = watching_profile(enforce=True, default="allow")
    document["profiles"][0]["grace"] = 0
    document["profiles"][0]["web"] = {
        "default": "deny",
        "rules": [{"match": "youtube.com", "verdict": "allow"},
                  {"match": "wikipedia.org", "verdict": "allow"}],
    }
    document["profiles"][0]["budgets"].append(
        {"id": "youtube.com", "match": "youtube.com", "kind": "site",
         "dailyMinutes": 1, "onExhausted": "block"})
    box.write_profiles(document)
    box.write_day(TODAY, {"youtube.com": 60})
    box.browsing("www.youtube.com")

    ran = box.run("watch", "--once")
    assert ran.returncode == 0, ran.stderr
    policy = box.policy()
    assert "youtube.com" in policy["URLBlocklist"], policy
    assert "youtube.com" not in policy.get("URLAllowlist", []), policy
    assert "wikipedia.org" in policy["URLAllowlist"], policy
    assert "*" in policy["URLBlocklist"], policy

    # And `status` says the same thing, out of the composed policy rather than
    # out of one profile's rules.
    said = box.run("status", USER)
    assert "youtube.com" in said.stdout, said.stdout


def check_status_and_report_show_the_time_per_site(box):
    """It appears where the budgets and the presence do, and says it has no teeth.

    Somebody reading a table of sites beside a table of budgets will assume the
    first one can take something away. It cannot: there is no site budget, no
    warning and no block, and both screens say so.
    """
    box.write_profiles(watching_profile())
    box.browsing("www.youtube.com")
    box.run("watch", "--once")
    box.browsing("en.wikipedia.org")
    box.run("watch", "--once")
    box.run("watch", "--once")

    live = box.run("status", USER)
    assert live.returncode == 0, live.stderr
    assert "TIME PER SITE" in live.stdout, live.stdout
    assert "wikipedia.org is in the front tab, and it is being counted" in live.stdout
    assert "never billed to a" in live.stdout, live.stdout
    assert "4m" not in below(live.stdout, "TIME PER SITE"), live.stdout

    document = json.loads(box.run("status", USER, "--json").stdout)
    assert document["sites"] == {
        "now": "wikipedia.org", "counted": True, "readable": True,
        "today": {"wikipedia.org": 4, "youtube.com": 2},
    }, document["sites"]

    reported = box.run("report", USER)
    assert "TIME PER SITE" in reported.stdout, reported.stdout
    assert row_for(reported.stdout, "youtube.com") == ["youtube.com", "2s"], reported.stdout
    assert row_for(reported.stdout, "wikipedia.org") == ["wikipedia.org", "4s"]

    # And a browser that is not open at all says so rather than saying nothing.
    box.stopped_browsing()
    quiet = box.run("status", USER)
    assert "Nothing is being reported right now" in quiet.stdout, quiet.stdout
    assert json.loads(box.run("status", USER, "--json").stdout)["sites"]["now"] is None


# -- the promise of the stage -------------------------------------------------

def check_the_reading_verbs_write_nothing(box):
    """`status`, `report` and `profile show` read. None of them touches a file.

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
        check_status_says_what_the_default_verdict_does_to_a_nameless_scope,
        check_status_about_nobody_in_particular,
        check_status_refuses_an_account_that_is_not_there,
        check_status_of_a_user_who_is_not_logged_in,
        check_status_says_what_a_scope_really_holds,
        check_status_with_a_profile,
        check_status_json,
        check_status_says_who_is_in_front_of_the_machine,
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
        check_a_profile_from_nothing_to_read_back,
        check_grant_writes_the_days_ledger,
        check_allow_warns_about_what_is_really_inside,
        check_it_refuses_a_profile_for_an_administrator,
        check_it_refuses_to_write_without_privilege,
        check_create_user_is_built_but_never_run_here,
        check_a_length_of_time_is_refused_rather_than_guessed,
        check_the_writing_verbs_want_a_profile_that_is_there,
        check_web_writes_a_policy_and_takes_it_away_again,
        check_web_only_listed_and_incognito,
        check_web_composes_profiles_that_disagree,
        check_removing_the_last_profile_removes_the_policy,
        check_web_refuses_what_it_does_not_do,
        check_the_browser_policy_of_this_machine_is_never_touched,
        check_status_says_the_policy_is_for_the_whole_machine,
        check_watch_says_when_there_is_nobody_to_watch,
        check_watch_dry_run_counts_and_touches_nothing,
        check_watch_counts_a_session_of_nothing_but_nameless_scopes,
        check_watch_debits_the_day_and_warns_once,
        check_watch_decides_the_teeth_and_never_bites_a_tree_it_was_lent,
        check_watch_writes_the_block_and_refuses_to_end_a_session_behind_it,
        check_watch_dry_run_never_writes_the_block,
        check_watch_refuses_what_it_does_not_do,
        check_watch_counts_a_faster_tick,
        check_the_meter_writes_what_the_browser_told_it,
        check_watch_counts_a_site_only_while_somebody_is_there,
        check_watch_bills_nothing_for_a_focus_file_that_is_wrong,
        check_limit_writes_a_budget_about_a_site,
        check_limit_refuses_what_it_cannot_do_about_a_site,
        check_a_site_that_ran_out_is_blocked_and_comes_back_on_its_own,
        check_a_site_budget_composes_with_the_web_rules,
        check_status_and_report_show_the_time_per_site,
        check_watch_writes_presence_beside_the_budgets_and_never_into_them,
        check_the_reading_verbs_write_nothing,
    ]
    for case in cases:
        # A machine of its own per case: a profiles.json one case wrote is a
        # profiles.json the next one would have to know about.
        with tempfile.TemporaryDirectory(prefix="omahouse-cli-") as directory:
            case(Box(directory))
    print(f"test_cli.py: {len(cases)} cases passed")


if __name__ == "__main__":
    main()
