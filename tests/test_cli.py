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

import base64
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
from fnmatch import fnmatch
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("OMAHOUSE_CLI", ROOT / "build/bin/omahouse"))

# A real account, because `status` resolves a name to a uid through NSS and an
# invented name is exit 2 by design. Whoever runs the suite is the one account
# every machine that runs it is guaranteed to have.
USER = pwd.getpwuid(os.getuid()).pw_name
UID = os.getuid()
TODAY = date.today()


def anybody():
    """The account name a profile uses to mean anybody without one of their own.

    Spelled once here so a case reads as being about the idea rather than about
    a punctuation mark, and so the day it changes there is one line to change.
    """
    return "*"

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
        "schemaVersion": 2,
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
                    {"id": "session", "match": ["*"], "dailyMinutes": 120,
                     "onExhausted": "logout"},
                    {"id": "chromium", "match": ["chromium"], "dailyMinutes": 45,
                     "onExhausted": "close"},
                    {"id": "code", "match": ["code"]},
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

    def run(self, *args, extra_env=None, system_roots=False, stdin=None):
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
            # Never the terminal the suite was started from. `collect` is the
            # one verb that reads a document from the pipe, so it gets one when
            # a case hands it text and the closed door otherwise.
            input=stdin,
            stdin=None if stdin is not None else subprocess.DEVNULL,
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



