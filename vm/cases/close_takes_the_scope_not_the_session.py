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

The limit is one minute with the pace's `app_seconds` left on it, which is what
the fixture wants. The clock is the real one, and how much of it this case is
given comes out of `vm/manifest.toml` -- eight seconds under the quick regime,
forty-five under the long one, where a `grace` a household would really set has
room to be seen.
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

    grace = vm.pace["grace_seconds"]
    limit = vm.whole_minutes(vm.pace["app_seconds"])
    spent = vm.already_spent(vm.pace["app_seconds"])
    vm.make_profile(
        budgets={"session": 600, "omahouse-polite": limit, "omahouse-stubborn": limit},
        default="allow", rules=["omahouse-polite", "omahouse-stubborn"], grace=grace)
    vm.seed_ledger({"omahouse-polite": spent, "omahouse-stubborn": spent})

    began = time.time()
    vm.start_daemon()

    # The budget, then the grace window, then the SIGTERM, then another window
    # before `cgroup.kill`. `patience_seconds` is the ceiling on the whole of it
    # and is never the thing under test: it only has to be longer than the
    # sequence the pace asked for.
    patience = vm.pace["patience_seconds"]
    vm.wait_for(lambda: polite not in vm.scopes(), patience, f"{polite} to be closed")
    polite_took = time.time() - began
    vm.wait_for(lambda: stubborn not in vm.scopes(), patience, f"{stubborn} to be closed")
    stubborn_took = time.time() - began

    if polite_took < vm.pace["app_seconds"]:
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
