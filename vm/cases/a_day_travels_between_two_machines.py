"""The transport, with nobody placing a file by hand.

`house` has been able to add up several machines' days since the day it was
written, and every proof of it so far put the other machines' days on disk with
a heredoc. This is the case where the days arrive on their own.

What runs is the Battery, not omahouse: a scheduled script on the operator's
computer that asks each machine for its day and pipes it into `collect`. That
is deliberate, and it is what the whole seam is for -- a person with `ssh` and a
pipe does the same thing, and if omahouse cannot tell the difference then the
transport is somewhere it can be replaced.

What has to hold:

  1. a machine publishes its day over its own console, fetched and not cued --
     Omakure's wire carries an exit code and a day is a document;
  2. the operator's computer files it, having checked whose it is;
  3. `house` then sums two machines with nobody having written a file;
  4. and a computer that is off is quiet rather than failed, because a laptop is
     shut most of the day and a job that goes red for that means nothing.
"""

import json

MACHINE = "poc"
WHY = "a day arrives on its own, and the house adds up two machines"

# omahouse's own defaults, from `renderNodeConfig`.
API, WIRE = 8787, 7879


def run(vm):
    dad = vm.peer("dad")

    # The operator's machine administers, so it needs omahouse. Taken off again
    # at the end, because `the_parent_administers` asserts the opposite about
    # this same machine and a copy left behind is that case failing later for a
    # reason nothing in it explains.
    dad.put(str(vm.build_binary), "/tmp/omahouse")
    dad.root("install -Dm755 /tmp/omahouse /usr/bin/omahouse")
    try:
        collected(vm, dad)
    finally:
        dad.root("rm -f /usr/bin/omahouse", check=False)


def collected(vm, dad):
    Failed = vm.Failed
    child = vm.subject
    here, there = vm.address, dad.address

    for box in (vm, dad):
        box.fresh_omakure()

    # -- paired, in the three commands a household would type ----------------
    invited = json.loads(dad.root(f"omahouse --json machine invite --at {there}:{WIRE} "
                                  "--name 'the study'"))
    prepared = json.loads(vm.root(
        f"omahouse --json machine prepare --invite {invited['invite']} "
        f"--at {here}:{WIRE} --name 'the kitchen laptop'"))
    added = json.loads(dad.root(
        f"omahouse --json machine add 'the kitchen laptop' --pair {prepared['pair']}"))
    if not added.get("trusted"):
        raise Failed(f"the pairing did not finish: {added}")

    # The console is on the network on the machine being administered, and only
    # there. It is the one way a day can come back, and the operator's own stays
    # on loopback because nobody pulls from it.
    if prepared["apiEndpoint"] != f"{here}:{API}":
        raise Failed(f"the prepared machine's console is at "
                     f"{prepared['apiEndpoint']!r}, not {here}:{API}")
    # In the unit and never in the config: Omakure will not load a `node.toml`
    # whose `api.bind` is not loopback, so the door is opened per machine by
    # whoever runs the service, which is the better place for it.
    opened = "/etc/systemd/system/omakure-node.service.d/omahouse.conf"
    if "--allow-non-loopback " not in vm.root(f"cat {opened}") + " ":
        raise Failed("the prepared machine's console was never opened to the house")
    if "--allow-non-loopback " in dad.root(f"cat {opened}") + " ":
        raise Failed("the operator's console is open, and nothing pulls from it")
    for box in (vm, dad):
        if "127.0.0.1" not in box.root("grep '^bind' /etc/omakure/node.toml"):
            raise Failed(f"{box.domain} wrote a console bind Omakure will refuse")

    # And the key to read it is on disk where only root can have it. This is the
    # one secret the pairing walk carries, so where it lands is worth asserting.
    keys = dad.root("omahouse machine token 'the kitchen laptop'").strip().splitlines()
    if keys[0] != f"{here}:{API}" or not keys[1].startswith("omk_"):
        raise Failed(f"the key to that machine is not what prepare handed over: {keys}")
    if dad.root("stat -c %a /etc/omahouse/machine-tokens.json").strip() != "600":
        raise Failed("the keys to the other machines are readable by somebody else")

    # Pairing installed the day and collection adapters in the service workspace.

    # -- a day on the machine under rules, and a profile on both -------------
    #
    # The profile has to be on the operator's machine too: `house` reads the
    # household's number from it, and the household's number is what a sum is
    # measured against.
    vm.make_profile({"session": 120}, default="allow")
    dad.root(f"omahouse profile add {child} --name Julia")
    dad.root(f"omahouse limit {child} --session 120m")
    vm.seed_ledger({"session": 1800})

    spent = json.loads(vm.root(f"omahouse --json day {child}"))
    if spent["budgets"].get("session") != 1800:
        raise Failed(f"the machine under rules does not have the day it was given: "
                     f"{spent}")

    # -- and the operator's computer fetches it ------------------------------
    #
    # Nothing between these two lines is omahouse. It is one Battery script,
    # curl and a pipe, which is the whole point of where the seam is.
    dad.ssh("command -v jq >/dev/null || sudo pacman -S --noconfirm --needed jq",
            timeout=300, check=False)
    # Through `bash`, which is how Omakure's own runner starts a script and so
    # is the path a scheduled run really takes. Not by exec, and not by choice:
    # `battery install` lands the file 0600, so the same command run directly is
    # exit 126 and EACCES on a file that is 0755 in the Battery and in the
    # cache. That is Omakure's, it is reported, and the fix is pending; when it
    # lands, this line can lose the `bash` and prove one more thing.
    def collect(box):
        return box.root(
            "-u omakure env HOME=/var/lib/omakure-workspace "
            "OMAKURE_SCRIPTS_DIR=/var/lib/omakure-workspace "
            f"OMAHOUSE_BATTERY_USER={child} "
            "bash /var/lib/omakure-workspace/omahouse-collect.sh", check=False)

    code, out = collect(dad)
    if code != 0 or "collected 1" not in out:
        raise Failed(f"the collection did not bring the day in ({code}):\n{out[:600]}")
    print(f"      {out.strip().splitlines()[-1]}")

    # -- so the house adds up two machines -----------------------------------
    document = json.loads(dad.root(f"omahouse --json house {child}"))
    session = [b for b in document["budgets"] if b["id"] == "session"][0]
    if [c["machine"] for c in session["spent"]] != ["here", "the kitchen laptop"]:
        raise Failed(f"the sum is not of two machines: {session}")
    if session["totalSeconds"] != 1800:
        raise Failed(f"the house spent {session['totalSeconds']}s, and the only "
                     "machine that spent anything spent 1800s")
    if document["notHeardFrom"]:
        raise Failed(f"a machine went unheard from after collecting: {document}")
    print("      the house added up a day it never wrote, from a machine it asked")

    # -- and a computer that is off is quiet, not failed ----------------------
    #
    # A laptop is shut most of the day. A scheduled job that went red every time
    # somebody closed one is a job whose red means nothing.
    vm.root("systemctl stop omakure-node.service")
    code, out = collect(dad)
    if code != 0:
        raise Failed(f"a machine that was off made the collection fail:\n{out[:400]}")
    if "quiet 1" not in out:
        raise Failed(f"a machine that was off was not counted as quiet:\n{out[:400]}")
    print("      with that machine off, the collection is quiet and not red")