def check_the_help_and_the_declaration_name_the_same_verbs(box):
    """`--help`, omahouse.usage.kdl and docs/cli.md offer the same commands.

    Three copies of one interface, and only two of them are held together:
    `usage-check.sh` regenerates docs/cli.md from the declaration and refuses a
    commit where they differ. The screen a person actually reads is the third
    copy, and it is written by hand in `usage()` -- so a verb can be declared,
    documented, implemented and never once offered, which is exactly what
    `machine token` was when this check was written.

    The top level is compared as a set, in both directions: a verb on the screen
    and not in the declaration is as wrong as the reverse. Subcommands are only
    checked for presence, because the screen writes them the way a person reads
    them -- `allocation init|enroll|apply|plan|show` is one line for five verbs
    -- and a parser that understood that line would be a second thing to keep
    right. Presence still catches the failure that matters: a verb nobody can
    find.
    """
    helped = box.run("--help")
    assert helped.returncode == 0, helped.stderr

    # Down to `Globals:`, because `Files:` below it is a column of program names
    # -- loginctl, notify-send, systemd-run -- indented exactly like a verb.
    screen = helped.stdout.split("\nGlobals:", 1)[0]
    offered = set(re.findall(r"(?m)^  ([a-z][a-z-]*)", screen))

    declaration = (ROOT / "omahouse.usage.kdl").read_text()
    declared = set(re.findall(r'(?m)^cmd "([^"]+)"', declaration))

    generated = (ROOT / "docs" / "cli.md").read_text()
    documented = {heading for heading in
                  re.findall(r"(?m)^## `omahouse ([^`]+)`$", generated)
                  if " " not in heading}

    # Every one of the three is derived by a regular expression, and a regular
    # expression that has stopped matching produces an empty set that equals
    # nothing and passes everything.
    for label, names in (("the screen", offered), ("the declaration", declared),
                         ("docs/cli.md", documented)):
        assert names, f"{label} named no commands at all, so nothing below is checked"
    assert "machine" in offered, sorted(offered)

    assert offered == declared, {"only on the screen": sorted(offered - declared),
                                 "only declared": sorted(declared - offered)}
    assert declared == documented, {"only declared": sorted(declared - documented),
                                    "only in docs": sorted(documented - declared)}

    # And every subcommand is somewhere on the screen. `machine token` was
    # declared, documented and implemented while `--help` had never heard of it.
    for path in re.findall(r"(?m)^## `omahouse ([a-z]+ [a-z]+)`$", generated):
        parent, leaf = path.split()
        assert re.search(rf"(?m)^  {parent}\b.*\b{leaf}\b", screen), \
            f"`{path}` is documented and `--help` never names it"

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
        ["session", "2h10m", "1h10m", "59m", "daily", "logs out"]
    # 45m of 45m: out, and what happens when it is.
    assert row_for(shown.stdout, "chromium", after="BUDGET") == \
        ["chromium", "45m", "45m", "0m", "daily", "closes"]
    # A budget with no dailyMinutes counts and never runs out.
    assert row_for(shown.stdout, "code", after="BUDGET") == \
        ["code", "—", "0m", "—", "daily", "never runs out"]


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

    The browser spike measured a browser answering `active` for
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
    (box.config / "profiles.json").write_text('{"schemaVersion": 2, "profiles": [{}]}')
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
    assert row_for(shown.stdout, "session") == ["session", "*", "2h00m", "daily", "logs out"]
    assert row_for(shown.stdout, "code") == ["code", "code", "—", "daily", "never runs out"]

    # Wrapped the way profiles.json wraps, and not the bare profile: these are
    # the bytes another machine takes in, and the envelope is what carries the
    # schema version with them.
    document = json.loads(box.run("--json", "profile", "show", USER).stdout)
    assert document["schemaVersion"] == 2, document
    assert len(document["profiles"]) == 1, document
    assert document["profiles"][0]["user"] == USER
    assert document["profiles"][0]["budgets"][0]["id"] == "session"

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
    # The profile file's own number, which moved when a profile could mean
    # anybody. The ledger's did not: a machine in the middle of a day keeps it.
    assert written["schemaVersion"] == 2
    fresh = dict(written["profiles"][0])
    # Who wrote it down and when, lifted out and checked separately: the whole
    # of the rest is fixed and worth comparing as one object, and these two are
    # a clock and an account name.
    stamped_at = fresh.pop("writtenAt")
    stamped_by = fresh.pop("writtenBy")
    assert stamped_at.startswith(TODAY.isoformat()), stamped_at
    assert stamped_by, stamped_by
    assert fresh == {
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
        {"id": "chromium", "match": ["chromium"], "dailyMinutes": 45, "onExhausted": "close"},
        {"id": "session", "match": ["*"], "dailyMinutes": 120, "onExhausted": "logout"},
        {"id": "code", "match": ["code"], "dailyMinutes": 90, "onExhausted": "close"},
    ]

    # And it reads back through the verbs that were written before it existed.
    shown = box.run("profile", "show", "julia")
    assert shown.returncode == 0, shown.stderr
    assert row_for(shown.stdout, "session") == ["session", "*", "2h00m", "daily", "logs out"]
    assert row_for(shown.stdout, "chromium") == ["chromium", "chromium", "45m", "daily", "closes"]
    assert json.loads(box.run("--json", "profile", "show", "julia").stdout)["profiles"] \
        == [profile]
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
    assert edited["budgets"][0] == {"id": "chromium", "match": ["chromium"],
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


def check_a_pot_survives_a_day_nobody_has_written_yet(box):
    """A budget that never resets, met on a morning before the daemon has ticked.

    The running total of a pot is in the last file that touched it, and the name
    of a ledger is its date, so the first look at a new day finds nothing. Every
    verb that reads today had to be taught to look back, and `grant` had to be
    taught first: it reads today, appends, and writes -- so a `grant` at nine
    o'clock on a day with no file yet wrote a day with no pot in it, and the
    pot's whole history was gone with one hand-over of ten minutes.
    """
    document = profile_document()
    document["profiles"][0]["budgets"] = [
        {"id": "pot", "match": ["chromium"], "dailyMinutes": 120,
         "resets": "never", "onExhausted": "close"},
    ]
    box.write_profiles(document)

    yesterday = TODAY - timedelta(days=1)
    directory = box.state / USER
    directory.mkdir(parents=True, exist_ok=True)
    (directory / f"{yesterday.isoformat()}.json").write_text(json.dumps({
        "schemaVersion": 1,
        "user": USER,
        "date": yesterday.isoformat(),
        "budgets": {},
        "kept": {"pot": 3600},
        "grants": [{"at": f"{yesterday.isoformat()}T21:00:00-03:00", "by": "howl",
                    "budget": "pot", "minutes": 30}],
        "events": [],
    }))
    assert not (directory / f"{TODAY.isoformat()}.json").exists()

    # An hour spent and half an hour handed over: two and a half hours of pot,
    # one hour of it gone. Read before anything has written today.
    shown = box.run("status", USER)
    assert shown.returncode == 0, shown.stderr
    pot = row_for(shown.stdout, "pot")
    assert pot is not None, shown.stdout
    assert "1h30m" in " ".join(pot), pot

    # What this computer reports to the house is the same morning. `day` is
    # what a household collects and plans from, and a pot it reported as empty
    # would have every other machine told the pot was untouched -- and then
    # refused the next statement for consumption moving backwards, every cycle,
    # until somebody spent something here. A machine that is off at midnight
    # and idle all morning is the ordinary case, not a corner.
    reported = box.run("--json", "day", USER)
    assert reported.returncode == 0, reported.stderr
    told = json.loads(reported.stdout)
    assert told["kept"] == {"pot": 3600}, told
    assert told["keptGranted"] == {"pot": 1800}, told
    assert told["date"] == TODAY.isoformat(), told
    # And not a day in the past: `report` reads each day as it was written.
    absent = box.run("day", USER, "--date", (TODAY - timedelta(days=2)).isoformat())
    assert absent.returncode == 2, absent.stdout

    # And handing over ten more does not erase what came before it.
    given = box.run("grant", USER, "--budget", "pot=10m")
    assert given.returncode == 0, given.stderr
    written = json.loads((directory / f"{TODAY.isoformat()}.json").read_text())
    assert written["kept"] == {"pot": 3600}, written
    assert written["keptGranted"] == {"pot": 1800}, written
    assert [g["minutes"] for g in written["grants"]] == [10]

    # `leave` answers how much remains *today*, and a pot has no today. Refused
    # where it is typed rather than left to do one of the two wrong things:
    # expire at midnight and undo itself, or survive and recharge the pot every
    # morning.
    refused = box.run("leave", USER, "--budget", "pot=10m")
    assert refused.returncode == 1, refused.stdout
    assert "never resets" in refused.stderr, refused.stderr
    assert "omahouse grant" in refused.stderr, refused.stderr

    # Which is what the answer says: two and a half hours, plus ten, less the
    # hour already spent.
    assert "1h40m left today" in given.stdout, given.stdout


def check_a_budget_can_be_asked_never_to_reset(box):
    """The verb that writes a pot, and the three things it must not do.

    `resets: never` was read and honoured by the engine for a day before
    anything could write it, so a pot arrived only in a file somebody edited by
    hand or a manager pushed. It is an option on `limit`, because `limit` is
    where a budget's shape is written and every one of its three shapes can be
    a pot. Absent means daily and is not written into the file; a budget that
    is already there keeps what it had, the way it keeps what it does when it
    runs out; and the profile for anybody is refused a pot where it is typed
    and not only where the file is read.
    """
    # Seeded and not written through `profile add`: whoever runs this suite
    # is an administrator, and the verb refuses to fiscalise one by accident.
    # A profile with no budgets, so that every budget below is one this case
    # wrote.
    document = profile_document()
    document["profiles"][0]["budgets"] = []
    box.write_profiles(document)

    def profile():
        return json.loads((box.config / "profiles.json").read_text())["profiles"][0]

    pot = box.run("limit", USER, "--session", "2h", "--resets", "never")
    assert pot.returncode == 0, pot.stderr
    assert "never resets" in pot.stdout, pot.stdout
    assert "a day" not in pot.stdout, pot.stdout
    assert profile()["budgets"] == [
        {"id": "session", "match": ["*"], "dailyMinutes": 120, "resets": "never",
         "onExhausted": "logout"},
    ], profile()
    # Stamped as every other write is: who, and when.
    assert profile()["writtenBy"] == USER, profile()
    assert profile()["writtenAt"].startswith(TODAY.isoformat()), profile()

    # Absent is daily, and daily is not written into the file.
    daily = box.run("limit", USER, "--budget", "code=1h")
    assert daily.returncode == 0, daily.stderr
    assert "a day" in daily.stdout, daily.stdout
    assert profile()["budgets"][1] == \
        {"id": "code", "match": ["code"], "dailyMinutes": 60, "onExhausted": "close"}, profile()

    # A new number keeps the pot a pot. Whether a budget resets is the same
    # kind of decision as what it does when it runs out, and a new limit is not
    # a reason to take either back.
    again = box.run("limit", USER, "--session", "3h")
    assert again.returncode == 0, again.stderr
    assert "never resets" in again.stdout, again.stdout
    assert profile()["budgets"][0]["resets"] == "never", profile()
    assert profile()["budgets"][0]["dailyMinutes"] == 180, profile()

    # A site can be a pot too: the same noun with the other selector.
    site = box.run("limit", USER, "--site", "youtube.com=30m", "--resets", "never")
    assert site.returncode == 0, site.stderr
    assert profile()["budgets"][2]["resets"] == "never", profile()

    # `profile show` and `status` both say which, on every row.
    shown = box.run("profile", "show", USER)
    assert row_for(shown.stdout, "session") == \
        ["session", "*", "3h00m", "never", "logs out"], shown.stdout
    assert row_for(shown.stdout, "code") == \
        ["code", "code", "1h00m", "daily", "closes"], shown.stdout
    live = box.run("status", USER)
    assert live.returncode == 0, live.stderr
    session = row_for(live.stdout, "session", after="BUDGET")
    assert session[4:] == ["never", "logs out"], live.stdout
    assert row_for(live.stdout, "code", after="BUDGET")[4:] == ["daily", "closes"], live.stdout
    document = json.loads(box.run("--json", "status", USER).stdout)
    assert {b["id"]: b["resets"] for b in document["budgets"]} == \
        {"session": "never", "code": "daily", "youtube.com": "never"}, document

    # Back to daily, said in so many words, and the word leaves the file.
    back = box.run("limit", USER, "--session", "2h", "--resets", "daily")
    assert back.returncode == 0, back.stderr
    assert "a day" in back.stdout, back.stdout
    assert "resets" not in profile()["budgets"][0], profile()

    # Anything else is refused and told what the two words are.
    weekly = box.run("limit", USER, "--session", "2h", "--resets", "weekly")
    assert weekly.returncode == 1, weekly.stdout
    assert "daily or never" in weekly.stderr, weekly.stderr

    # And it belongs to `limit` alone. `allow --limit` is sugar for the common
    # case, and a pot is not it.
    sugar = box.run("allow", USER, "chromium", "--limit", "45m", "--resets", "never")
    assert sugar.returncode == 1, sugar.stdout
    assert "takes no --resets" in sugar.stderr, sugar.stderr

    # The profile for anybody is shared, and a pot on a shared login is emptied
    # once by the first person and never refilled. Refused where it is typed,
    # naming the cause, with the file left exactly as it was.
    assert box.run("profile", "add", anybody(), "--name", "Whoever sits here").returncode == 0
    before = (box.config / "profiles.json").read_text()
    refused = box.run("limit", anybody(), "--session", "2h", "--resets", "never")
    assert refused.returncode == 1, refused.stdout
    assert "never resets" in refused.stderr, refused.stderr
    assert "shared" in refused.stderr, refused.stderr
    assert (box.config / "profiles.json").read_text() == before
    # A daily one is what a shared login takes, and it is not refused.
    assert box.run("limit", anybody(), "--session", "2h").returncode == 0


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
    box.write_profiles({"schemaVersion": 2, "profiles": [{"user": USER}]})

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


def check_one_budget_covers_a_browsers_two_ids(box):
    """A single Chromium window produces two scopes, so one clock has two names.

    `chromium` holds the child processes and `org.chromium.Chromium` holds the
    one that owns the window. Two commands with `--limit 45m` in each is two
    clocks of 45 minutes that happen to agree, and whoever writes one of them
    leaves half the browser with no limit at all -- which is a thing nothing in
    the file says out loud. One command, one budget, both names.
    """
    box.write_profiles({"schemaVersion": 2, "profiles": [{"user": USER}]})

    done = box.run("allow", USER, "chromium", "org.chromium.Chromium", "--limit", "45m")
    assert done.returncode == 0, done.stderr
    assert "chromium and org.chromium.Chromium allowed" in done.stdout, done.stdout
    assert "45m a day between them" in done.stdout, done.stdout

    profile = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    # A rule each, because the rules are read one at a time and either can be
    # taken back on its own.
    assert {"match": "chromium", "verdict": "allow"} in profile["rules"]
    assert {"match": "org.chromium.Chromium", "verdict": "allow"} in profile["rules"]
    # And one clock, holding both names.
    assert len(profile["budgets"]) == 1, profile["budgets"]
    budget = profile["budgets"][0]
    assert budget["id"] == "chromium", budget
    assert budget["match"] == ["chromium", "org.chromium.Chromium"], budget
    assert budget["dailyMinutes"] == 45, budget

    # `status` says the same, in the same shape the file uses.
    shown = json.loads(box.run("status", USER, "--json").stdout)
    budgets = {one["id"]: one for one in shown["budgets"]}
    assert budgets["chromium"]["match"] == ["chromium", "org.chromium.Chromium"], budgets

    # One name is a list of one. There is no second spelling of it: a bare
    # string was accepted while there were files to keep reading, and there are
    # none.
    box.run("allow", USER, "code", "--limit", "45m")
    profile = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    code = [one for one in profile["budgets"] if one["id"] == "code"][0]
    assert code["match"] == ["code"], code

    # Run again over the budget that is there, it replaces the names as well as
    # the number -- or a command that says it covers the browser would cover
    # half of it.
    box.run("allow", USER, "chromium", "--limit", "30m")
    profile = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    browser = [one for one in profile["budgets"] if one["id"] == "chromium"][0]
    assert browser["match"] == ["chromium"], browser
    assert browser["dailyMinutes"] == 30, browser

    # `deny` takes several too, for the same reason: a browser half denied is a
    # browser.
    box.run("deny", USER, "chromium", "org.chromium.Chromium")
    profile = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    verdicts = {rule["match"]: rule["verdict"] for rule in profile["rules"]}
    assert verdicts["chromium"] == "deny", verdicts
    assert verdicts["org.chromium.Chromium"] == "deny", verdicts

    # And the same name twice is said rather than folded away: somebody who
    # typed it twice meant two ids, and that is not what they typed.
    twice = box.run("allow", USER, "chromium", "chromium")
    assert twice.returncode == 1, twice.stdout
    assert "named twice" in twice.stderr, twice.stderr


def check_a_profile_records_who_changed_it_and_when(box):
    """`/etc/omahouse/profiles.json` is written by root and read by everybody,
    and until now the honest answer to "who put this here" was "somebody".

    The stamp is written where the file is saved and not by each verb, because
    there are a dozen places that change a profile and a stamp a caller has to
    remember is one that will be missing from whichever verb somebody adds
    next. What is on disk is compared against what is about to be written, so
    exactly the profiles that differ are stamped.

    That last part is the one worth a test of its own: a run that rewrites the
    file without changing anybody must leave every stamp alone, or the record
    of who last changed a rule becomes a record of who last ran a command.
    """
    # Two profiles with distinct old stamps, written by hand. The clock has a
    # second's resolution, so a case that made two changes and compared the
    # times would compare two identical numbers and prove nothing; old stamps
    # from different years are what makes "was this one touched" answerable.
    box.write_profiles({"schemaVersion": 2, "profiles": [
        {"user": USER, "displayName": "Kid",
         "writtenBy": "ana", "writtenAt": "2020-01-01T10:00:00-03:00"},
        {"user": "daemon", "displayName": "Other",
         "writtenBy": "pedro", "writtenAt": "2021-06-02T11:00:00-03:00"}]})

    done = box.run("allow", USER, "code")
    assert done.returncode == 0, done.stderr

    profiles = {p["user"]: p for p in
                json.loads((box.config / "profiles.json").read_text())["profiles"]}
    changed = profiles[USER]
    assert changed["writtenAt"] != "2020-01-01T10:00:00-03:00", changed
    assert changed["writtenAt"].startswith(TODAY.isoformat()), changed
    assert changed["writtenBy"] and changed["writtenBy"] != "ana", changed

    # And the one nobody touched keeps the name and the date it had. Without
    # this the case above passes on an implementation that stamps everything.
    assert profiles["daemon"]["writtenBy"] == "pedro", profiles["daemon"]
    assert profiles["daemon"]["writtenAt"] == "2021-06-02T11:00:00-03:00", profiles["daemon"]

    # Running the same verb again changes nothing, so it restamps nothing.
    before = json.loads((box.config / "profiles.json").read_text())
    again = box.run("allow", USER, "code")
    assert again.returncode == 0, again.stderr
    assert json.loads((box.config / "profiles.json").read_text()) == before

    # `profile show` answers the question out loud, and answers it either way.
    shown = box.run("profile", "show", USER)
    assert "Last changed:" in shown.stdout, shown.stdout
    assert changed["writtenBy"] in shown.stdout, shown.stdout

    box.write_profiles({"schemaVersion": 2, "profiles": [{"user": USER}]})
    unstamped = box.run("profile", "show", USER)
    assert "Last changed: not recorded" in unstamped.stdout, unstamped.stdout

    # Both doors to root, and the name of the person behind each.
    #
    # `sudo` sets the *real* uid to root as well as the effective one, so
    # `getuid()` answers `root` for somebody who typed their own password a
    # second ago. Without reading `SUDO_UID` every write from a terminal was
    # signed `root`, which is the line docs/cli.md already refuses about a
    # grant. `daemon` is used as the stand-in because it exists everywhere and
    # is nobody, so the case does not depend on who is running it.
    for door in ("PKEXEC_UID", "SUDO_UID"):
        box.write_profiles({"schemaVersion": 2, "profiles": [{"user": USER}]})
        signed = box.run("allow", USER, "code", extra_env={door: "2"})
        assert signed.returncode == 0, signed.stderr
        wrote = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
        assert wrote["writtenBy"] == "daemon", (door, wrote)


def check_the_rules_for_anybody_are_readable_by_whoever_they_bind(box):
    """`status` is what the fiscalised person runs to see what is left of their
    own day, and a profile for anybody has no line with their name on it.

    The daemon enforces those rules already. If nothing else knew about them,
    the rules would bite and the person they bind could not read them -- which
    is the one thing this verb exists to prevent.

    Two questions and not one, and the split is the point. *Whose profile is
    this* is asked by `profile add`, to refuse a second one, and by the writing
    verbs, to know what they are about to change: a fallback must never answer
    there, or `omahouse allow kid` would edit the rules of everybody who sits at
    the machine. *What applies to this person* is asked by everything that
    reports.
    """
    box.write_profiles({"schemaVersion": 2, "profiles": [
        {"user": anybody(), "displayName": "Whoever sits here", "default": "deny",
         "budgets": [{"id": "session", "match": ["*"], "dailyMinutes": 60,
                      "onExhausted": "logout"}]},
        {"user": "daemon", "displayName": "Has their own", "default": "allow"}]})

    # `nobody` and not the account running the suite: that one is usually an
    # administrator, and an administrator is left out of a fallback -- so the
    # case would be asserting the absence of rules and calling it their
    # presence. The first attempt did exactly that and said so.
    covered = "nobody"
    # And it has to not be an administrator, or `rulesFor` leaves it out of the
    # fallback and every assertion below is about the absence of rules while
    # claiming to be about their presence. Checked rather than assumed: the
    # failure then names the fixture instead of the feature.
    assert pwd.getpwnam(covered).pw_uid != 0, covered
    try:
        assert covered not in grp.getgrnam("wheel").gr_mem, covered
    except KeyError:
        pass  # No wheel group at all is a machine where nobody is in it.

    theirs = box.run("status", covered)
    assert theirs.returncode == 0, theirs.stderr
    assert "default deny" in theirs.stdout, theirs.stdout
    assert "1h00m" in theirs.stdout, theirs.stdout
    # And they are told whose rules these are, because they will not find their
    # own name in profiles.json and would otherwise go looking for it.
    assert "not " + covered + "'s" in theirs.stdout, theirs.stdout
    # The shared profile's display name is not printed as though it were theirs.
    assert "Whoever sits here" not in theirs.stdout, theirs.stdout

    # Somebody with their own reads their own, and is told nothing about a
    # fallback, because none applies to them.
    own = box.run("status", "daemon")
    assert "default allow" in own.stdout, own.stdout
    assert "rules for anybody" not in own.stdout, own.stdout

    # An administrator is not covered by it, here as in the cycle: the two
    # answers have to match, or the daemon and the report disagree about who is
    # under rules.
    admin = box.run("status", "root")
    assert "rules for anybody" not in admin.stdout, admin.stdout
    assert "default deny" not in admin.stdout, admin.stdout

    # `profile add` still works for an account a fallback covers: the guard it
    # keeps is about a profile of their own, and they have none.
    made = box.run("profile", "add", covered, "--name", "Kid")
    assert made.returncode == 0, made.stderr
    written = json.loads((box.config / "profiles.json").read_text())["profiles"]
    assert sorted(p["user"] for p in written) == sorted([anybody(), "daemon", covered]), written

    # And now that they have one, it is theirs that answers.
    after = box.run("status", covered)
    assert "rules for anybody" not in after.stdout, after.stdout
    assert "default allow" in after.stdout, after.stdout


def check_the_verb_writes_the_profile_for_anybody(box):
    """`omahouse profile add '*'` and nothing new to learn.

    The mark is `*` in the file, so it is `*` on the command line too: a verb
    that took `anybody` and wrote `*` would be a second word for one thing,
    which is what the budget's `match` had and lost.

    What it refuses, it refuses *as well as* the file reader and never instead
    of it. A pot on a shared profile is turned away where the file is read,
    because a profile that arrives by being pushed never passes through a verb.
    """
    written = box.run("profile", "add", anybody(), "--name", "Whoever sits here")
    assert written.returncode == 0, written.stderr
    assert "profile for anybody" in written.stderr, written.stderr

    profile = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    assert profile["user"] == anybody(), profile
    assert profile["enforce"] is False and profile["default"] == "allow", profile

    # Every command it prints back is one that can be pasted. `*` is the
    # shell's wildcard, and an unquoted `omahouse profile enforce * --on` turns
    # into the contents of whatever directory it is run in.
    for line in written.stdout.splitlines():
        if "omahouse " in line:
            assert "'*'" in line or anybody() not in line, line

    # And `--json` carries the name as the file has it. Quoting there would be
    # the same mistake pointing the other way: a consumer reading `"'*'"` and
    # looking for an account spelled with quotation marks in it. Nobody pipes
    # JSON into a shell, so the hazard the quoting exists for is not there.
    shown = json.loads(box.run("profile", "show", anybody(), "--json").stdout)
    assert shown["profiles"][0]["user"] == anybody(), shown

    # A second one is refused, and the refusal quotes it too.
    again = box.run("profile", "add", anybody())
    assert again.returncode == 1, again.stdout
    assert "already has a profile" in again.stderr, again.stderr
    assert "profile show '*'" in again.stderr, again.stderr

    # `--create-user` makes an account, and this names none.
    made = box.run("profile", "add", anybody(), "--create-user")
    assert made.returncode == 1, made.stdout
    assert "not one" in made.stderr, made.stderr

    # Somebody can still be given a profile of their own beside it. This is the
    # error that costs the most and shows up last: if the verb answered "does
    # this account have a profile" with the fallback, nobody could ever be given
    # their own, and `omahouse allow` would edit everybody's rules at once.
    own = box.run("profile", "add", "nobody", "--name", "Kid")
    assert own.returncode == 0, own.stderr
    users = [p["user"] for p in
             json.loads((box.config / "profiles.json").read_text())["profiles"]]
    assert sorted(users) == sorted([anybody(), "nobody"]), users

    # And a verb that changes rules changes the one it was given, not the shared
    # one. `allow nobody` must not appear in the profile for anybody.
    allowed = box.run("allow", "nobody", "code")
    assert allowed.returncode == 0, allowed.stderr
    profiles = {p["user"]: p for p in
                json.loads((box.config / "profiles.json").read_text())["profiles"]}
    assert profiles["nobody"]["rules"] == [{"match": "code", "verdict": "allow"}], profiles
    assert profiles[anybody()]["rules"] == [], profiles

    # The refusal that lives in the reader, reached through the verb as well: a
    # pot on a shared login empties once and is never refilled.
    box.write_profiles({"schemaVersion": 2, "profiles": [
        {"user": anybody(),
         "budgets": [{"id": "pot", "match": ["*"], "dailyMinutes": 60,
                      "resets": "never"}]}]})
    refused = box.run("status", "nobody")
    assert refused.returncode != 0 or "never resets" in refused.stderr, refused.stderr


def check_a_pushed_profile_comes_in_through_the_stage(box):
    """A profile arriving from a central omahouse, and the refusals it meets.

    The push has no person behind it: the household's node account writes the
    document into a directory it owns, then runs one command with root. So this
    is the one path into `profiles.json` that no verb and no polkit prompt
    stands in front of, and everything omahouse would refuse of a file it reads
    it has to refuse here too -- otherwise the refusals that live in the verbs
    have a back door with a wire attached to it.
    """
    stage = box.config / "staged"
    stage.mkdir(parents=True, exist_ok=True)
    document = stage / "profile.json"

    # `daemon` stands in for the household's node account on both sides: it is
    # who `sudo` says asked, and who omahouse thinks that account is. The two
    # have to agree, because a push may replace what a previous push wrote and
    # nothing else.
    household = {"SUDO_UID": "2", "OMAHOUSE_OMAKURE_USER": "daemon"}

    def push(profile, schema=2):
        document.write_text(json.dumps({"schemaVersion": schema, "profiles": [profile]}))
        return box.run("profile", "apply-staged", extra_env=household)

    box.write_profiles({"schemaVersion": 2, "profiles": []})

    taken = push({"user": "nobody", "displayName": "Pushed", "default": "deny"})
    assert taken.returncode == 0, taken.stderr
    written = json.loads((box.config / "profiles.json").read_text())["profiles"][0]
    assert written["user"] == "nobody" and written["default"] == "deny", written
    # Signed with the account it came through, which is what lets a central
    # tell later what it issued from what somebody typed on the machine.
    assert written["writtenBy"] == "daemon", written

    # The stage is a letterbox and not a record: left behind, the next run
    # would apply the same document again.
    assert not document.exists()
    again = box.run("profile", "apply-staged", extra_env=household)
    assert again.returncode == 2, again.stdout
    assert "nothing at" in again.stderr, again.stderr

    # A second push replaces rather than duplicating.
    push({"user": "nobody", "displayName": "Pushed again", "default": "allow"})
    people = json.loads((box.config / "profiles.json").read_text())["profiles"]
    assert [p["user"] for p in people] == ["nobody"], people
    assert people[0]["default"] == "allow", people

    # And now every refusal that has to reach this path. Each is a thing the
    # file reader turns away, and each would otherwise arrive over the wire.
    refused = push({"user": "nobody"}, schema=1)
    assert refused.returncode == 1, refused.stdout
    assert "schema version 1" in refused.stderr, refused.stderr

    refused = push({"user": anybody(),
                    "budgets": [{"id": "pot", "match": ["*"], "dailyMinutes": 60,
                                 "resets": "never"}]})
    assert refused.returncode == 1, refused.stdout
    assert "never resets" in refused.stderr, refused.stderr

    refused = push({"user": "root"})
    assert refused.returncode == 1, refused.stdout
    assert "does not fiscalise themselves" in refused.stderr, refused.stderr

    # Two profiles at once is a merge and is not this.
    document.write_text(json.dumps({"schemaVersion": 2, "profiles": [
        {"user": "nobody"}, {"user": "daemon"}]}))
    several = box.run("profile", "apply-staged", extra_env=household)
    assert several.returncode == 1, several.stdout
    assert "takes one" in several.stderr, several.stderr

    # An argument is refused, because the sudoers line names every one of them
    # and a verb that took a path would make that line meaningless.
    argued = box.run("profile", "apply-staged", "somewhere-else.json")
    assert argued.returncode == 1, argued.stdout
    assert "takes nothing" in argued.stderr, argued.stderr

    # And a symbolic link is refused before anything is opened. This read has
    # root, and the day the account's wide sudoers line narrows, this is the
    # door.
    document.unlink(missing_ok=True)
    elsewhere = box.root / "somewhere-else.json"
    elsewhere.write_text(json.dumps({"schemaVersion": 2,
                                     "profiles": [{"user": "nobody"}]}))
    document.symlink_to(elsewhere)
    linked = box.run("profile", "apply-staged", extra_env=household)
    assert linked.returncode == 1, linked.stdout
    assert "symbolic link" in linked.stderr, linked.stderr


def check_a_push_stands_back_from_a_hand_made_profile(box):
    """The difference between an emergency exit and a loan.

    The tiebreak says the most recent written wins, and a manager pushes on a
    schedule -- so recency favours the central by construction. An
    administrator who fixes something by hand at two o'clock would be
    overwritten by the routine push at five past, with nobody deciding and
    nobody told. omahouse is installed whole on every machine so that somebody
    can sit down and fix it with the central unreachable, and a fix the next
    cycle erases was not a fix.

    It is also what makes a merge possible. A push that overwrote blind would
    destroy the evidence before anybody could be asked, and the merge would
    list nothing -- forever, and without looking wrong, because nothing is the
    ordinary answer.
    """
    # `daemon` stands in for the household's node account, on both sides: it is
    # who `sudo` says asked, and who omahouse thinks the account is.
    household = {"SUDO_UID": "2", "OMAHOUSE_OMAKURE_USER": "daemon"}
    stage = box.config / "staged"
    stage.mkdir(parents=True, exist_ok=True)

    def push(display, supersedes=None):
        document = {"schemaVersion": 2,
                    "profiles": [{"user": "nobody", "displayName": display}]}
        if supersedes is not None:
            document["supersedes"] = supersedes
        (stage / "profile.json").write_text(json.dumps(document))
        return box.run("profile", "apply-staged", extra_env=household)

    def local():
        for one in json.loads((box.config / "profiles.json").read_text())["profiles"]:
            if one["user"] == "nobody":
                return one
        return None

    box.write_profiles({"schemaVersion": 2, "profiles": []})

    # Nothing here yet: there is nothing to overwrite.
    assert push("From the manager").returncode == 0
    assert local()["displayName"] == "From the manager"

    # The last write here was a push, so the central knows what it replaces.
    assert push("From the manager again").returncode == 0
    assert local()["displayName"] == "From the manager again"

    # Now a person changes it on this machine.
    assert box.run("profile", "remove", "nobody", "--keep-account").returncode == 0
    assert box.run("profile", "add", "nobody", "--name", "By hand").returncode == 0
    by_hand = local()
    assert by_hand["writtenBy"] != "daemon", by_hand

    # The routine push stands back, and says what it found and what to do.
    stood = push("From the manager once more")
    assert stood.returncode == 1, stood.stdout
    assert "last changed by" in stood.stderr, stood.stderr
    assert "does not say it is replacing" in stood.stderr, stood.stderr
    assert local()["displayName"] == "By hand", local()

    # A push that says which local version it is replacing goes through: that
    # is somebody on the manager having looked at this one and decided, which
    # is the second pass of a merge and the only way a hand-made profile is
    # replaced.
    resolved = push("Resolved", supersedes=by_hand)
    assert resolved.returncode == 0, resolved.stderr
    assert local()["displayName"] == "Resolved", local()

    # And a resolution that has gone stale is refused: the machine changed
    # between the manager looking and the manager deciding, so what was
    # approved is not what is here.
    #
    # It is the *content* that is compared and not the stamp, and the first
    # attempt compared the stamp: it has a second's resolution, this case runs
    # in far less than one, and the stale push sailed through looking current.
    assert box.run("profile", "remove", "nobody", "--keep-account").returncode == 0
    assert box.run("profile", "add", "nobody", "--name", "Changed again").returncode == 0
    stale = push("Stale", supersedes=by_hand)
    assert stale.returncode == 1, stale.stdout
    assert local()["displayName"] == "Changed again", local()


def check_a_profile_travels_out_and_back_as_the_same_bytes(box):
    """What one machine emits is what another takes in, unchanged.

    That is the reason `profile show --json` carries the envelope rather than
    the bare profile, and without a case it is a hope: the two shapes would
    drift the first time somebody added a field to one side, and nothing would
    say so until a household tried to reconcile and found a document its
    manager could not read.

    So the case does the trip. It takes the bytes the verb prints, puts them in
    the stage without touching them, and asks the machine to take them in.
    """
    box.write_profiles({"schemaVersion": 2, "profiles": []})
    box.run("profile", "add", "nobody", "--name", "Kid")
    box.run("allow", "nobody", "code", "--limit", "45m")

    emitted = box.run("--json", "profile", "show", "nobody")
    assert emitted.returncode == 0, emitted.stderr

    # Not re-serialised, not reshaped: the bytes.
    stage = box.config / "staged"
    stage.mkdir(parents=True, exist_ok=True)
    (stage / "profile.json").write_text(emitted.stdout)

    box.write_profiles({"schemaVersion": 2, "profiles": []})
    taken = box.run("profile", "apply-staged", extra_env={"SUDO_UID": "2"})
    assert taken.returncode == 0, taken.stderr

    arrived = json.loads((box.config / "profiles.json").read_text())["profiles"]
    assert len(arrived) == 1, arrived
    # Everything but the stamp, which is about who wrote it here and is
    # expected to differ: it came in through the household's account.
    was = json.loads(emitted.stdout)["profiles"][0]
    for side in (arrived[0], was):
        side.pop("writtenBy", None)
        side.pop("writtenAt", None)
    assert arrived[0] == was, (arrived[0], was)

    # And the same bytes are what the manager's side takes, so the envelope
    # serves both directions rather than one.
    box.run("machine", "add", "the study")
    collected = box.run("profile", "collect", "the study", "nobody",
                        stdin=emitted.stdout)
    assert collected.returncode == 0, collected.stderr
    filed = box.state / "elsewhere" / "the study" / "nobody" / "profile.json"
    assert filed.exists(), list((box.state / "elsewhere").rglob("*"))
    assert json.loads(filed.read_text())["profiles"][0]["user"] == "nobody"

    # A profile offered under the wrong name is refused: filed wrongly, a merge
    # would read somebody else's rules as this person's.
    wrong = box.run("profile", "collect", "the study", "daemon", stdin=emitted.stdout)
    assert wrong.returncode == 1, wrong.stdout
    assert "belongs to nobody" in wrong.stderr, wrong.stderr

    # And only for a computer the household has written down.
    stranger = box.run("profile", "collect", "nowhere", "nobody", stdin=emitted.stdout)
    assert stranger.returncode == 2, stranger.stdout
    assert "no machine called" in stranger.stderr, stranger.stderr


def check_the_merge_tells_the_four_answers_apart(box):
    """What each computer says about one profile, and what to do about it.

    Three kinds of answer and not one. *It differs* would put three different
    decisions under a single word, and somebody asked to choose would read
    "conflict" where there is nothing to conflict with.

    And a fourth thing that is not an answer at all: a machine nothing has been
    collected from. Folded into "no differences" it would make silence and
    agreement read the same, which is the shape `house` already refuses about a
    day nobody sent.
    """
    box.write_profiles({"schemaVersion": 2, "profiles": []})
    box.run("profile", "add", "kid", "--name", "Here")
    for machine in ("the study", "the kitchen", "the attic", "the porch", "silent"):
        assert box.run("machine", "add", machine).returncode == 0

    def collected(machine, document):
        where = box.state / "elsewhere" / machine / "kid"
        where.mkdir(parents=True, exist_ok=True)
        (where / "profile.json").write_text(json.dumps(document))

    theirs = {"user": "kid", "displayName": "Changed there",
              "writtenBy": "ana", "writtenAt": "2026-09-08T14:00:00-03:00"}
    mine = json.loads(box.run("--json", "profile", "show", "kid").stdout)
    collected("the study", {"schemaVersion": 2, "profiles": [theirs]})
    collected("the kitchen", {"schemaVersion": 2, "profiles": []})
    collected("the attic", mine)
    # The same rules under another hand and another second. A pushed profile
    # is restamped by the machine that took it in, so two copies that agree on
    # every rule differ in the stamp as a matter of course -- and a merge that
    # read the stamp would list every machine in the house as `changed` forever,
    # with nothing to decide about any of them. The stamp is shown in the
    # table; it is not what the answer is about.
    restamped = json.loads(json.dumps(mine))
    restamped["profiles"][0]["writtenBy"] = "omakure"
    restamped["profiles"][0]["writtenAt"] = "2026-09-08T14:00:01-03:00"
    assert restamped != mine, restamped
    collected("the porch", restamped)

    seen = json.loads(box.run("--json", "profile", "merge", "kid").stdout)
    says = {row["machine"]: row["is"] for row in seen["machines"]}
    assert says == {"the study": "changed", "the kitchen": "gone",
                    "the attic": "same", "the porch": "same",
                    "silent": "not collected"}, says
    # Both versions are shown whole, because whoever is choosing needs what
    # changed and who wrote each -- and all of that is already in the documents.
    assert seen["mine"]["displayName"] == "Here", seen
    study = [r for r in seen["machines"] if r["machine"] == "the study"][0]
    assert study["theirs"]["writtenBy"] == "ana", study

    # A machine that has sent nothing is named, not folded into agreement.
    plain = box.run("profile", "merge", "kid")
    assert "has sent nothing" in plain.stdout, plain.stdout
    assert "2 to decide" in plain.stdout, plain.stdout

    # `--keep` prints what makes that machine take this one, carrying what the
    # manager believes is over there. That is what makes the second pass apply
    # what was listed: the machine refuses it if that is not still what it has.
    keep = json.loads(box.run("profile", "merge", "kid", "--keep", "the study").stdout)
    assert keep["profiles"][0]["displayName"] == "Here", keep
    assert keep["supersedes"]["displayName"] == "Changed there", keep

    # `--take` brings a version here.
    took = box.run("profile", "merge", "kid", "--take", "the study")
    assert took.returncode == 0, took.stderr
    here = json.loads((box.config / "profiles.json").read_text())["profiles"]
    assert here[0]["displayName"] == "Changed there", here

    # And taking an absence takes a removal: somebody with the manager
    # unreachable decided this account has no rules, and agreeing means the
    # same here. Without this, the bluntest thing the exit does is the one
    # thing the household cannot act on.
    gone = box.run("profile", "merge", "kid", "--take", "the kitchen")
    assert gone.returncode == 0, gone.stderr
    assert json.loads((box.config / "profiles.json").read_text())["profiles"] == []

    # And now the answer for the study has *changed its kind*, which is the
    # distinction this case exists for: with nothing here to conflict with, its
    # profile is not a conflict, it is one to take in. The first version of
    # this case never reached `new` at all -- the manager always had a profile
    # -- so collapsing `new` into `changed` changed nothing and the mutation
    # sailed through.
    after = json.loads(box.run("--json", "profile", "merge", "kid").stdout)
    assert after["mine"] is None, after
    kinds = {row["machine"]: row["is"] for row in after["machines"]}
    assert kinds["the study"] == "new", kinds
    assert kinds["the attic"] == "new", kinds
    assert kinds["the porch"] == "new", kinds
    assert kinds["the kitchen"] == "same", kinds

    # The refusals. Nothing collected is not a version to decide about.
    quiet = box.run("profile", "merge", "kid", "--take", "silent")
    assert quiet.returncode == 1, quiet.stdout
    assert "nothing has been collected" in quiet.stderr, quiet.stderr

    # A push carries a profile and cannot carry its absence.
    nothing = box.run("profile", "merge", "kid", "--keep", "the study")
    assert nothing.returncode == 1, nothing.stdout
    assert "cannot carry its absence" in nothing.stderr, nothing.stderr

    # One decision at a time.
    both = box.run("profile", "merge", "kid", "--take", "the study",
                   "--keep", "the attic")
    assert both.returncode == 1, both.stdout
    assert "One decision at a time" in both.stderr, both.stderr


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
        "schemaVersion": 2,
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
        "schemaVersion": 2,
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
        "schemaVersion": 2,
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
        "schemaVersion": 2,
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
                {"id": "session", "match": ["*"], "dailyMinutes": 120,
                 "onExhausted": "logout"},
                {"id": "chromium", "match": ["chromium"], "dailyMinutes": 45,
                 "onExhausted": "close"},
                {"id": "code", "match": ["code"]},
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

    box.write_profiles({"schemaVersion": 2, "profiles": []})
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


def check_watch_runs_for_a_while_and_then_stops(box):
    """`--for` is the middle ground between one cycle and forever.

    It is what lets the loop be a scheduled job: something starts it every
    minute, it works for that minute, and it ends. A tick that jams dies with
    the minute it was in rather than jamming for a day, and what restarts it is
    the schedule rather than `Restart=always`.

    The window is asserted loosely -- it is a real clock and this is a real
    process -- but the two ends matter: it must not return at once, which would
    make it `--once` wearing another name, and it must not outlive its window,
    which is the whole promise.
    """
    box.write_profiles(watching_profile())

    began = time.monotonic()
    ran = box.run("watch", "--for", "6", "--interval", "2")
    took = time.monotonic() - began
    assert ran.returncode == 0, ran.stderr
    assert 5 <= took <= 11, f"--for 6 took {took:.1f}s"

    # And it really counted while it was alive: three cycles of two seconds,
    # give or take the one it starts with.
    spent = box.day(TODAY)["budgets"]["session"]
    assert 4 <= spent <= 10, f"six seconds of watching debited {spent}s"

    # The window is announced, because a run somebody started by hand has to say
    # when it intends to give the terminal back.
    assert "for 6s" in ran.stderr, ran.stderr


def check_watch_refuses_a_window_it_cannot_honour(box):
    """Every way `--for` can be asked for something it would not do.

    All of them are refusals with a sentence rather than a run that quietly does
    something else, because this flag exists to be put in a schedule and nobody
    reads the output of a scheduled job until it has already been wrong for a
    week.
    """
    both = box.run("watch", "--for", "10", "--once")
    assert both.returncode == 1
    assert "different things" in both.stderr, both.stderr

    # Shorter than one cycle is a run that would count nothing at all.
    short = box.run("watch", "--for", "1", "--interval", "5")
    assert short.returncode == 1
    assert "shorter than one cycle" in short.stderr, short.stderr

    # Seconds, like `--interval` and unlike `--limit`. A bare number elsewhere in
    # this program means minutes, so `1m` here is refused rather than guessed at.
    for wrong in ("1m", "0", "90000", "later"):
        said = box.run("watch", "--for", wrong)
        assert said.returncode == 1, f"--for {wrong} was accepted"
        assert "whole seconds" in said.stderr, said.stderr


def check_leave_says_what_is_left_and_can_be_said_twice(box):
    """The other end of `grant`, and the property a loop needs.

    A household with more than one computer consolidates on a loop: read every
    machine's day, add it up, push the truth back. "Take ten minutes off" said
    every minute drains the day by teatime; "leave thirty minutes of today"
    said twice is the same as said once. Idempotence is the whole reason this
    verb says what should remain rather than what to remove.
    """
    box.write_profiles(watching_profile())
    box.write_day(TODAY, {"session": 1800})

    # Two hours a day, half an hour spent: ninety minutes stand. Leaving thirty
    # takes an hour back.
    first = box.run("leave", USER, "--session", "30m")
    assert first.returncode == 0, first.stderr
    assert "30m of session left today" in first.stdout, first.stdout
    assert "1h taken back" in first.stdout, first.stdout

    # And again, and again. Nothing is written and it says so, because a loop
    # has to be able to tell "nothing to do" from "it did not work".
    for _ in range(2):
        again = box.run("leave", USER, "--session", "30m")
        assert again.returncode == 0, again.stderr
        assert "nothing written" in again.stdout, again.stdout
    assert len(box.day(TODAY)["grants"]) == 1, box.day(TODAY)["grants"]

    # Asking for more than stands hands time back, because "what should remain"
    # is a statement about the day and not a direction of travel.
    more = box.run("leave", USER, "--session", "45m")
    assert more.returncode == 0, more.stderr
    assert "15m handed back" in more.stdout, more.stdout

    minutes = [g["minutes"] for g in box.day(TODAY)["grants"]]
    assert minutes == [-60, 15], minutes

    document = json.loads(box.run("leave", USER, "--session", "1h", "--json").stdout)
    assert document["leftSeconds"] == 3600, document
    assert document["minutes"] == 15, document


def check_the_household_balance_is_absolute_and_fails_closed(box):
    box.write_profiles(watching_profile())
    box.write_day(TODAY, {"session": 1800})
    enrolled = box.run("allocation", "init", USER)
    assert enrolled.returncode == 0, enrolled.stderr
    initial = json.loads(enrolled.stdout)["allocation"]
    zero = json.loads(box.run("status", USER, "--json").stdout)["budgets"][0]
    assert zero["leftSeconds"] == 0, zero
    refused_leave = box.run("leave", USER, "--session", "30m")
    assert refused_leave.returncode == 1, refused_leave.stdout
    assert "comes from the household" in refused_leave.stderr, refused_leave.stderr
    planned = box.run("allocation", "plan", USER)
    assert planned.returncode == 0, planned.stderr
    plan = json.loads(planned.stdout)
    document = plan["documents"]["here"]
    # One computer in the household, so nothing has been spent elsewhere and
    # the balance is the whole credit. The two numbers are carried separately
    # even here, because which of them a late report moves is the difference
    # between allowing a little too much and allowing a day too much.
    assert document["house"]["session"] == {
        "credit": 7200, "elsewhere": 0, "counted": 0}, document
    for _ in range(2):
        applied = box.run("allocation", "apply", USER, stdin=json.dumps(document))
        assert applied.returncode == 0, applied.stderr
    box.write_day(TODAY, {"session": 1860})
    balance = json.loads(box.run("status", USER, "--json").stdout)["budgets"][0]
    assert balance["leftSeconds"] == 5340, balance
    same = box.run("allocation", "plan", USER)
    assert same.returncode == 0 and json.loads(same.stdout) == plan, same.stderr
    for changed in ({"revision": 0}, {"house": {"session": {"credit": 8000}}},
                    {"house": {"other": {"credit": 7200, "elsewhere": 0}}},
                    {"authority": "another"}, {"date": "2000-01-01"}):
        bad = box.run("allocation", "apply", USER, stdin=json.dumps(dict(document, **changed)))
        assert bad.returncode == 1, bad.stdout
    granted = box.run("grant", USER, "--session", "10m", "--json")
    assert granted.returncode == 0, granted.stderr
    # The ten minutes are ten minutes here and now. They used to be nothing at
    # all until the manager next planned, and on a machine that had lost contact
    # they were nothing ever -- which is the worst moment for the one verb that
    # exists so somebody can hand over time with the manager unreachable.
    assert "pendingAllocation" not in json.loads(granted.stdout)
    assert json.loads(granted.stdout)["leftSeconds"] == 5940
    next_plan = json.loads(box.run("allocation", "plan", USER).stdout)
    assert next_plan["documents"]["here"]["house"]["session"]["credit"] == 7800, next_plan
    assert next_plan["revision"] == 2
    # And when the household folds those ten minutes in, they are still ten
    # minutes. `counted` rises by the same six hundred `credit` did, so the
    # machine stops adding its own copy on top and the balance does not move.
    # Adding without subtracting would read 6540 here, which is the same grant
    # paid twice.
    issued = next_plan["documents"]["here"]
    assert issued["house"]["session"]["counted"] == 600, issued
    assert box.run("allocation", "apply", USER, stdin=json.dumps(issued)).returncode == 0
    after = json.loads(box.run("status", USER, "--json").stdout)["budgets"][0]
    assert after["leftSeconds"] == 5940, after
    # Reservations cannot be reconstructed from consumption after state loss.
    (box.state / "allocations" / USER / f"{TODAY}.json").unlink()
    refused = box.run("allocation", "plan", USER)
    assert refused.returncode == 1 and "restore" in refused.stderr, refused.stderr
    exported = json.loads(box.run("day", USER).stdout)
    assert exported["allocation"]["authority"] == initial["authority"]
    assert exported["observedAt"]


def check_leave_does_not_change_household_credit(box):
    """A local synchronization adjustment must not become global credit."""
    box.write_profiles(watching_profile())
    box.write_day(TODAY, {"session": 1800})
    before = json.loads(box.run("house", USER, "--json").stdout)["budgets"][0]
    assert before["limitSeconds"] == 7200
    for remaining in ("30m", "45m", "0m", "0m"):
        applied = box.run("leave", USER, "--session", remaining)
        assert applied.returncode == 0, applied.stderr
        after = json.loads(box.run("house", USER, "--json").stdout)["budgets"][0]
        assert after["limitSeconds"] == 7200, after
        assert after["totalSeconds"] == 1800, after
    assert all(g.get("kind") == "adjustment" for g in box.day(TODAY)["grants"])
    assert box.run("grant", USER, "--session", "10m").returncode == 0
    credited = json.loads(box.run("house", USER, "--json").stdout)["budgets"][0]
    assert credited["limitSeconds"] == 7800, credited


def check_leave_refuses_what_it_cannot_leave(box):
    """Every refusal, because this verb is made to be driven by a machine."""
    box.write_profiles(watching_profile())

    both = box.run("leave", USER, "--session", "10m", "--budget", "code=5m")
    assert both.returncode == 1 and "One of them" in both.stderr, both.stderr

    neither = box.run("leave", USER)
    assert neither.returncode == 1 and "One of them" in neither.stderr, neither.stderr

    unknown = box.run("leave", USER, "--budget", "ghost=10m")
    assert unknown.returncode == 2, unknown.stderr
    assert "no budget called ghost" in unknown.stderr, unknown.stderr

    # `code` counts and never runs out, so there is nothing for a number to be
    # left of. A `-20m` against a budget that cannot end is a row nobody reads.
    endless = box.run("leave", USER, "--budget", "code=10m")
    assert endless.returncode == 1, endless.stderr
    assert "no limit" in endless.stderr, endless.stderr

    shapeless = box.run("leave", USER, "--budget", "session")
    assert shapeless.returncode == 1 and "wants an id" in shapeless.stderr, shapeless.stderr


# -- time per site ------------------------------------------------------------
#
# docs/design.md §5.2, end to end, with no browser on the machine and no root.
# The extension and the native messaging host are both driven here: the host is
# `omahouse meter`, spoken to over a pipe the way Chromium speaks to it, and the
# file it leaves is the same file `watch` then reads.


def check_the_meter_writes_what_the_browser_told_it(box):
    """The host's half: frames in on stdin, one line out per frame.

    It is stupid on purpose. The browser spike measured this process
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
    # always a pipe (the browser spike measured `STDIN_ISATTY=False`),
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
    whether anybody is looking at it: the browser spike asked
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
    assert budgets["youtube.com"]["match"] == ["youtube.com"], budgets
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
    # Two minutes and not one, because the marks are 10, 5 and 1 and a mark the
    # budget was never above is not a mark: a one minute budget would announce
    # its one minute mark at the instant the tab opened, which is the thing that
    # stopped happening. Two minutes leaves the 1 minute mark real.
    document["profiles"][0]["budgets"].append(
        {"id": "youtube.com", "match": ["youtube.com"], "kind": "site",
         "dailyMinutes": 2, "onExhausted": "block"})
    box.write_profiles(document)
    box.write_day(TODAY, {"youtube.com": 116})
    box.browsing("www.youtube.com")

    # Two seconds left: counted, warned about, and opening.
    left = box.run("watch", "--once")
    assert left.returncode == 0, left.stderr
    assert box.day(TODAY)["budgets"]["youtube.com"] == 118, box.day(TODAY)
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
        {"id": "youtube.com", "match": ["youtube.com"], "kind": "site",
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


def check_the_time_per_site_stops_denying_the_budget_above_it(box):
    """The last line of the block has to agree with the table over it.

    The block was written when a site was counted and nothing else, and it said
    so out loud. Site budgets grew teeth afterwards, and the sentence stayed:
    with a limit written, the same screen said `stops opening` in the table and
    "there is no site limit" two inches below. Defect 11, and this is the case
    that would have caught it.
    """
    document = watching_profile()
    box.write_profiles(document)
    box.browsing("www.youtube.com")
    box.run("watch", "--once")

    # With nothing written about a site, the old sentence is the true one.
    quiet = box.run("status", USER)
    assert "never billed to a" in quiet.stdout, quiet.stdout

    written = box.run("limit", USER, "--site", "youtube.com=30m")
    assert written.returncode == 0, written.stderr

    said = box.run("status", USER)
    sites = below(said.stdout, "TIME PER SITE")
    assert "never billed to a" not in sites, said.stdout
    assert "billed to youtube.com" in sites, said.stdout
    assert "that budget spends" in sites, said.stdout
    # And the table above it still says what running out does, so the two halves
    # of the screen are saying one thing.
    assert row_for(said.stdout, "youtube.com")[-1] == "stops opening", said.stdout

    # A second one, and the sentence counts.
    box.run("limit", USER, "--site", "tiktok.com=10m")
    two = below(box.run("status", USER).stdout, "TIME PER SITE")
    assert "billed to youtube.com, tiktok.com" in two, two
    assert "those budgets spend" in two, two

    # A site budget with no limit counts and never runs out, so it is not one of
    # these: naming it would put the claim back the other way around.
    only = watching_profile()
    only["profiles"][0]["budgets"].append(
        {"id": "wikipedia.org", "match": ["wikipedia.org"], "kind": "site"})
    box.write_profiles(only)
    none = below(box.run("status", USER).stdout, "TIME PER SITE")
    assert "never billed to a" in none, none


def check_the_house_writes_down_its_machines(box):
    """The list of computers a household owns, which omahouse never had.

    It holds no rules. What a machine does is its own profiles.json, on the
    machine, because a central that held the rules would be a central whose
    absence is a machine with no rules at all.
    """
    # Most households are one computer, and one that has never been told about
    # another is not misconfigured.
    alone = box.run("machines")
    assert alone.returncode == 0, alone.stderr
    assert "whole house" in alone.stdout, alone.stdout
    assert json.loads(box.run("machines", "--json").stdout) == []

    # A name on its own is a note to self: the household owns that computer and
    # cannot reach it yet, and the answer says which of the two it is.
    noted = box.run("machine", "add", "the kitchen laptop")
    assert noted.returncode == 0, noted.stderr
    assert "not paired yet" in noted.stdout, noted.stdout

    paired = box.run("machine", "add", "workstation",
                     "--node", "omk1_abc123", "--at", "192.168.1.20:7879")
    assert paired.returncode == 0, paired.stderr
    assert "192.168.1.20:7879" in paired.stdout, paired.stdout

    listed = json.loads(box.run("machines", "--json").stdout)
    assert {m["name"] for m in listed} == {"the kitchen laptop", "workstation"}
    assert [m["reachable"] for m in sorted(listed, key=lambda m: m["name"])] == [False, True]

    # Writing one down again is how a machine that was named before it was
    # paired becomes reachable. It updates rather than refusing, and a field
    # left off keeps what the file had.
    again = box.run("machine", "add", "the kitchen laptop", "--node", "omk1_def456")
    assert again.returncode == 0, again.stderr
    assert "written down again" in again.stdout, again.stdout
    kitchen = [m for m in json.loads(box.run("machines", "--json").stdout)
               if m["name"] == "the kitchen laptop"][0]
    assert kitchen["nodeId"] == "omk1_def456", kitchen
    assert kitchen["reachable"] is False, "an endpoint was invented for it"

    # And taking one out of the list does nothing to the machine, which is said
    # rather than left to be assumed.
    gone = box.run("machine", "remove", "workstation")
    assert gone.returncode == 0, gone.stderr
    assert "Nothing on that machine changed" in gone.stdout, gone.stdout
    assert [m["name"] for m in json.loads(box.run("machines", "--json").stdout)] \
        == ["the kitchen laptop"]


def check_a_machine_says_what_it_is(box):
    """Alone, manager or managed -- and alone is not a lesser state.

    It was implicit until now: a machine became a manager by running one verb
    and managed by running another, and nobody could ask. Every verb after it
    reads differently depending on the answer, so the answer has to be one a
    person can get.
    """
    said = box.run("machine", "kind")
    assert said.returncode == 0, said.stderr
    assert "on its own, and nothing is missing" in said.stdout, said.stdout
    # Said out loud, because "alone" is what somebody would otherwise read as
    # "not set up yet".
    assert "Linking is what a second computer needs" in said.stdout, said.stdout

    document = json.loads(box.run("machine", "kind", "--json").stdout)
    assert document["kind"] == "alone", document
    assert document["managedBy"] == "", document

    # No file at all is that same answer, and never a failure.
    assert not (box.config / "machine.json").exists()


def check_a_machine_file_that_is_wrong_is_said_and_not_guessed(box):
    """What it refuses, because this file decides how every later verb reads.

    Reading a half-written link as `alone` would drop a machine out of a
    household it is really in, and nothing downstream would notice.
    """
    for document, says in (
        ({"schemaVersion": 1, "kind": "managed"}, "names no manager"),
        ({"schemaVersion": 1, "kind": "overlord"}, "overlord"),
        ({"schemaVersion": 1, "kind": "manager", "since": "yesterday"}, "yesterday"),
        ({"schemaVersion": 99, "kind": "manager"}, "schema version 99"),
    ):
        (box.config / "machine.json").write_text(json.dumps(document))
        said = box.run("machine", "kind")
        assert said.returncode == 1, f"{document} came back {said.returncode}"
        assert says in said.stderr, said.stderr

    # A manager, written properly, reads back as one.
    (box.config / "machine.json").write_text(json.dumps({
        "schemaVersion": 1, "kind": "manager", "name": "the study",
        "since": "2026-09-06T10:00:00",
    }))
    said = box.run("machine", "kind")
    assert said.returncode == 0, said.stderr
    assert "the study" in said.stdout, said.stdout
    assert "household's console" in said.stdout, said.stdout

    # And a managed one names who manages it.
    (box.config / "machine.json").write_text(json.dumps({
        "schemaVersion": 1, "kind": "managed", "name": "the kitchen laptop",
        "managedBy": "omk1_abc123", "since": "2026-09-06T10:00:00",
    }))
    said = box.run("machine", "kind")
    assert said.returncode == 0, said.stderr
    assert "its manager is omk1_abc123" in said.stdout, said.stdout
    assert "still enforcing its own rules on its own" in said.stdout, said.stdout


def check_link_refuses_before_it_touches_the_far_machine(box):
    """Every refusal `machine link` can make on its own.

    The order matters more than the messages: this machine has to fail before
    anything reaches the far one. Finding out that the manager cannot describe
    itself *after* installing a package on somebody else's computer would leave
    that computer half linked, with no verb here that knows it.
    """
    nowhere = box.root / "no-omakure-here"
    nowhere.mkdir()
    against = {"OMAHOUSE_OMAKURE_BIN": str(nowhere / "omakure"),
               "OMAHOUSE_OMAKURE_CONFIG_DIR": str(nowhere),
               # An ssh that would fail loudly if it were ever reached.
               "OMAHOUSE_SSH": str(nowhere / "no-ssh-here")}

    said = box.run("machine", "link", extra_env=against)
    assert said.returncode == 1, said.stdout
    assert "which computer" in said.stderr, said.stderr

    said = box.run("machine", "link", "arch@10.0.0.2", extra_env=against)
    assert said.returncode == 1, said.stdout
    assert "--at <host:port>" in said.stderr, said.stderr

    # And the one that says the install is not whole. omahouse ships with
    # omakure, so a machine with one and not the other is broken rather than
    # missing a prerequisite -- telling somebody to install a second product
    # would be telling them to work around their own package manager.
    said = box.run("machine", "link", "arch@10.0.0.2", "--at", "10.0.0.1:7879",
                   extra_env=against)
    assert said.returncode == 2, said.stdout
    assert "this install is not whole" in said.stderr, said.stderr
    assert "pacman -S omahouse" in said.stderr, said.stderr

    # Nothing was written, and nothing was reached.
    assert not (box.config / "machine.json").exists(), "a kind was written anyway"
    assert not (box.config / "machines.json").exists(), "a machine was written anyway"


def check_the_machine_verbs_refuse_what_they_cannot_do(box):
    """Every refusal, because this file is edited by hand.

    What it refuses matters more than what it accepts: a household that cannot
    say which computer it means is a household whose next verb is a guess.
    """
    for wrong, code, says in (
        (("machine", "add"), 1, "which machine"),
        (("machine", "remove"), 1, "which machine"),
        (("machine", "remove", "nobody"), 2, "no machine called"),
        (("machine",), 1, "link, kind, invite, prepare, add, remove or token"),
        (("machine", "polish"), 1,
         "link, kind, invite, prepare, add, remove or token"),
        (("machine", "token"), 1, "which machine"),
        (("machine", "token", "nobody"), 2, "nothing to read nobody with"),
    ):
        said = box.run(*wrong)
        assert said.returncode == code, f"{' '.join(wrong)} came back {said.returncode}"
        assert says in said.stderr, said.stderr

    # Two of one name is refused where the file is read, not left for a verb to
    # trip over later.
    (box.config / "machines.json").write_text(json.dumps({
        "schemaVersion": 1,
        "machines": [{"name": "twin"}, {"name": "twin"}],
    }))
    twice = box.run("machines")
    assert twice.returncode == 1
    assert "twice" in twice.stderr, twice.stderr


def a_pairing_line(name="the study", node="omk1_abc123", key="mHf6yPqk",
                   cert="30820122300d", at="192.168.1.10:7879"):
    """The line one machine prints for another, built here rather than asked for.

    Building it in the test is the point: `machine invite` needs a real Omakure
    to print one, and what these cases are about is what happens to a line after
    a person has carried it. Writing the format out twice is what makes a change
    to it fail here instead of failing between two computers in somebody's house.
    """
    document = json.dumps({"name": name, "node": node, "key": key,
                           "cert": cert, "at": at},
                          separators=(",", ":")).encode()
    return "omahouse-pair-1." + base64.urlsafe_b64encode(document).decode().rstrip("=")


def check_pairing_refuses_a_line_that_did_not_arrive_whole(box):
    """The walk between two computers, and everything that can go wrong on it.

    None of this needs an Omakure, and that is deliberate: the failures worth
    catching are the ones a person causes by copying, and they have to be caught
    before anything is written. A line that arrived in pieces must be refused as
    pieces, not trusted as half a certificate.
    """
    # An address is not optional, and it is not guessed. A machine cannot know
    # which of its interfaces the household will reach it on.
    for wrong, says in (
        (("machine", "invite"), "--at <host:port>"),
        (("machine", "invite", "--at", "192.168.1.10"), "--at <host:port>"),
        (("machine", "invite", "--at", "192.168.1.10:0"), "--at <host:port>"),
        (("machine", "prepare", "--at", "10.0.0.2:7879"), "--invite <line>"),
    ):
        said = box.run(*wrong)
        assert said.returncode == 1, f"{' '.join(wrong)} came back {said.returncode}"
        assert says in said.stderr, said.stderr

    # A line that is not one says so by name, rather than failing later inside
    # a trust registry.
    whole = a_pairing_line()
    for line, says in (
        ("omk1_abc123", "omahouse-pair-1."),
        (whole[:len(whole) // 2], "damaged"),
        (a_pairing_line(cert=""), "certificate"),
        (a_pairing_line(at="192.168.1.10"), "host:port"),
        (a_pairing_line(node=""), "names no node"),
    ):
        said = box.run("machine", "prepare", "--invite", line, "--at", "10.0.0.2:7879")
        assert said.returncode == 1, f"{line[:40]} came back {said.returncode}"
        assert says in said.stderr, said.stderr

    # And the same reading on the way back.
    said = box.run("machine", "add", "laptop", "--pair", whole[:20])
    assert said.returncode == 1, said.stdout
    assert "damaged" in said.stderr, said.stderr

    # A `--node` beside a `--pair` is somebody correcting a value they cannot
    # have checked, and what it buys is a peer trusted under one identity and
    # looked for at another.
    said = box.run("machine", "add", "laptop", "--pair", whole,
                   "--node", "omk1_something_else")
    assert said.returncode == 1, said.stdout
    assert "not both" in said.stderr, said.stderr


def check_pairing_says_when_there_is_no_omakure_to_pair_with(box):
    """The one thing that has to be installed first, named as itself.

    Every verb below this would fail on the same missing thing with a different
    sentence -- `node path is insecure`, `io_failed: Permission denied` -- and
    none of those name what is really wrong. So the question is asked once, up
    front, and answered with the line to run.
    """
    nowhere = box.root / "no-omakure-here"
    nowhere.mkdir()
    against = {"OMAHOUSE_OMAKURE_BIN": str(nowhere / "omakure"),
               "OMAHOUSE_OMAKURE_CONFIG_DIR": str(nowhere)}

    said = box.run("machine", "invite", "--at", "192.168.1.10:7879", extra_env=against)
    # Exit 2 and not 1: something it was asked about is not there, which is what
    # tells a script "install it" from "you typed it wrong".
    assert said.returncode == 2, said.stdout
    assert "no omakure on this machine" in said.stderr, said.stderr
    # A broken install and not a missing prerequisite: the omahouse package
    # depends on omakure, so the way back is the package manager.
    assert "this install is not whole" in said.stderr, said.stderr

    # And nothing was written on the way to finding out.
    assert not (nowhere / "node.toml").exists(), "a config was written anyway"

    said = box.run("machine", "prepare", "--invite", a_pairing_line(),
                   "--at", "10.0.0.2:7879", extra_env=against)
    assert said.returncode == 2, said.stdout
    assert "no omakure on this machine" in said.stderr, said.stderr


def check_a_day_travels_from_one_machine_to_another(box):
    """The two ends of the transport, and nothing between them.

    `day` publishes what this computer spent; `collect` accepts what another
    one did. What moves the bytes is outside omahouse on purpose -- a scheduled
    script, a person with ssh and a pipe -- and the proof that the seam is in
    the right place is that this case is a pipe.
    """
    box.write_profiles(watching_profile())
    box.write_day(TODAY, {"session": 1800, "chromium": 600})

    # The document is the ledger unchanged. No envelope and no summary: every
    # transformation between the machine that spent the time and the sum is a
    # place the two can come to disagree.
    published = json.loads(box.run("day", USER).stdout)
    assert published["user"] == USER, published
    assert published["date"] == TODAY.isoformat(), published
    assert published["budgets"] == {"session": 1800, "chromium": 600}, published

    # A day is accepted only for a computer the household has written down.
    # This refusal is the whole of what stands between the sum and a stranger.
    stranger = box.run("collect", "laptop", USER, stdin=json.dumps(published))
    assert stranger.returncode == 2, stranger.stdout
    assert "no machine called laptop" in stranger.stderr, stranger.stderr

    box.run("machine", "add", "laptop", "--node", "omk1_a", "--at", "10.0.0.2:7879")
    landed = json.loads(box.run("collect", "laptop", USER, "--json",
                                stdin=json.dumps(published)).stdout)
    assert landed["machine"] == "laptop", landed
    assert landed["path"].endswith(f"elsewhere/laptop/{USER}/{TODAY.isoformat()}.json")

    # And the loop closes: what `collect` wrote is what `house` adds up, with
    # nobody having placed a file by hand.
    document = json.loads(box.run("house", USER, "--json").stdout)
    session = [b for b in document["budgets"] if b["id"] == "session"][0]
    assert [c["machine"] for c in session["spent"]] == ["here", "laptop"], session
    assert session["totalSeconds"] == 3600, session
    assert document["notHeardFrom"] == [], document


def check_the_transport_refuses_a_day_it_should_not_file(box):
    """Every refusal, because a day filed wrong is time added to the wrong person.

    Nothing downstream would ever notice: `house` reads whatever is in the
    directory. So the checking has to happen where the day comes in.
    """
    box.write_profiles(watching_profile())
    box.write_day(TODAY, {"session": 1800})
    box.run("machine", "add", "laptop", "--node", "omk1_a", "--at", "10.0.0.2:7879")
    published = json.loads(box.run("day", USER).stdout)

    # A day that names somebody else, offered under this name.
    wrong = box.run("collect", "laptop", "pedro", stdin=json.dumps(published))
    assert wrong.returncode == 1, wrong.stdout
    assert "belongs to" in wrong.stderr, wrong.stderr

    # A machine whose clock is ahead. Accepting it puts a file in tomorrow's
    # name that today's sum will not read and tomorrow's will, which is a total
    # that changes overnight for no reason anybody can see.
    ahead = dict(published, date=(TODAY + timedelta(days=1)).isoformat())
    tomorrow = box.run("collect", "laptop", USER, stdin=json.dumps(ahead))
    assert tomorrow.returncode == 1, tomorrow.stdout
    assert "has not happened here yet" in tomorrow.stderr, tomorrow.stderr

    for body, says in (
        ("", "nothing came in"),
        ("not json at all", "is not a day"),
        ("[]", "is not a day"),
        (json.dumps({"schemaVersion": 99, "user": USER, "date": TODAY.isoformat(),
                     "budgets": {}, "grants": [], "events": []}), "is not a day"),
    ):
        said = box.run("collect", "laptop", USER, stdin=body)
        assert said.returncode == 1, f"{body[:30]!r} came back {said.returncode}"
        assert says in said.stderr, said.stderr

    # And nothing was written by any of them.
    assert not (box.state / "elsewhere").exists(), "a refused day was filed anyway"

    # A day nobody spent is exit 2 and never an empty document: a quiet
    # afternoon and a machine that is not reporting must not look the same.
    quiet = box.run("day", USER, "--date", "2020-01-01")
    assert quiet.returncode == 2, quiet.stdout
    assert "nothing for" in quiet.stderr, quiet.stderr
    assert not quiet.stdout.strip(), quiet.stdout

    bad = box.run("day", USER, "--date", "yesterday")
    assert bad.returncode == 1 and "--date wants a date" in bad.stderr, bad.stderr


def check_the_house_adds_up_what_every_machine_spent(box):
    """What the house spent, as opposed to what this computer spent.

    The profile's number is the household's: two hours in the house, not two
    hours per computer. A machine on its own enforces the whole thing, which is
    what makes one computer complete; a house with three of them has to add the
    three up.
    """
    box.write_profiles(watching_profile())
    box.run("machine", "add", "laptop", "--node", "omk1_a", "--at", "10.0.0.2:7879")
    box.run("machine", "add", "workstation", "--node", "omk1_b", "--at", "10.0.0.3:7879")

    box.write_day(TODAY, {"session": 1800, "chromium": 600})
    elsewhere = box.state / "elsewhere" / "laptop" / USER
    elsewhere.mkdir(parents=True)
    (elsewhere / f"{TODAY.isoformat()}.json").write_text(json.dumps({
        "schemaVersion": 1, "user": USER, "date": TODAY.isoformat(),
        "budgets": {"session": 2400, "chromium": 1200},
        "grants": [], "events": [], "sites": {}, "presence": {},
    }))

    document = json.loads(box.run("house", USER, "--json").stdout)
    session = [b for b in document["budgets"] if b["id"] == "session"][0]
    assert session["totalSeconds"] == 4200, session
    assert session["limitSeconds"] == 7200, session
    assert session["leftSeconds"] == 3000, session
    assert [c["machine"] for c in session["spent"]] == ["here", "laptop"], session

    # The machine nobody heard from is not in the sum, and the answer says so.
    # A total quietly missing a computer reads exactly like a total of a quiet
    # afternoon, and one of those is a fact.
    assert document["notHeardFrom"] == ["workstation"], document
    assert document["machines"] == 2, document

    said = box.run("house", USER)
    assert "Nothing today from workstation" in said.stdout, said.stdout
    assert "LAPTOP" in said.stdout and "IN ALL" in said.stdout, said.stdout


def check_the_house_refuses_what_it_cannot_add_up(box):
    """A house of one computer, and the two ways to ask about nobody."""
    box.write_profiles(watching_profile())

    # No machines written down: the sum is this computer, which is the whole
    # house, and that is an answer rather than an error.
    alone = json.loads(box.run("house", USER, "--json").stdout)
    assert alone["machines"] == 1 and alone["notHeardFrom"] == [], alone

    nobody = box.run("house", "stranger")
    assert nobody.returncode == 2, nobody.stderr
    assert "no profile for stranger" in nobody.stderr, nobody.stderr

    which = box.run("house")
    assert which.returncode == 1 and "which user" in which.stderr, which.stderr


# -- the promise of the stage -------------------------------------------------

# -- the package, put on a machine and taken off it --------------------------
#
# The one case in this file that is not about the binary. It is here rather than
# in `vm/` because of where the failure it catches lives: `vm/cases/` is nightly
# and manual and needs a VM with a browser on it, and this failure is silent and
# on the wrong side -- a restriction left on a machine that no longer has the
# tool to lift it. Something that can only be caught by an afternoon in a VM is
# something that will be caught after it has shipped.

INSTALL_SCRIPT = ROOT / "packaging/omahouse.install"
METER_PACK = ROOT / "packaging/omahouse-meter-pack"


def artifacts_the_install_declares():
    """The one list in `packaging/omahouse.install`, read the way `post_remove`
    reads it.

    Parsed out of the file rather than repeated here, and that is the whole
    point of the exercise: a copy in this file would be a second hand-written
    list, which is the thing that was wrong in the first place."""
    text = INSTALL_SCRIPT.read_text()
    body = re.search(r"_artifacts\(\)\s*\{\n\s*cat <<-'LIST'\n(.*?)\n\s*LIST\n",
                     text, re.S)
    assert body, f"{INSTALL_SCRIPT} no longer has one list for anything to read"
    declared = []
    for line in body.group(1).splitlines():
        parts = line.strip().split(None, 2)
        if not parts:
            continue
        assert parts[0] in ("take", "prune", "borrow", "keep", "unlink"), line
        assert len(parts) >= 2 and parts[1].startswith("/"), line
        declared.append((parts[0], parts[1]))
    assert declared, "the list is empty"
    return declared


def paths_under(root):
    """Every path in that tree, spelled as it would be on a real machine."""
    return {"/" + child.relative_to(root).as_posix() for child in root.rglob("*")}


def is_declared(path, declared):
    """Whether the list has anything to say about that path.

    Three ways it can. The path is named; the path is inside something the list
    says to `take`, which is removed whole; or the path is a directory that only
    exists so that something named can live in it -- `/etc/chromium` is not an
    artefact, it is where one of them is."""
    for verb, named in declared:
        if fnmatch(path, named):
            return True
        if verb == "take" and fnmatch(path, named + "/*"):
            return True
        if named.startswith(path + "/"):
            return True
    return False


def may_outlive_the_removal(path, declared):
    """Whether that path is allowed to still be there after `post_remove`.

    Only three kinds are: what the list `keep`s on purpose, a directory it
    `borrow`s from another program, and the containers those live in."""
    for verb, named in declared:
        if verb in ("keep", "borrow") and fnmatch(path, named):
            return True
        if named.startswith(path + "/"):
            return True
    return False


def check_the_removal_leaves_the_other_program_its_own_unit(box):
    """A node unit omahouse did not write survives `pacman -R`, and its drop-in
    does not.

    `machine link` writes `omakure-node.service` only when there is none,
    because Omakure's own installer will not provision it before a tokens file
    that only a running node can produce -- so a household following that order
    has nowhere to begin. Omakure's installer writes its own at that same path
    on a later upgrade, and from that moment the file is not ours.

    Removing it anyway would take a household's wire down on omahouse's way out,
    on a machine where Omakure is a product of its own that nobody asked to
    uninstall. So the `unlink` verb removes a file only while its first line
    still says omahouse wrote it, and this is the case for the other branch --
    the branch a marker check has no reason to get right by accident.

    The drop-in goes either way: it is ours by name, it is the file that put the
    console on the household network, and a node still serving that console
    after the household is gone is the thing this whole removal is about.
    """
    machine = box.root / "othersunit"
    unit = machine / "etc/systemd/system/omakure-node.service"
    unit.parent.mkdir(parents=True, exist_ok=True)
    theirs = ("[Unit]\nDescription=Omakure machine node service\n"
              "\n[Service]\nExecStart=/usr/bin/omakure node serve\n")
    unit.write_text(theirs)
    drop = machine / "etc/systemd/system/omakure-node.service.d/omahouse.conf"
    drop.parent.mkdir(parents=True, exist_ok=True)
    drop.write_text("# Written by omahouse.\n[Service]\n"
                    "ExecStart=\nExecStart=/usr/bin/omakure node serve --bind 0.0.0.0:8787\n")
    # One of the three `_was_linked` asks about, so the removal knows there was
    # a household here at all. Without it nothing below runs and this case
    # passes for the wrong reason.
    (machine / "etc/omahouse").mkdir(parents=True, exist_ok=True)
    (machine / "etc/omahouse/machine.json").write_text('{"kind":"managed"}\n')

    done = subprocess.run(
        ["bash", "-c", f". {INSTALL_SCRIPT}; pre_remove; post_remove"],
        capture_output=True, text=True,
        env=dict(os.environ, OMAHOUSE_PACK_ROOT=str(machine)))
    assert done.returncode == 0, done.stderr

    assert unit.is_file(), (
        "omahouse removed a unit it did not write, so taking omahouse off a "
        "computer took that household's wire down with it")
    assert unit.read_text() == theirs, "omahouse rewrote another program's unit"
    assert not drop.exists(), (
        "the drop-in that opened the console on the household network survived "
        "the removal, so the node goes on serving it")
    assert not (machine / "etc/omahouse/machine.json").exists()
    # And it said so, because an operator reading this is deciding whether they
    # still have a node.
    assert "Omakure is left as it was" in done.stdout + done.stderr

def check_the_removal_covers_what_the_install_makes(box):
    """The property `packaging/omahouse.install` exists to have: **nothing can
    be added to what the installation creates without the removal knowing.**

    The package is no longer describable by its own file list. The `.crx`, the
    update manifest, the force-install policy, the native messaging manifest and
    the signing key are made by a scriptlet, so `pacman -Ql` does not list them
    and `pacman -Qkk` does not verify them -- and what stood in for that was a
    column of hand-written `rm` lines in `post_remove`. The VM proved on the
    first attempt that the column was already incomplete: the meter's native
    messaging host survived `pacman -R`, on a machine with nothing left on it
    that could explain or undo it.

    So the scriptlet declares what it creates, once, and this runs the real
    thing against a directory of its own and checks the declaration against the
    disk. Two questions, and the second is the one with the teeth:

      1. Did anything appear that the list does not name?
      2. Did anything the install made outlive `post_remove`?

    Add an artefact to `omahouse-meter-pack` and forget the line, and both go
    red here rather than on somebody's machine.

    Nothing of this machine is touched. `$OMAHOUSE_PACK_ROOT` is the prefix the
    packer already had and the scriptlet now shares, and under it the scriptlet
    refuses to enable a unit or to kill a process -- the same discipline
    `mayTouchTheBrowserPolicy` keeps in the CLI."""
    declared = artifacts_the_install_declares()
    machine = box.root / "machine"

    # What `pacman -U` itself puts down: `vm/PKGBUILD`'s `package()`, cut to the
    # four files the scriptlet reaches for. Everything else in that list is a
    # binary or a unit and is removed by pacman without a scriptlet's help.
    packaged = {}
    for source, destination, mode in (
            (ROOT / "packaging/omahouse-meter-host", "usr/lib/omahouse/meter-host", 0o755),
            (METER_PACK, "usr/lib/omahouse/meter-pack", 0o755),
            (ROOT / "extension/manifest.json",
             "usr/share/omahouse/chromium/omahouse-meter/manifest.json", 0o644),
            (ROOT / "extension/background.js",
             "usr/share/omahouse/chromium/omahouse-meter/background.js", 0o644)):
        target = machine / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())
        target.chmod(mode)
        packaged["/" + destination] = target

    # A login stack with an `account` line in it, so the PAM edit has somewhere
    # to go. The one thing on a machine omahouse changes rather than creates,
    # which is why it is checked by comparing bytes rather than by the list.
    pam = machine / "etc/pam.d/system-login"
    pam.parent.mkdir(parents=True, exist_ok=True)
    was = ("auth       required   pam_unix.so\n"
           "account    required   pam_unix.so\n"
           "session    required   pam_unix.so\n")
    pam.write_text(was)

    environment = dict(os.environ, OMAHOUSE_PACK_ROOT=str(machine))
    before = paths_under(machine)

    def scriptlet(function):
        done = subprocess.run(
            ["bash", "-c", f". {INSTALL_SCRIPT}; {function}"],
            capture_output=True, text=True, env=environment)
        assert done.returncode == 0, f"{function}: {done.stderr}"
        return done.stdout + done.stderr

    said = scriptlet("post_install")

    # If the packer did not run, everything below is vacuously true, which is
    # the worst way for this case to pass.
    crx = machine / "usr/share/omahouse/chromium/omahouse-meter.crx"
    assert crx.is_file(), (
        "the packer wrote no archive, so this case proved nothing. `openssl` and "
        f"`zip` are its whole toolchain and vm/PKGBUILD names both. It said:\n{said}")
    identifier = (machine / "etc/omahouse/meter/id").read_text().strip()
    assert len(identifier) == 32, identifier
    for named in ("etc/chromium/policies/managed/omahouse-meter.json",
                  "etc/chromium/native-messaging-hosts/com.omahouse.meter.json",
                  "usr/share/omahouse/chromium/updates.xml"):
        assert identifier in (machine / named).read_text(), named

    # The §11 policy and the `blocked` list are written by the program and not by
    # the scriptlet, so nothing above made them -- and they are on the list all
    # the same, because what the list is about is what omahouse leaves on a
    # machine and not which of its parts put it there. Made here by hand so that
    # the removal half is asked about them too.
    (machine / "etc/omahouse/blocked").write_text("kid\n")
    (machine / "etc/chromium/policies/managed/omahouse.json").write_text(
        '{"URLBlocklist":["youtube.com"]}\n')

    # And the same for what `omahouse machine link` writes, which is the other
    # half of what omahouse leaves on a machine and the half no install ever
    # puts there: linking happens long after the package went on, so nothing
    # above could have made these and nothing above would have missed them. A
    # VM run of the walkthrough found all six still on a machine that had taken
    # omahouse off -- a sudoers rule, two bearer tokens and a running node --
    # and this is that finding moved to where it costs a commit instead.
    for named, contents in (
            ("etc/omahouse/machine.json", '{"kind":"managed"}\n'),
            ("etc/omahouse/machine-tokens.json", '{"the kitchen laptop":"omk_…"}\n'),
            ("etc/omahouse/omakure-token", "omk_a_bearer\n"),
            ("etc/sudoers.d/omahouse-node", "omakure ALL=(root) NOPASSWD: /usr/bin/omahouse\n"),
            ("etc/systemd/system/omakure-node.service.d/omahouse.conf",
             "# Written by omahouse.\n[Service]\n"),
            # With the marker, because that is the unit omahouse wrote. The one
            # without it is a case of its own below.
            ("etc/systemd/system/omakure-node.service",
             "# Written by omahouse because there was none.\n[Unit]\n")):
        target = machine / named
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(contents)

    made = paths_under(machine) - before
    undeclared = sorted(path for path in made if not is_declared(path, declared))
    assert not undeclared, (
        "the installation put these on a machine and "
        f"{INSTALL_SCRIPT.name} does not name any of them, so `post_remove` will "
        "not take them off:\n      " + "\n      ".join(undeclared))

    # And now `pacman -R`: it removes the files it owns and the directories that
    # are its own and are empty, and then runs the scriptlet.
    for target in packaged.values():
        target.unlink()
    (machine / "usr/share/omahouse/chromium/omahouse-meter").rmdir()
    (machine / "usr/lib/omahouse").rmdir()

    scriptlet("post_remove")

    left = sorted(path for path in made
                  if (machine / path.lstrip("/")).exists()
                  and not may_outlive_the_removal(path, declared))
    assert not left, (
        "`pacman -R` left these on a machine with no omahouse on it:\n      "
        + "\n      ".join(left))

    # The one that really happened, named so that a regression says which
    # regression it is.
    assert not (machine / "etc/chromium/native-messaging-hosts"
                          "/com.omahouse.meter.json").exists()
    # And the key, which is the whole of what a key per machine buys: nothing is
    # left anywhere that could sign an extension this machine's policy accepts.
    assert not (machine / "etc/omahouse/meter").exists()

    # Nothing of ours inside the directories that are not ours, and the
    # directories themselves still standing: they belong to the browser, and
    # Omarchy's own `browser-policy.sh` writes `policies.json` beside our file.
    for verb, named in declared:
        if verb != "borrow":
            continue
        borrowed = machine / named.lstrip("/")
        assert borrowed.is_dir(), f"{named} was taken, and it is not ours to take"
        for child in borrowed.rglob("*"):
            assert "omahouse" not in child.name, child

    # The two kept on purpose, and the message that names them, out of the same
    # list `post_remove` worked from.
    assert (machine / "etc/omahouse").is_dir()
    assert (machine / "var/lib/omahouse").is_dir()

    # And the PAM file, which is not on the list because it is not a file
    # omahouse creates: it is one it edits, and the only sound assertion about
    # an edit is that the bytes came back.
    assert pam.read_text() == was, pam.read_text()


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


def publication_fixture(box):
    box.write_profiles({"schemaVersion": 2, "profiles": []})
    assert box.run("profile", "add", "kid").returncode == 0
    (box.config / "machine.json").write_text(json.dumps({"schemaVersion": 1,
        "kind": "manager", "name": "central", "since": "2026-09-09T10:00:00Z"}))
    for machine in ("a", "b"):
        result = box.run("machine", "add", machine, "--node", "omk1_abc", "--at", "localhost:8787")
        assert result.returncode == 0, result.stderr
    return json.loads(box.run("profile", "show", "kid", "--json").stdout)


def check_profile_list_and_show_say_where_a_profile_stands(box):
    draft = publication_fixture(box)
    result = box.run("profile", "list")
    assert "PUBLISHED" in result.stdout, result.stdout
    assert "never" in result.stdout, result.stdout
    assert "never published" in box.run("profile", "show", "kid").stdout
    assert json.loads(box.run("profile", "show", "kid", "--json").stdout) == draft
    directory = box.state / "elsewhere" / "a" / "kid"
    directory.mkdir(parents=True)
    (directory / "published.json").write_text(json.dumps({"schemaVersion": 1,
        "publishedAt": "2026-09-09T10:00:00Z", "profile": draft["profiles"][0]}))
    assert "up to date" in box.run("profile", "show", "kid").stdout
    assert box.run("limit", "kid", "--session", "1h").returncode == 0
    assert "1 behind" in box.run("profile", "list").stdout
    changed = json.loads(json.dumps(draft))
    changed["profiles"][0]["displayName"] = "Changed there"
    (directory / "profile.json").write_text(json.dumps(changed))
    row = json.loads(box.run("profile", "list", "--json").stdout)[0]
    assert row["unresolved"] is True, row
    assert "unresolved (a)" == row["publication"], row


def fake_publication_battery(box):
    draft = publication_fixture(box)
    workspace = box.root / "workspace"
    workspace.mkdir()
    world = workspace / "world.json"
    world.write_text(json.dumps({"a": {"profiles": [], "schemaVersion": 2},
                                 "b": {"profiles": [], "schemaVersion": 2}}))
    (workspace / "omahouse-profile-publish.py").write_text('''import argparse, json
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument("--action"); p.add_argument("--user"); p.add_argument("--machine",action="append"); p.add_argument("--document")
a=p.parse_args(); root=Path(__file__).parent; world=json.loads((root/"world.json").read_text()); rows=[]
for machine in a.machine:
    value=world[machine]; code=value.get("pushExitCode" if a.action == "push" else "sendExitCode",0)
    row=dict(machine=machine,reached=not value.get("offline",False),declared=not value.get("undeclared",False),exitCode=(None if value.get("offline") else 1 if value.get("undeclared") else code),stdout="",stderr=value.get("reason",""))
    if row["reached"] and row["declared"]:
        if a.action == "push":
            doc=json.loads(a.document)
            with (root/"pushes.jsonl").open("a") as f: f.write(json.dumps(dict(machine=machine,document=doc))+"\\n")
            if code == 0:
                world[machine]={"schemaVersion":2,"profiles":doc["profiles"]}
                world[machine]["profiles"][0]["writtenBy"]="omakure"
                world[machine]["profiles"][0]["writtenAt"]="2026-09-09T12:00:00Z"
                if value.get("readbackOffline"): world[machine]["offline"]=True
                if value.get("readbackChanged"): world[machine]["profiles"][0]["displayName"]="Changed after push"
                (root/"world.json").write_text(json.dumps(world))
        else: row["stdout"]=json.dumps({k:v for k,v in value.items() if k in ("schemaVersion","profiles")})
    rows.append(row)
print(json.dumps(rows))
''')
    env = {"OMAHOUSE_OMAKURE_WORKSPACE": str(workspace), "OMAHOUSE_OMAKURE_USER": USER}
    def run(*args):
        return box.run(*args, extra_env=env)
    return draft, workspace, world, run


def check_publish_refuses_what_it_cannot_do(box):
    _, workspace, _, run = fake_publication_battery(box)
    for args in [("kid",), ("absent", "--to", "a"), ("kid", "--to", "missing"),
                 ("kid", "--to", "a", "--all")]:
        result = run("profile", "publish", *args)
        assert result.returncode != 0, result.stdout
    assert box.run("machine", "add", "unpaired").returncode == 0
    assert run("profile", "publish", "kid", "--to", "unpaired").returncode != 0
    (workspace / "omahouse-profile-publish.py").unlink()
    result = run("profile", "publish", "kid", "--to", "a")
    assert "omakure battery install" in result.stderr, result.stderr


def check_publish_lands_a_draft(box):
    draft, workspace, world, run = fake_publication_battery(box)
    result = run("profile", "publish", "kid", "--to", "a", "--json")
    assert result.returncode == 0, result.stderr + result.stdout
    assert "published" in result.stdout, result.stdout
    pushes = lambda: [json.loads(line) for line in (workspace / "pushes.jsonl").read_text().splitlines()]
    assert pushes()[0]["document"] == draft, pushes()
    published = box.state / "elsewhere" / "a" / "kid" / "published.json"
    assert json.loads(published.read_text())["profile"] == draft["profiles"][0]
    assert "up to date" in run("profile", "show", "kid").stdout
    assert run("limit", "kid", "--session", "1h").returncode == 0
    assert "1 behind" in run("profile", "list").stdout
    observed = json.loads(world.read_text())["a"]["profiles"][0]
    result = run("profile", "publish", "kid", "--to", "a")
    assert result.returncode == 0, result.stdout + result.stderr
    assert pushes()[-1]["document"]["supersedes"] == observed, pushes()
    before = published.read_bytes()
    assert run("limit", "kid", "--session", "2h").returncode == 0
    remote = json.loads(world.read_text()); remote["a"]["pushExitCode"] = 1; remote["a"]["reason"] = "changed during push"; world.write_text(json.dumps(remote))
    result = run("profile", "publish", "kid", "--to", "a")
    assert result.returncode == 1, result.stdout + result.stderr
    assert published.read_bytes() == before
    assert len(pushes()) == 3, pushes()


def check_publish_checks_everywhere_before_pushing_anywhere(box):
    draft, workspace, world, run = fake_publication_battery(box)
    assert run("profile", "publish", "kid", "--all").returncode == 0
    remote = json.loads(world.read_text()); remote["a"]["profiles"][0]["displayName"] = "Remote edit"; world.write_text(json.dumps(remote))
    before = (workspace / "pushes.jsonl").read_bytes()
    result = run("profile", "publish", "kid", "--to", "b", "--json")
    assert result.returncode == 1, result.stdout + result.stderr
    assert json.loads(result.stdout)["changedOn"] == ["a"], result.stdout
    assert (workspace / "pushes.jsonl").read_bytes() == before
    assert run("profile", "merge", "kid", "--take", "a").returncode == 0
    result = run("profile", "publish", "kid", "--to", "b")
    assert result.returncode == 0, result.stdout + result.stderr
    # Keeping the old central rules must force a push, even if its publication baseline matches.
    assert run("profile", "publish", "kid", "--to", "a").returncode == 0
    remote = json.loads(world.read_text()); remote["a"]["profiles"][0]["displayName"] = "Another edit"; world.write_text(json.dumps(remote))
    assert run("profile", "publish", "kid", "--to", "a").returncode == 1
    kept = run("profile", "merge", "kid", "--keep", "a")
    assert kept.returncode == 0, kept.stderr
    assert json.loads(kept.stdout)["supersedes"]["displayName"] == "Another edit"
    result = run("profile", "publish", "kid", "--to", "a")
    assert result.returncode == 0 and "published" in result.stdout, result.stdout + result.stderr
    assert "unresolved" not in run("profile", "list").stdout


def check_publish_names_what_it_could_not_check(box):
    _, _, world, run = fake_publication_battery(box)
    remote = json.loads(world.read_text()); remote["a"]["offline"] = True; remote["a"]["reason"] = "offline"; world.write_text(json.dumps(remote))
    result = run("profile", "publish", "kid", "--to", "a", "--json")
    assert result.returncode == 1, result.stdout + result.stderr
    assert result.stdout.strip().startswith("{"), result.stderr
    data = json.loads(result.stdout)
    assert data["notChecked"] and "unreachable" in result.stdout, result.stdout
    remote["a"] = {"undeclared": True, "reason": "omahouse.profile-push"}; world.write_text(json.dumps(remote))
    result = run("profile", "publish", "kid", "--to", "a")
    assert result.returncode == 1 and "install" in result.stdout, result.stdout + result.stderr


def check_publish_does_not_invent_a_confirmation(box):
    _, workspace, world, run = fake_publication_battery(box)
    assert run("profile", "publish", "kid", "--to", "a").returncode == 0
    record = box.state / "elsewhere" / "a" / "kid" / "published.json"
    before = record.read_bytes()
    assert run("limit", "kid", "--session", "1h").returncode == 0
    remote = json.loads(world.read_text())
    remote["a"]["readbackOffline"] = True
    world.write_text(json.dumps(remote))
    result = run("profile", "publish", "kid", "--to", "a")
    assert result.returncode == 1, result.stdout + result.stderr
    assert "applied, confirmation unavailable" in result.stdout, result.stdout
    assert record.read_bytes() == before
    assert len((workspace / "pushes.jsonl").read_text().splitlines()) == 2


def check_publish_keeps_an_unreachable_conflict_unresolved(box):
    draft, workspace, world, run = fake_publication_battery(box)
    remote = json.loads(world.read_text()); remote["a"] = draft
    remote["a"]["profiles"][0]["displayName"] = "Remote edit"
    world.write_text(json.dumps(remote))
    assert run("profile", "publish", "kid", "--to", "b").returncode == 1
    remote["a"]["offline"] = True; world.write_text(json.dumps(remote))
    result = run("profile", "publish", "kid", "--to", "b", "--json")
    assert result.returncode == 1, result.stdout + result.stderr
    assert json.loads(result.stdout)["changedOn"] == ["a"]
    assert not (workspace / "pushes.jsonl").exists()
    assert run("profile", "merge", "kid", "--keep", "a").returncode == 0
    # A later observed stamp is a different explicit decision, even with the same remote rules.
    remote["a"].pop("offline"); remote["a"]["profiles"][0]["writtenBy"] = "changed again"
    world.write_text(json.dumps(remote))
    result = run("profile", "publish", "kid", "--to", "b", "--json")
    assert result.returncode == 1 and json.loads(result.stdout)["unresolved"], result.stdout
    assert not (workspace / "pushes.jsonl").exists()


def check_publish_all_skips_what_is_up_to_date(box):
    _, workspace, _, run = fake_publication_battery(box)
    assert run("profile", "publish", "kid", "--all").returncode == 0
    before = (workspace / "pushes.jsonl").read_bytes()
    result = run("profile", "publish", "kid", "--all")
    assert result.returncode == 0, result.stdout + result.stderr
    assert (workspace / "pushes.jsonl").read_bytes() == before


def main():
    assert CLI.is_file(), f"missing CLI at {CLI}"
    cases = [
        check_version_is_said_once,
        check_the_help_and_the_declaration_name_the_same_verbs,
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
        check_publish_refuses_what_it_cannot_do,
        check_publish_lands_a_draft,
        check_publish_checks_everywhere_before_pushing_anywhere,
        check_publish_names_what_it_could_not_check,
        check_publish_all_skips_what_is_up_to_date,
        check_publish_does_not_invent_a_confirmation,
        check_publish_keeps_an_unreachable_conflict_unresolved,

        check_profile_list_and_show_say_where_a_profile_stands,
        check_profile_show,
        check_profile_refuses_what_it_does_not_do,
        check_a_profile_from_nothing_to_read_back,
        check_grant_writes_the_days_ledger,
        check_allow_warns_about_what_is_really_inside,
        check_one_budget_covers_a_browsers_two_ids,
        check_a_profile_records_who_changed_it_and_when,
        check_the_rules_for_anybody_are_readable_by_whoever_they_bind,
        check_the_verb_writes_the_profile_for_anybody,
        check_a_pushed_profile_comes_in_through_the_stage,
        check_a_push_stands_back_from_a_hand_made_profile,
        check_a_profile_travels_out_and_back_as_the_same_bytes,
        check_the_merge_tells_the_four_answers_apart,
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
        check_leave_says_what_is_left_and_can_be_said_twice,
        check_leave_refuses_what_it_cannot_leave,
        check_leave_does_not_change_household_credit,
        check_the_household_balance_is_absolute_and_fails_closed,
        check_watch_counts_a_faster_tick,
        check_watch_runs_for_a_while_and_then_stops,
        check_watch_refuses_a_window_it_cannot_honour,
        check_the_meter_writes_what_the_browser_told_it,
        check_watch_counts_a_site_only_while_somebody_is_there,
        check_watch_bills_nothing_for_a_focus_file_that_is_wrong,
        check_limit_writes_a_budget_about_a_site,
        check_limit_refuses_what_it_cannot_do_about_a_site,
        check_a_site_that_ran_out_is_blocked_and_comes_back_on_its_own,
        check_a_site_budget_composes_with_the_web_rules,
        check_status_and_report_show_the_time_per_site,
        check_the_time_per_site_stops_denying_the_budget_above_it,
        check_watch_writes_presence_beside_the_budgets_and_never_into_them,
        check_the_house_writes_down_its_machines,
        check_the_house_adds_up_what_every_machine_spent,
        check_a_day_travels_from_one_machine_to_another,
        check_the_transport_refuses_a_day_it_should_not_file,
        check_the_house_refuses_what_it_cannot_add_up,
        check_a_machine_says_what_it_is,
        check_link_refuses_before_it_touches_the_far_machine,
        check_a_machine_file_that_is_wrong_is_said_and_not_guessed,
        check_the_machine_verbs_refuse_what_they_cannot_do,
        check_pairing_refuses_a_line_that_did_not_arrive_whole,
        check_pairing_says_when_there_is_no_omakure_to_pair_with,
        check_the_removal_covers_what_the_install_makes,
        check_the_removal_leaves_the_other_program_its_own_unit,
        check_the_reading_verbs_write_nothing,
        check_a_pot_survives_a_day_nobody_has_written_yet,
        check_a_budget_can_be_asked_never_to_reset,
    ]
    for case in cases:
        # A machine of its own per case: a profiles.json one case wrote is a
        # profiles.json the next one would have to know about.
        with tempfile.TemporaryDirectory(prefix="omahouse-cli-") as directory:
            case(Box(directory))
    print(f"test_cli.py: {len(cases)} cases passed")


if __name__ == "__main__":
    main()
