"""The walkthrough of `docs/how-to-link-another-computer.md`, typed for real.

Not a case that happens to cover the same ground. This follows that page step
for step, in its order, with its commands -- so that a household reading it gets
what is printed there, and so that changing the page without changing the
product fails here.

It starts from the state the page says it starts from: a second computer running
Omarchy with **no omahouse on it**, reachable by ssh and nothing else. The
package is installed by the one command under test, out of a repository, with
pacman resolving its dependency on omakure the way pacman resolves dependencies.
A case that reached around the package manager would prove nothing about the
install it is here to prove.

The two machines play the two parts of the page: `dad` is the operator's
computer -- "this one", the manager -- and `poc` is the one being linked.
"""

import json

MACHINE = "poc"
WHY = "a household follows docs/how-to-link-another-computer.md and gets what it says"

WIRE = 7879


def run(vm):
    dad = vm.peer("dad")
    try:
        walked(vm, dad)
    finally:
        # The operator's machine is borrowed, not ours. `the_parent_administers`
        # asserts this same machine has no omahouse on it.
        dad.root("rm -f /usr/bin/omahouse", check=False)
        dad.root("rm -rf /root/.ssh", check=False)
        # And the machine under test gets its omahouse back however this ended.
        # A case that failed halfway would otherwise leave every case after it
        # on a machine with no omahouse, all failing for a reason none of them
        # can explain.
        if vm.ssh("command -v omahouse", check=False)[0] != 0:
            vm.put(str(vm.build_binary), "/tmp/omahouse")
            vm.root("install -Dm755 /tmp/omahouse /usr/bin/omahouse")


