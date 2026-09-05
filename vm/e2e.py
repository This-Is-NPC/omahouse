#!/usr/bin/env python3
"""The VM suite: the teeth, in a machine that is not this one.

`mise run verify` proves everything that can be proved without a graphical
session. This proves the rest, and the rest is the dangerous half: a SIGTERM into
somebody's app scope, a write to a `cgroup.kill`, a name in the file PAM reads,
and `loginctl terminate-user`. None of it may ever run here.

The rule that makes that true is not a convention. Three of them, in order, and
none of the cases is imported until all three have answered:

  1. the libvirt domain is `omahouse-poc`, running from the disk the manifest
     names -- a domain with the same name and another definition is somebody
     else's machine that happens to share a name;
  2. the address it came up on is not one of this machine's;
  3. `uname -n` on the far side of the ssh says `omahouse-poc`.

The third is the one that matters, because it is about the machine that will
really run the commands rather than the one that was asked to start.

The binary under test is built from the working tree every run and installed on
the guest. It is the artefact being tested; a copy frozen into the image would
be testing last week.

Every duration this suite waits on comes out of `vm/manifest.toml`, under one of
two regimes. `quick` is the default and is what somebody types forty times in an
afternoon; `long` is explicit and holds the windows that catch what only shows
with time. There is one knob and it is `--pace`.

    python3 vm/e2e.py                 every case, quick, then shut the machine down
    python3 vm/e2e.py --pace long     the same cases with the long windows
    python3 vm/e2e.py --keep          leave it running, for looking at
    python3 vm/e2e.py --case grace    one case, by a piece of its name
"""

import argparse
import json
import os
import re
import shlex
import socket
import subprocess
import sys
import time
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HERE = Path(__file__).resolve().parent


class Blocked(Exception):
    """A prerequisite that is not there.

    A missing prerequisite is an explicit `blocked`, never a case
    quietly skipped. A suite that prints green because it did nothing is worse
    than one that prints red.
    """


class Failed(Exception):
    """An assertion that did not hold."""


def say(line=""):
    print(line, flush=True)


def run(argv, **kwargs):
    return subprocess.run(argv, text=True, capture_output=True, **kwargs)


# -- the machine --------------------------------------------------------------


