"""Nobody is cut off cold -- spec.md §6.

The sequence is always warning, then `warnAt`, then `grace`, then the end, and
this asserts the two halves that only a real session can show: that the
notification arrived at a real notification daemon on the other side of a bus the
root daemon is not on, and that the app was still there when it did.

poc/findings.md round 2 measured the notification path -- `systemd-run --uid=
--setenv=DBUS_SESSION_BUS_ADDRESS=... notify-send`, with `makoctl list` on the
far side showing it. The same check is made here, against the daemon rather than
against a hand-typed command.
"""

import time

WHY = "the warning lands in the session's own notification daemon before anything closes"


def run(vm):
    Failed = vm.Failed
    # A clean slate on the notification daemon, so what is read afterwards is
    # what this case caused.
    vm.julia("makoctl dismiss --all", check=False)

    app = "omahouse-polite"
    unit = vm.launch(app)
    grace = vm.manifest["budgets"]["grace_seconds"]
    vm.make_profile(budgets={"session": 600, app: 1}, default="allow", rules=[app],
                    grace=grace, warn_at=(1,))
    vm.seed_ledger({app: 60 - vm.manifest["budgets"]["app_seconds"]})

    vm.start_daemon()

    # The mark of `warnAt: [1]` is crossed on the first tick, because there is
    # less than a minute left of a one minute budget.
    def warned():
        return "left" in vm.julia("makoctl list", check=False)[1]

    vm.wait_for(warned, 20, "the warning to reach the session")
    listed = vm.julia("makoctl list", check=False)[1]
    if unit not in vm.scopes():
        raise Failed(f"{unit} was already closed when the warning arrived:\n{listed}")

    # And the last word before the window, which is the one that says how long
    # is left. `Time is up` with `closes in N seconds` is the GraceStarted of
    # spec.md §6; it has to come out while the app is still open.
    def told_the_window():
        return "Time is up" in vm.julia("makoctl list", check=False)[1]

    vm.wait_for(told_the_window, 30, "the grace window to be announced")
    announced = time.time()
    window = vm.julia("makoctl list", check=False)[1]
    if unit not in vm.scopes():
        raise Failed("the app was closed before it was told the window had opened")

    vm.wait_for(lambda: unit not in vm.scopes(), 30, f"{unit} to close after the window")
    waited = time.time() - announced
    if waited < grace:
        raise Failed(f"the app closed {waited:.1f}s after `time is up`, and the window "
                     f"is {grace}s -- it was cut off inside its own grace")

    journal = vm.journal()
    for wanted in ("grace:", "SIGTERM into", "cgroup.kill on"):
        if wanted not in journal:
            raise Failed(f"the journal never says {wanted!r}:\n{journal}")

    print(f"      warned, then told `Time is up`, then closed {waited:.1f}s later "
          f"(window {grace}s)")
    print("      " + " | ".join(line.strip() for line in window.splitlines()
                                if line.strip())[:160])
