"""The loop the whole fleet design exists for, on a real machine.

Read every machine's day, add it up, push the truth back. Each half has been
proved on its own -- `house` adds and `leave` sets -- and this is the two of
them closing, which is the only shape that matters: a consolidation that adds
correctly and cannot act is arithmetic, and one that acts on a number it did
not add is a guess.

The other machine's day is placed rather than fetched. Bringing days here is
transport and belongs to the Battery; what is asserted here is that omahouse
adds what it is given and enforces what it concludes.
"""

import json
import time

MACHINE = "poc"
WHY = "the house adds up two machines and the daemon closes on the total"


def run(vm):
    Failed = vm.Failed
    child = vm.subject
    today = vm.today()

    # Ten minutes of an app in the house, and this machine has spent none.
    vm.make_profile({"omahouse-polite": 10}, default="allow",
                    rules=("omahouse-polite",), grace=0)
    vm.root("omahouse machine add laptop --node omk1_a --at 10.0.0.2:7879")

    # The laptop spent nine of the ten. This machine knows nothing about that
    # until the day is put where it can read it.
    elsewhere = f"/var/lib/omahouse/elsewhere/laptop/{child}"
    vm.root(f"mkdir -p {elsewhere}")
    vm.root(f"tee {elsewhere}/{today}.json >/dev/null <<'DAY'\n"
            + json.dumps({"schemaVersion": 1, "user": child, "date": today,
                          "budgets": {"omahouse-polite": 540},
                          "grants": [], "events": [], "sites": {}, "presence": {}})
            + "\nDAY")

    house = json.loads(vm.root(f"omahouse --json house {child}"))
    budget = [b for b in house["budgets"] if b["id"] == "omahouse-polite"][0]
    if budget["totalSeconds"] != 540 or budget["leftSeconds"] != 60:
        raise Failed(f"the house did not add up: {budget}")
    print(f"      the house has spent {budget['totalSeconds']}s of "
          f"{budget['limitSeconds']}s; {budget['leftSeconds']}s stand")

    # This machine, on its own, still thinks the whole ten minutes are its own
    # to spend. That is correct and is the reason the loop exists.
    alone = json.loads(vm.root(f"omahouse status {child} --json"))
    mine = [b for b in alone["budgets"] if b["id"] == "omahouse-polite"][0]
    if mine["leftSeconds"] < 500:
        raise Failed(f"this machine already knew about the laptop: {mine}")
    print(f"      but this machine still thinks {mine['leftSeconds']}s are its own")

    # So the truth is pushed down. One minute is what the house has left, and
    # `leave` is what says so here.
    vm.root(f"omahouse leave {child} --budget omahouse-polite=1m")
    told = json.loads(vm.root(f"omahouse status {child} --json"))
    now = [b for b in told["budgets"] if b["id"] == "omahouse-polite"][0]
    if now["leftSeconds"] > 60:
        raise Failed(f"the machine was told and did not take it: {now}")
    print(f"      told: this machine now has {now['leftSeconds']}s")

    # And the daemon acts on it. This is the assertion the whole thing is for --
    # a number that was added on one machine, written on another, and obeyed.
    scope = vm.launch()
    vm.root(f"omahouse profile enforce {child} --on")
    vm.start_daemon()

    began = time.time()
    # A minute of wall clock: `leave` cannot be told zero, so the smallest day
    # is one minute and the machine has to spend it.
    while time.time() - began < 150:
        if not vm.scope_processes(scope):
            print(f"      closed {time.time() - began:.0f}s later, on a number "
                  "this machine did not spend")
            return
        time.sleep(1)
    raise Failed("the house said one minute and the machine spent the old ten")