def walked(vm, dad):
    Failed = vm.Failed
    here, there = dad.address, vm.address
    # The page writes `kid` and says in as many words that it is a placeholder
    # for a login name. Here it is the account these machines really have, which
    # is what makes the commands below the page's commands rather than a
    # transcription of them.
    child = vm.subject

    # -- what the page says you need before you start ------------------------
    #
    # "The other computer runs Omarchy and you have ssh to it. That is the whole
    # of what is needed there -- no omahouse yet, no omakure, nothing installed
    # by hand."
    dad.put(str(vm.build_binary), "/tmp/omahouse")
    dad.root("install -Dm755 /tmp/omahouse /usr/bin/omahouse")
    # And no rules on it yet. `reset()` reaches the machine a run is *about*
    # and reaches a borrowed one only on the way out, so a case that borrowed
    # this machine earlier can still have a profile on it -- and step 3 would
    # then fail on `julia already has a profile`, which is the right refusal to
    # the wrong question.
    dad.root("rm -f /etc/omahouse/profiles.json /etc/omahouse/machines.json "
             "/etc/omahouse/machine.json /etc/omahouse/machine-tokens.json")
    dad.root("rm -rf /var/lib/omahouse/*")

    # The operator's own ssh, which the page assumes and this program does not
    # wrap. Root's, because `machine link` is run under sudo.
    # `sh -c`, because `sudo a && b` sudoes only `a`.
    dad.root("sh -c 'mkdir -p /root/.ssh && chmod 700 /root/.ssh'")
    dad.put(vm.key, "/tmp/key")
    dad.root("install -m 600 /tmp/key /root/.ssh/id_ed25519")
    dad.root("sh -c 'printf \"Host *\\n  StrictHostKeyChecking no\\n"
             "  UserKnownHostsFile /dev/null\\n  LogLevel ERROR\\n\" "
             "> /root/.ssh/config && chmod 600 /root/.ssh/config'")

    # And the far machine as the page describes it: Omarchy, ssh, nothing else.
    # The repository stands in for the shelf a household would have; removing
    # omahouse is what makes this a computer that has never had it.
    packages = [vm.built_package, vm.built_omakure_package]
    vm.serve_a_repository(packages)
    vm.root("pacman -Rns --noconfirm omahouse", check=False)
    # And every file the suite itself deployed by hand before any case ran.
    # Read out of the package rather than listed here, so the two cannot drift:
    # a path this case forgot is `pacman -S` refusing the whole transaction with
    # `exists in filesystem`, which reads like a packaging fault and is a dirty
    # machine.
    owned = [line for line in vm.root(
        f"bsdtar -tf /var/cache/omahouse-suite/{vm.built_package.name}").splitlines()
        if line and not line.startswith(".") and not line.endswith("/")]
    vm.root("rm -f " + " ".join("/" + path for path in owned))
    vm.root("rm -rf /etc/omahouse /var/lib/omahouse /etc/omakure /var/lib/omakure "
            "/var/lib/omakure-workspace /etc/sudoers.d/omahouse-node")
    vm.root("systemctl disable --now omakure-node.service", check=False)
    vm.root("rm -rf /etc/systemd/system/omakure-node.service.d")
    vm.root("systemctl daemon-reload", check=False)
    if vm.ssh("command -v omahouse", check=False)[0] == 0:
        raise Failed("the machine being linked still has omahouse on it, so this "
                     "case would not prove the install")
    if dad.root(f"ssh arch@{there} true", check=False)[0] != 0:
        raise Failed(f"the operator's machine cannot ssh to {there}, which is the "
                     "one thing the page says it needs")
    print("      a second computer with Omarchy, ssh, and no omahouse")

    # -- 1. Link it ----------------------------------------------------------
    #
    # The one command the page opens with.
    code, said = dad.root(
        f"omahouse machine link arch@{there} "
        f"--name 'the kitchen laptop' --at {here}:{WIRE}", check=False, timeout=600)
    if code != 0:
        raise Failed(f"the one command of step 1 came back {code}:\n{said[:900]}")
    for sentence in ("the kitchen laptop: paired",
                     "the kitchen laptop is linked",
                     "log in there as root"):
        if sentence not in said:
            raise Failed(f"step 1 does not print {sentence!r}, which the page "
                         f"says it does:\n{said[:700]}")
    print("      one command: installed, linked, and in the list")

    # And it really installed it, out of the repository, with the dependency
    # resolved. This is the whole reason the far machine started empty.
    if vm.ssh("command -v omahouse", check=False)[0] != 0:
        raise Failed("the far machine still has no omahouse after `machine link`")
    if vm.ssh("command -v omakure", check=False)[0] != 0:
        raise Failed("omahouse was installed without omakure, so the one install "
                     "was not the only install")
    owner = vm.root("pacman -Qo /usr/bin/omahouse", check=False)[1]
    if "omahouse" not in owner:
        raise Failed(f"omahouse is on the machine but pacman does not own it, so "
                     f"it did not come from a package: {owner.strip()[:200]}")
    print(f"      {owner.strip().splitlines()[-1][:80]}")

    # -- 2. Ask each computer what it is -------------------------------------
    manager = dad.root("omahouse machine kind")
    if "the household's console" not in manager:
        raise Failed(f"the operator's machine does not call itself the console:\n"
                     f"{manager}")

    managed = dad.root(f"ssh arch@{there} omahouse machine kind", check=False)[1]
    # The sentence the whole design turns on, and the page says so in bold.
    if "still enforcing its own rules on its own" not in managed:
        raise Failed(f"the linked machine does not say it still enforces its own "
                     f"rules:\n{managed}")
    if "its manager is" not in managed:
        raise Failed(f"the linked machine does not name its manager:\n{managed}")
    print("      each computer says what it is, and the managed one says it is "
          "still its own")

    # -- 3. Put the same person under rules on both --------------------------
    for box in (dad, vm):
        box.root(f"omahouse profile add {child} --name Kid")
        box.root(f"omahouse limit {child} --session 2h")

    # -- 4. Read the day across both -----------------------------------------
    #
    # An afternoon on each, so the sum is of two computers that both spent
    # something rather than of one and a zero.
    vm.seed_ledger({"session": 2400})
    dad.seed_ledger({"session": 600})

    # The pipe the page prints, typed exactly. It is the whole of the transport
    # and it is deliberately something a person can type.
    code, said = dad.root(
        f"sh -c \"ssh arch@{there} omahouse day {child} | "
        f"omahouse collect 'the kitchen laptop' {child}\"", check=False)
    if code != 0 or "counts towards the house" not in said:
        raise Failed(f"the pipe of step 4 came back {code}:\n{said[:500]}")

    document = json.loads(dad.root(f"omahouse --json house {child}"))
    session = [b for b in document["budgets"] if b["id"] == "session"][0]
    if [c["machine"] for c in session["spent"]] != ["here", "the kitchen laptop"]:
        raise Failed(f"the house is not adding up both computers: {session}")
    if session["limitSeconds"] != 7200:
        raise Failed(f"2h is not two hours in the household: {session}")
    if session["totalSeconds"] < 2400:
        raise Failed(f"the far machine's day did not reach the sum: {session}")
    left = session["leftSeconds"]
    print(f"      the house has spent {session['totalSeconds']}s of 7200s; "
          f"{left}s stand")

    # -- 5. Push the truth back ----------------------------------------------
    minutes = max(1, left // 60)
    code, said = dad.root(
        f"ssh arch@{there} sudo omahouse leave {child} --session {minutes}m",
        check=False)
    if code != 0:
        raise Failed(f"step 5 came back {code}:\n{said[:400]}")
    if "left today" not in said:
        raise Failed(f"`leave` does not say what is left, which the page shows:\n"
                     f"{said[:300]}")

    # And it really moved the far machine's own number, which is the only thing
    # that makes step 5 worth typing.
    theirs = json.loads(vm.root(f"omahouse --json status {child}"))
    budget = [b for b in theirs["budgets"] if b["id"] == "session"][0]
    if budget["leftSeconds"] != minutes * 60:
        raise Failed(f"the far machine still thinks it has {budget['leftSeconds']}s "
                     f"and not the {minutes * 60}s the house left it: {budget}")
    print(f"      told from here, the far machine now has {minutes}m of its own")

    # -- Taking it back ------------------------------------------------------
    forgotten = dad.root("omahouse machine remove 'the kitchen laptop'")
    if "Nothing on that machine changed" not in forgotten:
        raise Failed(f"removing does not say what it did not do:\n{forgotten}")
    # And it really did not: the page's second sentence is the one somebody
    # trusts, so it is the one worth checking.
    still = vm.root(f"omahouse --json profile show {child}", check=False)
    if still[0] != 0:
        raise Failed("forgetting the computer took its profile with it, and the "
                     "page says it does not")
    print("      forgotten here, and untouched there")
