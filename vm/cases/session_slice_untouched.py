"""The case that justifies the whole layer.

This is not about time running out -- the arithmetic of that is proved in
milliseconds by the unit suite. It is about the one way
omahouse could fail that would end the product. A profile with `enforce: true`
and an empty allowlist means every app scope is refused and closed. If the model
of docs/design.md §5 is wrong about which cgroups those are, what happens is that
Hyprland goes down two seconds after somebody logs in, and the person is looking
at a greeter with no idea why.

Only a real graphical session proves it does not. The tree here is the one uwsm
lays out, the compositor in it is a real Hyprland on a real seat, and the daemon
under test is the one built from the working tree.

The stricter form is used: two apps are opened first, so the profile really does
bite something. A run where nothing was ever closed would prove nothing about
what closing reaches.
"""

import time

WHY = "enforce + empty allowlist, held, and the compositor never notices"


def run(vm):
    Failed = vm.Failed
    hyprland = vm.pid_of("Hyprland")
    if not hyprland:
        raise Failed("Hyprland is not running, so there is nothing to protect")

    # Everything docs/design.md §5 says is never judged, by unit and by pid.
    before = vm.session_slice_pids()
    for unit in ("wayland-wm@hyprland.desktop.service", "pipewire.service",
                 "dbus-broker.service"):
        if unit not in before or not before[unit]:
            raise Failed(f"{unit} has nothing in it, so there is nothing to protect: "
                         f"{sorted(before)}")
    sessions_before = vm.sessions()

    # And the user's own manager, which is the process the whole tree hangs off.
    manager = vm.ssh(f"systemctl show user@{vm.uid}.service -p MainPID --value").strip()

    apps = [vm.launch(args="900"), vm.launch(args="901")]

    # No budgets at all. Nothing runs out, so nothing here is about time: what is
    # being asked is what an allowlist reaches when it refuses everything.
    vm.make_profile(budgets={}, default="deny")
    vm.start_daemon()

    say = vm.ssh
    # The one window in this suite that cannot shrink to nothing and still mean
    # anything. What is being asked is whether an empty allowlist eventually
    # reaches something it must not, and "eventually" is the whole question, so
    # the quick regime buys its speed by giving up reach here rather than by
    # pretending. Twenty seconds is ten ticks of the daemon with every app scope
    # being refused; three minutes is the long regime's answer, and it is the one
    # to run before publishing.
    held = vm.pace["hold_seconds"]
    deadline = time.time() + held
    while time.time() < deadline:
        # Asserted the whole window through and not only at the end. A compositor
        # that died at second nine and was restarted by uwsm would look fine to a
        # check that only ran at the end of it.
        if vm.pid_of("Hyprland") != hyprland:
            raise Failed(f"Hyprland was {hyprland} and is now {vm.pid_of('Hyprland')}")
        time.sleep(3)

    after = vm.session_slice_pids()
    if after != before:
        raise Failed("session.slice changed under the daemon:\n"
                     f"    before {before}\n    after  {after}")

    now = say(f"systemctl show user@{vm.uid}.service -p MainPID --value").strip()
    if now != manager:
        raise Failed(f"the user manager was {manager} and is now {now}")

    if vm.sessions() != sessions_before:
        raise Failed(f"the sessions were {sessions_before} and are now {vm.sessions()}")
    state = say(f"loginctl show-session {sessions_before[0]} -p State --value").strip()
    if state != "active":
        raise Failed(f"session {sessions_before[0]} is {state!r} and not active")

    # And the apps really were refused, so the minute above was a minute of the
    # teeth being in rather than a minute of nothing happening.
    left = [unit for unit in vm.scopes() if unit in apps]
    if left:
        raise Failed(f"the allowlist refused nothing: {left} is still open")

    journal = vm.journal()
    if "cgroup.kill" not in journal:
        raise Failed("nothing was closed, so nothing was proved:\n" + journal)

    print("      Hyprland %s throughout %ss, user@%s.service %s, sessions %s active"
          % (hyprland, held, vm.uid, manager, sessions_before))
    for unit, pids in sorted(before.items()):
        print(f"      {unit}: {len(pids)} process(es), the same ones")
    print(f"      and both app scopes were closed: {apps}")
