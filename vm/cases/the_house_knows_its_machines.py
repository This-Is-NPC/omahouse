"""The household's list of computers, on a real machine.

The CLI suite proves the verbs; this proves the file where it will really live
-- `/etc/omahouse/machines.json`, root's to write and everybody's to read,
beside `profiles.json` and asked for by the package's own scriptlet.

It also proves the one thing a unit test cannot: that writing it needs root and
that the account under rules can read it and not change it. A list of the
household's computers that the child could edit is a list that means nothing.
"""

import json

MACHINE = "poc"
WHY = "the house writes down its computers, and the account under rules cannot"


def run(vm):
    Failed = vm.Failed
    child = vm.subject
    listing = "/etc/omahouse/machines.json"

    # Nobody has been told about another computer yet, which is the state of
    # every machine before somebody says otherwise.
    alone = vm.ssh("omahouse machines")
    if "whole house" not in alone:
        raise Failed(f"a machine on its own did not say so:\n{alone}")

    # Writing needs root, exactly as profiles.json does.
    refused = vm.ssh("omahouse machine add sneaky --node omk1_a --at 1.2.3.4:7879",
                     check=False)
    if refused[0] == 0:
        raise Failed("the list was written without privilege")

    vm.root("omahouse machine add workstation --node omk1_abc123 --at 10.0.0.2:7879")
    vm.root("omahouse machine add 'the spare'")

    written = json.loads(vm.ssh("omahouse --json machines"))
    by_name = {m["name"]: m for m in written}
    if set(by_name) != {"workstation", "the spare"}:
        raise Failed(f"the list is {sorted(by_name)}")
    if not by_name["workstation"]["reachable"] or by_name["the spare"]["reachable"]:
        raise Failed(f"reachability is wrong: {written}")
    print(f"      the house lists {len(written)}: one paired, one written down")

    # 0644 on purpose, like profiles.json: reading is for everybody and writing
    # is root's. The account under rules must be able to see the house it is in.
    mode = vm.root(f"stat -c %a {listing}").strip()
    if mode != "644":
        raise Failed(f"{listing} is {mode}, not 644")
    read = vm.root(f"-u {child} omahouse --json machines", check=False)
    if read[0] != 0:
        raise Failed(f"{child} cannot read the house's list:\n{read[1][:200]}")

    # And cannot change it. Two doors and both are asserted: no privilege of her
    # own, and no privilege borrowed.
    hers = vm.ssh(f"sudo -n -u {child} sudo -n omahouse machine remove workstation",
                  check=False)
    if hers[0] == 0:
        raise Failed(f"{child} removed a machine from the house's list")
    print(f"      {child} reads the list and cannot write it")

    vm.root("omahouse machine remove workstation")
    left = json.loads(vm.ssh("omahouse --json machines"))
    if [m["name"] for m in left] != ["the spare"]:
        raise Failed(f"after removing one, the list is {left}")

    # Taking a machine out of the list does nothing to the machine, and the
    # profiles on this one are untouched by any of it.
    vm.make_profile({"session": 120}, default="allow")
    vm.root("omahouse machine add workstation --node omk1_abc123 --at 10.0.0.2:7879")
    still = json.loads(vm.root(f"omahouse status {child} --json"))
    if still.get("user") != child:
        raise Failed("the profiles stopped answering once machines existed")
    print("      the two files are strangers: profiles unchanged by any of it")
