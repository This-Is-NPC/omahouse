"""The unit of spec.md §9, installed by the package and put back when it dies.

`Restart=always` is not tidiness. A stopped daemon is a rule switched off, and
the whole of spec.md §1's "the terminal does not trust itself" rests on the
counting outliving anything that happens to it. An account without privilege
cannot stop a system service, so the only way this ends is a crash -- and a crash
must not be how somebody gets their evening back.

So: the scriptlet's `systemctl enable` really enabled it, the unit really starts,
and a SIGKILL is answered by systemd with a new process inside `RestartSec`.
"""

import time

WHY = "the packaged unit starts, and a SIGKILL is answered by a new process"


def run(vm):
    Failed = vm.Failed
    enabled = vm.ssh("systemctl is-enabled omahouse.service", check=False)[1].strip()
    if enabled != "enabled":
        raise Failed(f"omahouse.service is {enabled!r} after the .install ran")

    unit = vm.ssh("systemctl cat omahouse.service")
    for wanted in ("ExecStart=/usr/bin/omahouse watch", "Restart=always", "RestartSec=2",
                   "After=systemd-logind.service", "WantedBy=multi-user.target"):
        if wanted not in unit:
            raise Failed(f"the installed unit has no {wanted!r}")

    # Something to watch, so the daemon has a reason to still be there.
    vm.make_profile(budgets={"session": 600}, default="allow")
    vm.start_daemon()
    vm.wait_for(lambda: vm.ssh("systemctl is-active omahouse.service",
                               check=False)[1].strip() == "active",
                30, "omahouse.service to be active")

    first = vm.ssh("systemctl show omahouse.service -p MainPID --value").strip()
    if first in ("", "0"):
        raise Failed("omahouse.service is active with no main process")

    vm.root("systemctl kill -s KILL omahouse.service")

    def raised():
        state = vm.ssh("systemctl is-active omahouse.service", check=False)[1].strip()
        now = vm.ssh("systemctl show omahouse.service -p MainPID --value").strip()
        return state == "active" and now not in ("", "0", first)

    began = time.time()
    vm.wait_for(raised, 30, "systemd to put the daemon back")
    second = vm.ssh("systemctl show omahouse.service -p MainPID --value").strip()

    restarts = vm.ssh("systemctl show omahouse.service -p NRestarts --value").strip()
    print(f"      pid {first} killed, back as {second} in {time.time() - began:.0f}s "
          f"(NRestarts={restarts})")

    # And it is counting again rather than merely running: the ledger of the day
    # is the thing that must not have a hole in it.
    vm.launch(args="900")
    day = vm.today()
    path = f"/var/lib/omahouse/{vm.subject}/{day}.json"
    vm.wait_for(lambda: vm.ssh(f"test -f {path}", check=False)[0] == 0, 20,
                "the ledger to be written again after the restart")
    print(f"      and counting again: {path}")