class VM:
    # Handed to the cases through the object they are given, rather than
    # imported. A case loaded by path would import this file a second time and
    # raise a `Failed` the harness's own `except Failed` would not catch -- one
    # class, two names, and every failure printing as a crash.
    Failed = Failed
    Blocked = Blocked

    def __init__(self, manifest, machine, pace):
        self.manifest = manifest
        # Which of the two machines this run is about. `poc` is disposable and is
        # where the teeth are exercised; `omarchy` is the owner's demonstration
        # machine and is put back exactly as it was found.
        self.machine = machine
        self.about = manifest["machines"][machine]
        # Every second this run is willing to wait, and every second of budget it
        # will seed. One dictionary, chosen once by `--pace`, and no case holds a
        # duration of its own.
        self.pace = pace
        self.domain = self.about["name"]
        self.uri = self.about["uri"]
        self.hostname = self.about["hostname"]
        self.key = str(Path(self.about["key"]).expanduser())
        self.operator = self.about["operator"]
        self.subject = self.about["subject"]
        self.uid = self.about["uid"]
        self.disposable = self.about["disposable"]
        self.address = None
        self.log = []
        # What was on the machine before this run touched it, for a machine that
        # is not disposable. Filled by `remember_the_state` and undone by
        # `put_the_state_back`.
        self.remembered = None

    # -- libvirt ------------------------------------------------------------

    def virsh(self, *args, check=True):
        done = run(["virsh", "--connect", self.uri, *args])
        if check and done.returncode != 0:
            raise Blocked(f"virsh {' '.join(args)}: {done.stderr.strip()}")
        return done.stdout

    def prove_it_is_the_right_machine(self):
        """Check 1 and 2. Check 3 waits until there is an ssh to ask over."""
        listed = self.virsh("list", "--all")
        if self.domain not in listed:
            raise Blocked(
                f"there is no libvirt domain called {self.domain} on {self.uri}.\n"
                "        It is not recreated here: installing it again is twenty\n"
                "        minutes of manual work. `vm/provision-omarchy.sh` is the\n"
                "        script that built it.")

        disks = self.virsh("domblklist", self.domain)
        expected = self.about["disk"]
        if expected not in disks:
            raise Blocked(
                f"{self.domain} is not running from {expected}.\n"
                f"        It has:\n{disks}\n"
                "        A domain with the right name and the wrong disk is not the\n"
                "        machine this suite was written for, and it is not touched.")

        if not Path(self.key).exists():
            raise Blocked(f"there is no ssh key at {self.key}")

    def start(self, timeout=None):
        timeout = timeout or self.pace["boot_seconds"]
        if "running" not in self.virsh("domstate", self.domain):
            say(f"  starting {self.domain}")
            self.virsh("start", self.domain)

        deadline = time.time() + timeout
        while time.time() < deadline:
            for line in self.virsh("domifaddr", self.domain, check=False).splitlines():
                found = re.search(r"ipv4\s+(\d+\.\d+\.\d+\.\d+)/", line)
                if found:
                    self.address = found.group(1)
                    break
            if self.address:
                break
            time.sleep(2)
        if not self.address:
            raise Blocked(f"{self.domain} never took an address in {timeout}s")

        # Check 2: whatever answered, it is not this machine.
        mine = {info[4][0] for info in socket.getaddrinfo(socket.gethostname(), None)}
        mine.add("127.0.0.1")
        if self.address in mine:
            raise Blocked(f"{self.address} is an address of this machine")

        say(f"  {self.domain} is at {self.address}")
        self.wait_for_ssh(deadline)

        # Check 3, and the one that counts: the machine on the far side of the
        # ssh says who it is.
        said = self.ssh("uname -n").strip()
        if said != self.hostname:
            raise Blocked(
                f"the machine at {self.address} calls itself {said!r} and not "
                f"{self.hostname!r}")
        say(f"  it is {said}, and it is not this machine")

    def wait_for_ssh(self, deadline):
        while time.time() < deadline:
            if self.ssh("true", check=False)[0] == 0:
                return
            time.sleep(2)
        raise Blocked(f"{self.address} never answered ssh")

    def shutdown(self):
        say(f"  shutting {self.domain} down")
        self.virsh("shutdown", self.domain, check=False)
        # Not a window this suite is about -- nothing is being proved while a
        # machine powers off -- so it takes the regime's ordinary patience and
        # pulls the plug after it.
        deadline = time.time() + self.pace["patience_seconds"]
        while time.time() < deadline:
            if "shut off" in self.virsh("domstate", self.domain, check=False):
                return True
            time.sleep(2)
        self.virsh("destroy", self.domain, check=False)
        return False

    # -- talking to it ------------------------------------------------------

    def _ssh_argv(self, command):
        return [
            "ssh", "-i", self.key,
            "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null",
            "-o", "LogLevel=ERROR",
            "-o", "ConnectTimeout=10",
            "-o", "BatchMode=yes",
            f"{self.operator}@{self.address}",
            command,
        ]

    def ssh(self, command, check=True, timeout=120):
        if self.address is None:
            raise Blocked("no address yet")
        done = run(self._ssh_argv(command), timeout=timeout)
        self.log.append((command, done.returncode, done.stdout, done.stderr))
        if check and done.returncode != 0:
            raise Failed(
                f"{command}\n    exit {done.returncode}\n    {done.stdout}{done.stderr}")
        return done.stdout if check else (done.returncode, done.stdout + done.stderr)

    def root(self, command, **kwargs):
        return self.ssh(f"sudo {command}", **kwargs)

    def julia(self, command, **kwargs):
        """As the fiscalised account, inside its own session.

        The environment is the session's, because that is the whole point: a
        `uwsm app` launched without it lands in the wrong cgroup, and
        docs/design.md round 2 measured exactly that difference.
        """
        env = (f"XDG_RUNTIME_DIR=/run/user/{self.uid} WAYLAND_DISPLAY=wayland-1 "
               f"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/{self.uid}/bus")
        return self.root(f"-u {self.subject} env {env} {command}", **kwargs)

    def put(self, local, remote):
        done = run([
            "scp", "-i", self.key,
            "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null",
            "-o", "LogLevel=ERROR",
            str(local), f"{self.operator}@{self.address}:{remote}",
        ])
        if done.returncode != 0:
            raise Blocked(f"scp {local}: {done.stderr.strip()}")

    # -- reading the guest --------------------------------------------------

    @property
    def app_slice(self):
        return (f"/sys/fs/cgroup/user.slice/user-{self.uid}.slice"
                f"/user@{self.uid}.service/app.slice")

    @property
    def session_slice(self):
        return (f"/sys/fs/cgroup/user.slice/user-{self.uid}.slice"
                f"/user@{self.uid}.service/session.slice")

    def scope_paths(self):
        """Every `.scope` under the subject's app.slice, by full path.

        Paths and not names, and the names are derived here rather than by
        `find -printf %f`, because a unit name carries systemd's escaping --
        `app-uwsm-omahouse\\x2dpolite-abd0478c.scope` -- and `find -name` reads a
        backslash in its pattern as an escape. Asking the shell to match a name
        it is also parsing is how the first run of this suite looked at an app
        that was there and saw nothing in it.
        """
        found = self.ssh(
            f"find {shlex.quote(self.app_slice)} -maxdepth 2 -name '*.scope' "
            "-printf '%p\\n' 2>/dev/null || true", check=False)[1]
        return {path.rsplit("/", 1)[1]: path
                for path in found.splitlines() if path.endswith(".scope")}

    def scopes(self):
        return sorted(self.scope_paths())

    def scope_processes(self, unit):
        path = self.scope_paths().get(unit)
        if not path:
            return []
        found = self.ssh(f"cat {shlex.quote(path)}/cgroup.procs 2>/dev/null || true",
                         check=False)[1]
        return [line for line in found.split() if line.strip()]

    def pid_of(self, name):
        rc, out = self.ssh(f"pgrep -u {self.subject} -x {shlex.quote(name)}", check=False)
        return out.split()[0] if rc == 0 and out.split() else None

    def session_slice_pids(self):
        """Every pid in the subject's session.slice, by unit.

        This is what `session_slice_untouched` compares before and after, and it
        is read from the cgroup tree rather than from `pgrep` so that the thing
        asserted about is exactly the thing docs/design.md §5 says is never judged.
        """
        out = self.ssh(
            f"for unit in $(find {shlex.quote(self.session_slice)} -maxdepth 1 "
            "-mindepth 1 -type d -printf '%f\\n'); do "
            f"printf '%s %s\\n' \"$unit\" \"$(cat {shlex.quote(self.session_slice)}"
            "/$unit/cgroup.procs 2>/dev/null | tr '\\n' ',')\"; done", check=False)[1]
        units = {}
        for line in out.splitlines():
            parts = line.split()
            if parts:
                units[parts[0]] = sorted(p for p in (parts[1] if len(parts) > 1 else "")
                                         .split(",") if p)
        return units

    def sessions(self):
        """The subject's logind sessions, as ids, with their state."""
        rc, out = self.ssh(
            f"loginctl list-sessions --no-legend | awk '$3 == \"{self.subject}\"'",
            check=False)
        return [line.split()[0] for line in out.splitlines() if line.split()]

    def blocked(self):
        rc, out = self.ssh("cat /etc/omahouse/blocked 2>/dev/null", check=False)
        return [line.strip() for line in out.splitlines() if line.strip()]

    def today(self):
        return self.ssh("date +%F").strip()

    def journal(self, since=None, unit="omahouse"):
        when = f"--since '{since}'" if since else "--no-pager -n 200"
        return self.root(f"journalctl -u {unit} {when} --no-pager", check=False)[1]

    # -- driving it ---------------------------------------------------------

    def launch(self, app="omahouse-polite", args="900"):
        """One app in the subject's session, the way the Omarchy launchers do.

        `uwsm app --`, which is what docs/design.md round 2 measured putting a
        process in a scope of its own under `app.slice` -- and what a raw
        `hyprctl dispatch exec` was measured *not* doing. So the scope that comes
        back is `app-uwsm-<app>-<hex>.scope`, and its id is `<app>`, which is what
        a rule and a budget are written about.
        """
        before = set(self.scopes())
        self.julia(
            f"setsid --fork sh -c 'cd /tmp && exec uwsm app -- /usr/local/bin/{app} {args}' "
            "</dev/null >/dev/null 2>&1", check=False)
        for _ in range(self.pace["patience_seconds"] * 2):
            time.sleep(0.5)
            new = set(self.scopes()) - before
            if new:
                return sorted(new)[0]
        raise Failed(f"{app} never took a scope of its own under app.slice")

    def start_daemon(self):
        self.root("systemctl restart omahouse.service")

    def stop_daemon(self):
        self.root("systemctl stop omahouse.service", check=False)

    def reset(self):
        """A machine with no rules on it and nobody shut out.

        Between cases and never inside one. The `blocked` goes first, because a
        case that left a name in it would be a case that stopped the next one
        from ever logging in.
        """
        if not self.disposable:
            # The owner's machine. Its profile is the one it demonstrates from
            # and its ledger is a record; neither is this suite's to empty. What
            # a case leaves behind here is undone by `put_the_state_back`, from a
            # copy taken before anything ran.
            raise Blocked(f"{self.domain} is not disposable, and reset() empties "
                          "/etc/omahouse and /var/lib/omahouse")
        self.stop_daemon()
        self.root("rm -f /etc/omahouse/blocked /etc/omahouse/profiles.json")
        self.root("rm -rf /var/lib/omahouse/*")
        self.root(f"pkill -9 -u {self.subject} -f 'uwsm app|/usr/local/bin/omahouse-' "
                  "|| true", check=False)
        self.root(f"pkill -9 -u {self.subject} -x sleep || true", check=False)
        self.wait_for(lambda: not self.scopes(), self.pace["patience_seconds"],
                      "the app scopes to go")
        # And a session again. `logout_blocks_the_way_back` ends the one that
        # was there and the tty1 autologin brings up a new one, which takes the
        # notification daemon with it -- so every case starts from a session that
        # is up rather than from whatever the case before it left, and the order
        # they run in does not matter.
        wait_for_the_session(self)

    def make_profile(self, budgets, default="allow", rules=(), grace=None, warn_at=(1,)):
        """A profile, written through the verbs an operator would type.

        `grace` and `warnAt` have no verb of their own -- they are fields of
        docs/design.md §4 that this build's CLI does not expose -- so those two are
        patched into the file afterwards. Everything else goes through
        `omahouse`, which is the point: the rules under test are the ones a
        person could have written.
        """
        self.root(f"omahouse profile add {self.subject} --name Julia")
        self.root(f"omahouse profile default {self.subject} --{default}")
        for app in rules:
            self.root(f"omahouse allow {self.subject} {shlex.quote(app)}")
        for name, minutes in budgets.items():
            if name == "session":
                self.root(f"omahouse limit {self.subject} --session {minutes}m")
            else:
                self.root(f"omahouse limit {self.subject} "
                          f"--budget {shlex.quote(name)}={minutes}m")
        if grace is not None or warn_at:
            patch = json.dumps({"grace": grace, "warnAt": list(warn_at)})
            self.root(
                "python3 -c " + shlex.quote(
                    "import json,sys\n"
                    "p='/etc/omahouse/profiles.json'\n"
                    "d=json.load(open(p))\n"
                    f"patch=json.loads({patch!r})\n"
                    "for profile in d['profiles']:\n"
                    "    for key, value in patch.items():\n"
                    "        if value is not None:\n"
                    "            profile[key] = value\n"
                    "open(p,'w').write(json.dumps(d, indent=2))\n"))
        self.root(f"omahouse profile enforce {self.subject} --on")

    def whole_minutes(self, seconds):
        """The smallest whole-minute limit that can leave `seconds` on the clock.

        /etc/omahouse/profiles.json holds whole minutes -- docs/design.md §4 --
        and the regimes of `vm/manifest.toml` are counted in seconds. These two
        methods are where they meet, and they are here rather than in each case
        so that a regime asking for more than a minute cannot quietly produce a
        negative amount already spent.
        """
        return max(1, -(-seconds // 60))

    def already_spent(self, seconds):
        """What today has to have on it for `seconds` to be left of that limit."""
        return self.whole_minutes(seconds) * 60 - seconds

    def seed_ledger(self, spent):
        """A day that has already been going on for a while.

        The limits in profiles.json are whole minutes, and the cases want
        budgets of seconds. `whole_minutes` and `already_spent` above are how the
        two meet: a one minute budget with fifty-two seconds already on it has
        eight seconds left, and it is the same shape as a machine that has been
        on since lunch.
        """
        day = self.today()
        document = json.dumps({
            "schemaVersion": 1,
            "user": self.subject,
            "date": day,
            "budgets": spent,
            "grants": [],
            "events": [],
        })
        self.root(f"install -d -m 0755 /var/lib/omahouse/{self.subject}")
        self.root("tee /var/lib/omahouse/%s/%s.json > /dev/null <<'OMAHOUSE_LEDGER'\n%s\n"
                  "OMAHOUSE_LEDGER" % (self.subject, day, document))

    # -- putting a machine that is not disposable back ----------------------

    def remember_the_state(self):
        """Everything this run is about to change, as bytes, before it changes.

        Only for a machine that is not disposable. `.temp/docs/vm-runbook.md` §7
        restores the demonstration VM by retyping the verbs that made it, which
        is a restoration of what somebody remembered to write down; this is the
        file. A profile put back byte for byte is a profile that cannot come back
        subtly different from the one the owner demonstrates with.
        """
        self.remembered = {
            "profiles": self.root("cat /etc/omahouse/profiles.json 2>/dev/null || true",
                                  check=False)[1],
            "ledger": self.root(
                f"cat /var/lib/omahouse/{self.subject}/{self.today()}.json 2>/dev/null "
                "|| true", check=False)[1],
            "sddm": self.root("cat /var/lib/sddm/state.conf 2>/dev/null || true",
                              check=False)[1],
        }

    def put_the_state_back(self):
        """The copy above, and everything this run installed, taken off again.

        Best effort and loud about what it could not do: a machine left half
        restored is worse than one nobody touched, so what failed has to be
        readable rather than swallowed.
        """
        if self.remembered is None:
            return
        say("  putting the machine back")
        self.stop_daemon()

        # The browser half of docs/design.md §5.2: the force-install policy, the
        # archive it names, the native messaging manifest and the extension in
        # the subject's own profile. Every one of them is a restriction or a
        # thing that reports, and none of them may outlive this run.
        self.root("rm -f /etc/chromium/policies/managed/omahouse-meter.json "
                  "/etc/chromium/native-messaging-hosts/com.omahouse.meter.json "
                  "/usr/share/omahouse/chromium/omahouse-meter.crx "
                  "/usr/share/omahouse/chromium/updates.xml", check=False)
        self.root("rmdir /usr/share/omahouse/chromium /usr/share/omahouse "
                  "/etc/chromium/native-messaging-hosts 2>/dev/null || true", check=False)
        self.root(f"pkill -u {self.subject} -x chromium || true", check=False)
        self.root(f"rm -rf /home/{self.subject}/.config/chromium/Default/Extensions "
                  f"/run/user/{self.uid}/omahouse", check=False)

        # The virtual keyboard, which is a test tool and does not stay.
        self.root("systemctl stop ydotoold; systemctl reset-failed ydotoold; "
                  "rm -f /run/ydotoold.socket", check=False)
        self.root("pacman -Rns --noconfirm ydotool", check=False)

        # And the state, from the copy. `blocked` goes first, because a name left
        # in it is somebody locked out of the machine.
        self.root("rm -f /etc/omahouse/blocked", check=False)
        for what, path in (
                ("profiles", "/etc/omahouse/profiles.json"),
                ("ledger", f"/var/lib/omahouse/{self.subject}/{self.today()}.json"),
                ("sddm", "/var/lib/sddm/state.conf"),
        ):
            was = self.remembered.get(what, "")
            if was.strip():
                self.root("tee %s > /dev/null <<'OMAHOUSE_WAS'\n%s\nOMAHOUSE_WAS"
                          % (path, was.rstrip("\n")), check=False)
            else:
                self.root(f"rm -f {path}", check=False)

        # The session this run logged in, ended, so the greeter is what the next
        # person sees -- which is where the machine was found.
        self.root(f"loginctl terminate-user {self.subject} || true", check=False)
        self.root("systemctl restart sddm", check=False)

    # -- typing at it -------------------------------------------------------
    #
    # Only the machine with a greeter has these, and only because there is no
    # keyboard on the far side of an ssh. `.temp/docs/vm-runbook.md` §5 is where
    # the key codes come from: `<keycode>:<1 down|0 up>`, modifier down first and
    # up last, nesting closing from the inside out.

    def press(self, keys):
        self.root("env YDOTOOL_SOCKET=/run/ydotoold.socket ydotool key " + keys,
                  check=False)

    def type_text(self, text):
        # `--key-delay 25` because the default sends events faster than some text
        # boxes read them, and 25 ms was measured getting through SDDM's whole
        # password field without losing a key.
        self.root("env YDOTOOL_SOCKET=/run/ydotoold.socket ydotool type --key-delay 25 "
                  + shlex.quote(text), check=False)

    def wait_for(self, predicate, seconds, what):
        deadline = time.time() + seconds
        while time.time() < deadline:
            if predicate():
                return time.time()
            time.sleep(0.5)
        raise Failed(f"waited {seconds}s for {what} and it never happened")

    def wait_while(self, predicate, seconds, what):
        return self.wait_for(lambda: not predicate(), seconds, what)


# -- putting the build on it --------------------------------------------------


def deploy(vm):
    if vm.machine == "omarchy":
        return deploy_on_omarchy(vm)
    binary = ROOT / "build/bin/omahouse"
    if not binary.exists():
        raise Blocked(f"{binary} is not built. Run `mise run build` first.")

    # Same Qt, same glibc, or the copy is pointless. Checked rather than assumed,
    # because "it segfaults on the guest" is a bad way to learn it.
    guest_qt = vm.ssh("pacman -Q qt6-base 2>/dev/null || true", check=False)[1].split()
    if not guest_qt:
        raise Blocked("the guest has no qt6-base. `sudo pacman -S --needed qt6-base`")
    host_qt = run(["pacman", "-Q", "qt6-base"]).stdout.split()
    if host_qt and guest_qt[1].split("-")[0] != host_qt[1].split("-")[0]:
        raise Blocked(f"qt6-base is {host_qt[1]} here and {guest_qt[1]} there; "
                      "build on the guest instead of copying")

    say("  installing the build and the packaging")
    vm.put(binary, "/tmp/omahouse")
    vm.root("install -Dm755 /tmp/omahouse /usr/bin/omahouse")

    # The fake apps. The allowlist matches the identity of a
    # systemd scope, so a browser and a one line shell script are the same thing
    # to `Proc` -- and the two of them are not interchangeable: one leaves on its
    # SIGTERM and one does not, which is the only way to exercise both halves of
    # docs/design.md §5's sequence.
    for fixture in sorted((HERE / "fixtures").iterdir()):
        vm.put(fixture, f"/tmp/{fixture.name}")
        vm.root(f"install -Dm755 /tmp/{fixture.name} /usr/local/bin/{fixture.name}")

    packaging = ROOT / "packaging"
    for name, target in (
            ("omahouse.service", "/usr/lib/systemd/system/omahouse.service"),
            ("org.omarchy.omahouse.policy",
             "/usr/share/polkit-1/actions/org.omarchy.omahouse.policy"),
            ("omahouse.desktop", "/usr/share/applications/omahouse.desktop"),
            ("omahouse.svg", "/usr/share/icons/hicolor/scalable/apps/omahouse.svg"),
    ):
        vm.put(packaging / name, f"/tmp/{name}")
        vm.root(f"install -Dm644 /tmp/{name} {target}")

    # The scriptlet, run the way pacman runs it. This is the case for the
    # `.install`: the PAM line, the two directories and the enable, on a machine
    # that already has the line from docs/design.md round 3 -- so it is also the
    # proof that running it twice changes nothing.
    vm.put(packaging / "omahouse.install", "/tmp/omahouse.install")
    vm.root("systemctl daemon-reload")
    vm.root("bash -c '. /tmp/omahouse.install; post_install'")


def wait_for_the_session(vm, timeout=None):
    if vm.machine == "omarchy":
        return log_in_on_omarchy(vm)

    """Hyprland up, and the notification daemon with it.

    `sudo modprobe vkms` after every boot: it is the virtual GPU Hyprland draws
    on, and without it the graphical session does not come up at all. Asked for
    every run and not only the first, because it is a module and a boot forgets.
    """
    vm.root("modprobe vkms", check=False)
    deadline = time.time() + (timeout or vm.pace["boot_seconds"])
    while time.time() < deadline:
        if vm.pid_of("Hyprland"):
            break
        time.sleep(2)
    else:
        raise Blocked("Hyprland never came up. `sudo modprobe vkms` and look at "
                      "`journalctl -u getty@tty1` on the guest.")

    # mako, because `grace_warns_before_closing` asserts a notification arrived
    # and there has to be something on the far side of the bus to receive it.
    if not vm.pid_of("mako"):
        vm.julia("setsid --fork sh -c 'cd /tmp && exec mako' </dev/null >/dev/null 2>&1",
                 check=False)
        time.sleep(2)
    if not vm.pid_of("mako"):
        raise Blocked("mako would not start in the subject's session")

    # And the session plumbing, started by hand. The VM is not a full
    # Omarchy -- there is no quickshell on it -- but pipewire is the other unit
    # `session_slice_untouched` is about, and a case that only ever saw the
    # compositor would be a weaker case than the one that was asked for.
    vm.julia("systemctl --user start pipewire.service wireplumber.service", check=False)

    if not vm.sessions():
        raise Blocked(f"{vm.subject} has no logind session")


# -- the cases ----------------------------------------------------------------




# -- the machine with a browser on it -----------------------------------------
#
# Real Omarchy, SDDM instead of an autologin, and a `bochs` framebuffer the VNC
# and `virsh screenshot` can both see. The browser case of docs/design.md §5.2
# needs all three and the disposable machine has none of them.
#
# Everything below installs something or logs somebody in, and every one of them
# is undone by `VM.put_the_state_back`.


def keyboard(vm):
    """A virtual keyboard on the guest's own seat, through `uinput`.

    There is no keyboard on the far side of an ssh, and SDDM wants a password.
    `ydotool` makes an input device the guest's `seat0` accepts like any other,
    which both the greeter and Hyprland then receive. It is a test tool and it
    does not stay: `put_the_state_back` removes the package and the unit.

    `--socket-path` is not a flourish. Without it the daemon puts its socket in
    /tmp with mode 0600 and the client under `sudo` looks somewhere else, and the
    failure mode is `ydotool` exiting 0 with nothing happening on the screen --
    which reads as the keystroke being wrong rather than the socket being missing.
    """
    vm.root("pacman -S --noconfirm --needed ydotool", check=False)
    if vm.ssh("pgrep -x ydotoold", check=False)[0] != 0:
        vm.root("systemd-run --unit=ydotoold --description='ydotool (omahouse suite)' "
                "/usr/bin/ydotoold --socket-path=/run/ydotoold.socket", check=False)
        vm.wait_for(lambda: vm.ssh("pgrep -x ydotoold", check=False)[0] == 0,
                    vm.pace["patience_seconds"], "ydotoold to come up")
    # The device really on the seat, and not merely a daemon that started. This
    # is the check that turns the expensive silent failure into a `blocked`.
    vm.wait_for(
        lambda: "ydotoold virtual device" in vm.root("loginctl seat-status seat0",
                                                     check=False)[1],
        vm.pace["patience_seconds"], "the virtual keyboard to appear on seat0")


def log_in_on_omarchy(vm):
    """The subject's real session, entered through the greeter with a password.

    Not an autologin. This is the machine the owner demonstrates from and it asks
    for a password like anybody's would, so the suite types one -- which is also
    what makes the session under test the same session a person would get.
    """
    keyboard(vm)
    if vm.pid_of("Hyprland"):
        return

    # SDDM's `state.conf` decides whose name the greeter is asking about, and the
    # greeter does not show it. It is checked rather than assumed: typing julia's
    # password at a greeter asking about howl is a failed login and a confusing
    # screenshot.
    state = vm.root("cat /var/lib/sddm/state.conf", check=False)[1]
    if f"User={vm.subject}" not in state:
        raise Blocked(f"SDDM is asking about somebody other than {vm.subject}:\n{state}")

    vm.type_text(vm.about["password"])
    vm.press("28:1 28:0")
    deadline = time.time() + vm.pace["boot_seconds"]
    while time.time() < deadline:
        if vm.pid_of("Hyprland"):
            break
        time.sleep(2)
    else:
        raise Blocked("the greeter never let the session through. Look at "
                      "`journalctl -u sddm` on the guest, and at a screenshot.")
    if not vm.sessions():
        raise Blocked(f"{vm.subject} has no logind session")
    # The bar, the notification daemon and the rest of a real session, which take
    # a few seconds after the compositor.
    vm.wait_for(lambda: vm.ssh(f"pgrep -u {vm.subject} -x quickshell", check=False)[0] == 0,
                vm.pace["patience_seconds"], "the Omarchy shell to come up")


def deploy_on_omarchy(vm):
    """The build, and the browser half of docs/design.md §5.2.

    The `.crx` is signed **on this machine** and copied over, so the private key
    never leaves the developer's laptop -- `extension/pack.sh` says where it
    lives and what it would be in production. The guest gets an archive and a
    public id, which is all a machine ever needs.
    """
    binary = ROOT / "build/bin/omahouse"
    if not binary.exists():
        raise Blocked(f"{binary} is not built. Run `mise run build` first.")
    guest_qt = vm.ssh("pacman -Q qt6-base 2>/dev/null || true", check=False)[1].split()
    host_qt = run(["pacman", "-Q", "qt6-base"]).stdout.split()
    if guest_qt and host_qt and guest_qt[1].split("-")[0] != host_qt[1].split("-")[0]:
        raise Blocked(f"qt6-base is {host_qt[1]} here and {guest_qt[1]} there")

    say("  installing the build")
    vm.put(binary, "/tmp/omahouse")
    vm.root("install -Dm755 /tmp/omahouse /usr/bin/omahouse")
    vm.put(ROOT / "packaging/omahouse.service",
           "/tmp/omahouse.service")
    vm.root("install -Dm644 /tmp/omahouse.service "
            "/usr/lib/systemd/system/omahouse.service")
    vm.root("systemctl daemon-reload")

    say("  signing the meter and putting it on the machine")
    packed = Path(run(["mktemp", "-d", "/tmp/omahouse-meter-out.XXXXXX"]).stdout.strip())
    done = run([str(ROOT / "extension/pack.sh"), str(packed)])
    if done.returncode != 0:
        raise Blocked("extension/pack.sh: " + (done.stderr.strip() or done.stdout.strip()))
    say("    " + " · ".join(line.strip() for line in done.stdout.splitlines()))

    vm.put(packed / "omahouse-meter.crx", "/tmp/omahouse-meter.crx")
    vm.put(packed / "updates.xml", "/tmp/updates.xml")
    vm.root("install -Dm644 /tmp/omahouse-meter.crx "
            "/usr/share/omahouse/chromium/omahouse-meter.crx")
    vm.root("install -Dm644 /tmp/updates.xml /usr/share/omahouse/chromium/updates.xml")

    for name, target in (
            ("omahouse-meter-host", "/usr/lib/omahouse/meter-host"),
            ("com.omahouse.meter.json",
             "/etc/chromium/native-messaging-hosts/com.omahouse.meter.json"),
            ("omahouse-meter-policy.json",
             "/etc/chromium/policies/managed/omahouse-meter.json"),
    ):
        vm.put(ROOT / "packaging" / name, f"/tmp/{name}")
        mode = "0755" if name == "omahouse-meter-host" else "0644"
        vm.root(f"install -Dm{mode} /tmp/{name} {target}")


def load_cases(wanted, machine):
    import importlib.util

    cases = []
    for path in sorted((HERE / "cases").glob("*.py")):
        if path.name.startswith("_"):
            continue
        if wanted and wanted not in path.stem:
            continue
        spec = importlib.util.spec_from_file_location(path.stem, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        # A case says which machine it needs, and one that does not say is a
        # `poc` case. This is what makes it impossible to run the cases that
        # empty /var/lib/omahouse and end a login against the owner's own VM: the
        # filter is here, before a single one of them is even imported into a run
        # that is pointed at it.
        if getattr(module, "MACHINE", "poc") != machine:
            continue
        cases.append((path.stem, module))
    if not cases:
        raise Blocked(f"no case matches {wanted!r} on {machine}")
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keep", action="store_true",
                        help="leave the machine running afterwards")
    parser.add_argument("--case", default=None, help="run the cases whose name holds this")
    # The one knob. Every second this run waits on and every second of budget it
    # seeds comes out of the regime this picks, and out of nowhere else -- a case
    # with a number of its own would be a case nobody could cost from the
    # manifest.
    parser.add_argument("--pace", default="quick", choices=("quick", "long"),
                        help="quick (the default, for iterating) or long (for publishing)")
    # Which machine, and so which cases. The two are one choice: a case declares
    # the machine it needs and is not loaded for the other, so there is no way to
    # run the disposable machine's cases -- which empty /var/lib/omahouse and end
    # a login -- against the owner's demonstration VM.
    parser.add_argument("--machine", default="poc", choices=("poc", "omarchy"),
                        help="poc (the default, disposable) or omarchy (real Omarchy, "
                             "with a browser, put back as it was found)")
    options = parser.parse_args()

    with open(HERE / "manifest.toml", "rb") as file:
        manifest = tomllib.load(file)

    pace = manifest["pace"][options.pace]

    os.environ.setdefault("LIBVIRT_DEFAULT_URI",
                          manifest["machines"][options.machine]["uri"])
    vm = VM(manifest, options.machine, pace)

    say(f"omahouse — the VM suite, {vm.domain}, {pace['label']} pace")
    say()

    results = []
    started = False
    began = time.time()
    try:
        vm.prove_it_is_the_right_machine()
        vm.start()
        started = True
        # The copy first, before a single thing is installed or logged in. A
        # machine that is not disposable is put back from this and never from
        # what somebody remembered to write down.
        if not vm.disposable:
            vm.remember_the_state()
        wait_for_the_session(vm)
        deploy(vm)
        say()

        for name, case in load_cases(options.case, options.machine):
            say(f"  {name}")
            say(f"    {case.WHY}")
            if vm.disposable:
                vm.reset()
            begin = time.time()
            try:
                case.run(vm)
            except Failed as failure:
                results.append((name, "FAIL", str(failure)))
                say(f"    FAIL  {failure}")
            except Blocked as blocked:
                results.append((name, "BLOCKED", str(blocked)))
                say(f"    BLOCKED  {blocked}")
            else:
                results.append((name, "PASS", f"{time.time() - begin:.0f}s"))
                say(f"    PASS  in {time.time() - begin:.0f}s")
            say()
    except Blocked as blocked:
        say(f"  BLOCKED  {blocked}")
        results.append(("the run itself", "BLOCKED", str(blocked)))
    finally:
        # The report comes after the cleanup, never before.
        if started:
            try:
                if vm.disposable:
                    vm.reset()
                else:
                    vm.put_the_state_back()
            except Exception as problem:  # noqa: BLE001 -- cleanup is best effort
                say(f"  could not put the machine back: {problem}")
            if options.keep:
                say(f"  leaving {vm.domain} running at {vm.address}")
            else:
                vm.shutdown()

    say()
    say("  " + "-" * 60)
    for name, verdict, detail in results:
        say(f"  {verdict:8} {name}   {detail if verdict != 'PASS' else detail}")
    failures = [one for one in results if one[1] != "PASS"]
    # The wall clock of the whole run, said out loud. It is the number the two
    # regimes exist to trade against each other, and a regime whose cost nobody
    # prints is a regime nobody chooses on purpose.
    say(f"  {len(results) - len(failures)}/{len(results)} passed, "
        f"{pace['label']} pace, {time.time() - began:.0f}s in all")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
