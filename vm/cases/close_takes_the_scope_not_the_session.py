"""A budget runs out, the app closes, and the compositor does not notice.

docs/design.md round 2 measured the mechanism on its own: one write to a
scope's `cgroup.kill` took it from one process to none while Hyprland, pid 484,
carried on either side of it. This is the same write, arrived at the way it
really will be -- a budget in /etc/omahouse/profiles.json, a ledger under
/var/lib, a daemon under systemd, and nobody typing anything.

Two apps, because docs/design.md §5's sequence has two halves and one app can only show
one of them. `omahouse-polite` takes its SIGTERM and goes, which is the ordinary
ending and the one where `cgroup.kill` is never needed. `omahouse-stubborn`
ignores it, which is what makes the write necessary rather than decorative -- and
the first run of this suite is what found that out, because with only the polite
one the journal never said `cgroup.kill` at all.

The limit is one minute with forty-five seconds already spent, which is the
fifteen seconds the fixture wants. The clock is the real one.
"""

import time

WHY = "a spent budget closes the scope by SIGTERM, or by cgroup.kill when that is ignored"


def run(vm):
    Failed = vm.Failed
    hyprland = vm.pid_of("Hyprland")
    before = vm.session_slice_pids()

    polite = vm.launch("omahouse-polite")
    stubborn = vm.launch("omahouse-stubborn")
    pids = {polite: vm.scope_processes(polite), stubborn: vm.scope_processes(stubborn)}
    for unit, found in pids.items():
        if not found:
            raise Failed(f"{unit} has no processes in it")

    grace = vm.manifest["budgets"]["grace_seconds"]
    spent = 60 - vm.manifest["budgets"]["app_seconds"]
    vm.make_profile(
        budgets={"session": 600, "omahouse-polite": 1, "omahouse-stubborn": 1},
        default="allow", rules=["omahouse-polite", "omahouse-stubborn"], grace=grace)
    vm.seed_ledger({"omahouse-polite": spent, "omahouse-stubborn": spent})

    began = time.time()
    vm.start_daemon()

    # Fifteen seconds of budget, then three of grace, then the SIGTERM, then
    # three more before `cgroup.kill`. Sixty is room for the two second tick to
    # land where it lands.
    vm.wait_for(lambda: polite not in vm.scopes(), 60, f"{polite} to be closed")
    polite_took = time.time() - began
    vm.wait_for(lambda: stubborn not in vm.scopes(), 60, f"{stubborn} to be closed")
    stubborn_took = time.time() - began

    if polite_took < vm.manifest["budgets"]["app_seconds"]:
        raise Failed(f"{polite} went in {polite_took:.0f}s, before its budget ran out")
    if stubborn_took < polite_took:
        raise Failed("the app that ignored its SIGTERM went first, so the polite half "
                     "of the sequence is not happening")

    for unit, found in pids.items():
        for pid in found:
            rc, _ = vm.ssh(f"test -d /proc/{pid}", check=False)
            if rc == 0:
                raise Failed(f"pid {pid} of {unit} is still alive")

    if vm.pid_of("Hyprland") != hyprland:
        raise Failed(f"Hyprland was {hyprland} and is now {vm.pid_of('Hyprland')}")
    after = vm.session_slice_pids()
    if after != before:
        raise Failed(f"session.slice changed:\n    before {before}\n    after {after}")
    if not vm.sessions():
        raise Failed("the session went with the app")

    journal = vm.journal()
    if f"SIGTERM into omahouse-polite ({polite})" not in journal:
        raise Failed("the journal does not show the polite half:\n" + journal)
    if f"cgroup.kill on omahouse-stubborn ({stubborn})" not in journal:
        raise Failed("the journal does not show cgroup.kill, so the second half of "
                     "docs/design.md §5's sequence never ran:\n" + journal)
    # And the one that did leave on its signal was never written about, which is
    # the point of doing the polite half first.
    if f"cgroup.kill on omahouse-polite" in journal:
        raise Failed("the polite app was killed as well as asked")

    print(f"      {polite} gone {polite_took:.0f}s in, on its SIGTERM")
    print(f"      {stubborn} ignored it and went to cgroup.kill "
          f"{stubborn_took:.0f}s in")
    print(f"      Hyprland still {hyprland}; session.slice unchanged; "
          f"session {vm.sessions()} active")
