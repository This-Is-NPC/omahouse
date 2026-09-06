"""`leave` on a real machine, and the daemon acting on what it wrote.

The CLI suite proves the arithmetic. What it cannot prove is the thing the verb
exists for: that a number pushed down from outside is a number the loop obeys.
A consolidation that writes a smaller day and watches the machine spend the old
one is worse than no consolidation, because it reads as working.

So this leaves less time than stands, and then waits for the daemon to close
the app on the smaller number.
"""

import json
import time

MACHINE = "poc"
WHY = "a day set from outside is a day the daemon closes on"


def run(vm):
    Failed = vm.Failed
    child = vm.subject

    # Ten minutes of an app, with nine already spent: one minute stands.
    vm.make_profile({"omahouse-polite": 10}, default="allow",
                    rules=("omahouse-polite",), grace=0)
    vm.seed_ledger({"omahouse-polite": 540})
    vm.root(f"omahouse profile enforce {child} --on")

    def left():
        day = json.loads(vm.root(f"omahouse status {child} --json"))
        return [b for b in day["budgets"]
                if b["id"] == "omahouse-polite"][0]["leftSeconds"]

    if not 30 <= left() <= 90:
        raise Failed(f"the day did not start where it was seeded: {left()}s left")

    # Said twice, and the second time writes nothing. This is the property the
    # verb exists for and it is asserted on a real ledger, not a temporary one.
    vm.root(f"omahouse leave {child} --budget omahouse-polite=5m")
    before = len(json.loads(vm.root(f"omahouse report {child} --json"))["days"][0]["grants"])
    vm.root(f"omahouse leave {child} --budget omahouse-polite=5m")
    after = len(json.loads(vm.root(f"omahouse report {child} --json"))["days"][0]["grants"])
    if after != before:
        raise Failed(f"saying it twice wrote twice: {before} then {after} grants")
    print(f"      five minutes left, and saying it again wrote nothing ({after} grant)")

    # Now the part only a machine can answer. The app is opened with five
    # minutes standing, and then the day is cut to nothing from outside.
    scope = vm.launch()
    vm.start_daemon()
    vm.wait_for(lambda: bool(vm.scope_processes(scope)), vm.pace["patience_seconds"],
                "the app to be counted")

    vm.root(f"omahouse leave {child} --budget omahouse-polite=1m")
    print(f"      {scope.split('-')[2]} is open, and the day was cut to one minute")

    # A minute of wall clock, and not the suite's patience. `leave` cannot be
    # told zero -- no time at all is not a length of time -- so the smallest day
    # it can write is one minute, and the machine has to actually spend it. This
    # waits on time passing rather than on a machine being slow, which is why
    # the number is here and not in the pace.
    began = time.time()
    deadline = began + 150
    while time.time() < deadline:
        if not vm.scope_processes(scope):
            print(f"      the daemon closed it {time.time() - began:.0f}s later, "
                  "on the number it was given")
            return
        time.sleep(1)
    raise Failed("the day was cut and the daemon went on spending the old one")
