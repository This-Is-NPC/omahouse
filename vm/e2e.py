#!/usr/bin/env python3
"""Layer 2 of testing.md: the teeth, in a machine that is not this one.

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

    python3 vm/e2e.py                 every case, then shut the machine down
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

    testing.md §6: a missing prerequisite is an explicit `blocked`, never a case
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

    def __init__(self, manifest):
        self.manifest = manifest
        self.domain = manifest["domain"]["name"]
        self.uri = manifest["domain"]["uri"]
        self.hostname = manifest["domain"]["hostname"]
        self.key = str(Path(manifest["access"]["key"]).expanduser())
        self.operator = manifest["access"]["operator"]
        self.subject = manifest["subject"]["user"]
        self.uid = manifest["subject"]["uid"]
        self.address = None
        self.log = []

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
                "        poc/findings.md round 2 made it, and it is the seed of the\n"
                "        prepared base of testing.md §3. It is not recreated here:\n"
                "        installing it again is twenty minutes of manual work.")

        disks = self.virsh("domblklist", self.domain)
        expected = self.manifest["domain"]["disk"]
        if expected not in disks:
            raise Blocked(
                f"{self.domain} is not running from {expected}.\n"
                f"        It has:\n{disks}\n"
                "        A domain with the right name and the wrong disk is not the\n"
                "        machine this suite was written for, and it is not touched.")

        if not Path(self.key).exists():
            raise Blocked(f"there is no ssh key at {self.key}")

    def start(self, timeout=180):
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
        deadline = time.time() + 90
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
        poc/findings.md round 2 measured exactly that difference.
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

        This is what `session_slice_intocada` compares before and after, and it
        is read from the cgroup tree rather than from `pgrep` so that the thing
        asserted about is exactly the thing spec.md §5 says is never judged.
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

        `uwsm app --`, which is what poc/findings.md round 2 measured putting a
        process in a scope of its own under `app.slice` -- and what a raw
        `hyprctl dispatch exec` was measured *not* doing. So the scope that comes
        back is `app-uwsm-<app>-<hex>.scope`, and its id is `<app>`, which is what
        a rule and a budget are written about.
        """
        before = set(self.scopes())
        self.julia(
            f"setsid --fork sh -c 'cd /tmp && exec uwsm app -- /usr/local/bin/{app} {args}' "
            "</dev/null >/dev/null 2>&1", check=False)
        for _ in range(20):
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
        self.stop_daemon()
        self.root("rm -f /etc/omahouse/blocked /etc/omahouse/profiles.json")
        self.root("rm -rf /var/lib/omahouse/*")
        self.root(f"pkill -9 -u {self.subject} -f 'uwsm app|/usr/local/bin/omahouse-' "
                  "|| true", check=False)
        self.root(f"pkill -9 -u {self.subject} -x sleep || true", check=False)
        self.wait_for(lambda: not self.scopes(), 20, "the app scopes to go")
        # And a session again. `logout_bloqueia_o_reingresso` ends the one that
        # was there and the tty1 autologin brings up a new one, which takes the
        # notification daemon with it -- so every case starts from a session that
        # is up rather than from whatever the case before it left, and the order
        # they run in does not matter.
        wait_for_the_session(self)

    def make_profile(self, budgets, default="allow", rules=(), grace=None, warn_at=(1,)):
        """A profile, written through the verbs an operator would type.

        `grace` and `warnAt` have no verb of their own -- they are fields of
        spec.md §4 that this build's CLI does not expose -- so those two are
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

    def seed_ledger(self, spent):
        """A day that has already been going on for a while.

        The limits in profiles.json are whole minutes, and testing.md asks for
        budgets of forty and fifteen seconds. This is how the two meet: a one
        minute budget with twenty seconds already on it has forty seconds left,
        and it is the same shape as a machine that has been on since lunch.
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

    # The fake apps of testing.md. The allowlist matches the identity of a
    # systemd scope, so a browser and a one line shell script are the same thing
    # to `Proc` -- and the two of them are not interchangeable: one leaves on its
    # SIGTERM and one does not, which is the only way to exercise both halves of
    # spec.md §5's sequence.
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
    # that already has the line from poc/findings.md round 3 -- so it is also the
    # proof that running it twice changes nothing.
    vm.put(packaging / "omahouse.install", "/tmp/omahouse.install")
    vm.root("systemctl daemon-reload")
    vm.root("bash -c '. /tmp/omahouse.install; post_install'")


def wait_for_the_session(vm, timeout=180):
    """Hyprland up, and the notification daemon with it.

    `sudo modprobe vkms` after every boot: it is the virtual GPU Hyprland draws
    on, and without it the graphical session does not come up at all. Asked for
    every run and not only the first, because it is a module and a boot forgets.
    """
    vm.root("modprobe vkms", check=False)
    deadline = time.time() + timeout
    while time.time() < deadline:
        if vm.pid_of("Hyprland"):
            break
        time.sleep(2)
    else:
        raise Blocked("Hyprland never came up. `sudo modprobe vkms` and look at "
                      "`journalctl -u getty@tty1` on the guest.")

    # mako, because `grace_avisa_antes_de_fechar` asserts a notification arrived
    # and there has to be something on the far side of the bus to receive it.
    if not vm.pid_of("mako"):
        vm.julia("setsid --fork sh -c 'cd /tmp && exec mako' </dev/null >/dev/null 2>&1",
                 check=False)
        time.sleep(2)
    if not vm.pid_of("mako"):
        raise Blocked("mako would not start in the subject's session")

    # And the session plumbing testing.md names by hand. The VM is not a full
    # Omarchy -- there is no quickshell on it -- but pipewire is the other unit
    # `session_slice_intocada` is about, and a case that only ever saw the
    # compositor would be a weaker case than the one that was asked for.
    vm.julia("systemctl --user start pipewire.service wireplumber.service", check=False)

    if not vm.sessions():
        raise Blocked(f"{vm.subject} has no logind session")


# -- the cases ----------------------------------------------------------------


def load_cases(wanted):
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
        cases.append((path.stem, module))
    if not cases:
        raise Blocked(f"no case matches {wanted!r}")
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keep", action="store_true",
                        help="leave the machine running afterwards")
    parser.add_argument("--case", default=None, help="run the cases whose name holds this")
    options = parser.parse_args()

    with open(HERE / "manifest.toml", "rb") as file:
        manifest = tomllib.load(file)

    os.environ.setdefault("LIBVIRT_DEFAULT_URI", manifest["domain"]["uri"])
    vm = VM(manifest)

    say(f"omahouse — layer 2, {manifest['domain']['name']}")
    say()

    results = []
    started = False
    try:
        vm.prove_it_is_the_right_machine()
        vm.start()
        started = True
        wait_for_the_session(vm)
        deploy(vm)
        say()

        for name, case in load_cases(options.case):
            say(f"  {name}")
            say(f"    {case.WHY}")
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
        # The report comes after the cleanup, never before -- testing.md §6.
        if started:
            try:
                vm.reset()
            except Exception as problem:  # noqa: BLE001 -- cleanup is best effort
                say(f"  could not reset the machine: {problem}")
            if options.keep:
                say(f"  leaving {vm.domain} running at {vm.address}")
            else:
                vm.shutdown()

    say()
    say("  " + "-" * 60)
    for name, verdict, detail in results:
        say(f"  {verdict:8} {name}   {detail if verdict != 'PASS' else detail}")
    failures = [one for one in results if one[1] != "PASS"]
    say(f"  {len(results) - len(failures)}/{len(results)} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
