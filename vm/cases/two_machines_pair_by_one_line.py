"""The seven manual steps, done by three commands and one line carried.

`the_two_machines_trust_each_other.py` proved the wire works. It proved it the
way everything is proved the first time: by hand, with the harness writing
`node.toml` in a heredoc, creating the service account, wiping state, running
`node init` as the right user, exchanging three values in each direction, and
generating a token per side. Every one of those was found by getting it wrong
first, and every one of the failures reads like a networking problem.

None of that is something a household can do. So it became three verbs, and
this is the case that says the verbs really do it -- on two machines, with
nothing but a line copied between them, the way a person would.

What has to hold:

  1. `machine invite` gives the operator's machine an identity and prints one
     line, and the line carries nothing a shell would mangle;
  2. `machine prepare` on the far machine, given only that line, ends with a
     node that has an identity, trusts the operator, and is listening;
  3. `machine add --pair` back on the operator's, and the two really can speak:
     a Cue dispatched over the wire lands and changes a file;
  4. a line that arrived in pieces is refused as pieces, before anything is
     written.

The last one is here rather than in the CLI suite for one reason: this is the
machine where refusing costs something. A truncated certificate accepted here
is a trust registry that has to be torn down by hand.
"""

import json
import time

MACHINE = "poc"
WHY = "three commands and one carried line replace seven manual pairing steps"

# omahouse's own defaults, from `renderNodeConfig`.
API, WIRE = 8787, 7879


def run(vm):
    dad = vm.peer("dad")

    # The operator's machine needs omahouse, and this is the first case
    # where that is true: `dad` is stripped of it by `vm/make-dad.sh`, because
    # every case before this one used it as a machine that only answers. Here it
    # is the one that administers, and administering is an omahouse verb.
    #
    # And taken off again at the end, whatever happens. `the_parent_administers`
    # asserts the opposite -- that the console needs no omahouse -- so a copy
    # left behind here is that case failing on a machine, in a later run, for a
    # reason nothing in it explains.
    dad.put(str(vm.build_binary), "/tmp/omahouse")
    dad.root("install -Dm755 /tmp/omahouse /usr/bin/omahouse")
    try:
        paired(vm, dad)
    finally:
        dad.root("rm -f /usr/bin/omahouse", check=False)


def paired(vm, dad):
    Failed = vm.Failed
    child = vm.subject
    here, there = vm.address, dad.address

    # Omakure comes from the local build and never from the network -- the
    # binary under test is the one on this developer's disk. What its own
    # installer would do is done here by hand, because that installer will only
    # provision the service alongside a tokens file that already holds hashed
    # entries, and the entries are made by the node it is about to start.
    for box in (vm, dad):
        box.put(str(vm.omakure_binary), "/tmp/omakure")
        box.ssh("chmod +x /tmp/omakure")
        box.root("install -m 0755 /tmp/omakure /usr/local/bin/omakure")
        box.root("sh -c 'getent group omakure >/dev/null || groupadd --system omakure'")
        box.root("sh -c 'id omakure >/dev/null 2>&1 || useradd --system --gid omakure "
                 "--home-dir /var/lib/omakure-workspace --shell /usr/sbin/nologin "
                 "omakure'")
        # The service first, because the wipe below is of state it holds open.
        # `reset()` between cases knows about omahouse and not about Omakure, so
        # a run started after a run leaves a node still serving -- and wiping
        # /var/lib/omakure under it is a node that answers `registry_invalid`
        # from then on.
        box.root("systemctl disable --now omakure-node.service", check=False)
        # Bracketed, or `pkill -f` matches the very shell running it.
        box.root("pkill -f 'omakure node[ ]serve' || true", check=False)
        box.root("rm -rf /etc/systemd/system/omakure-node.service "
                 "/etc/systemd/system/omakure-node.service.d")
        box.root("systemctl daemon-reload", check=False)
        # From nothing, every time. `node init` refuses to replace an identity,
        # so a half-made one from an earlier attempt is a machine that can never
        # be initialised again. This state belongs to the case and not to the
        # machine, which is what makes wiping it honest here and wrong anywhere
        # else.
        box.root("rm -rf /var/lib/omakure /var/lib/omakure-workspace /etc/omakure")
        box.root("mkdir -p /etc/omakure /var/lib/omakure /var/lib/omakure-workspace")
        box.root("chown -R omakure:omakure /var/lib/omakure /var/lib/omakure-workspace")
        box.root("chmod 0700 /var/lib/omakure")
        box.root("chmod 0750 /var/lib/omakure-workspace")
        box.ssh("command -v git >/dev/null || sudo pacman -S --noconfirm --needed git",
                timeout=300, check=False)

    # -- 1. the operator's machine invites ----------------------------------
    #
    # `dad` is the operator's: it is the one that will give orders. It has no
    # Omakure identity at this point, and asking for an invitation is the
    # one-time act of becoming the household's Conductor.
    said = json.loads(dad.root(f"omahouse --json machine invite --at {there}:{WIRE} "
                               "--name 'the study'"))
    if not said.get("madeIdentity"):
        raise Failed("the operator's machine claimed an identity it did not have")
    invite = said["invite"]
    print(f"      the study is {said['nodeId'][:16]}…, and printed one line")

    # A line a person copies. Anything a shell would touch in it is a household
    # finding out that copy and paste was not enough.
    if any(c in invite for c in " +/=\n\t"):
        raise Failed(f"the invitation is not one word a shell would leave alone: "
                     f"{invite[:80]}")

    # -- 2. the far machine is prepared from that line alone ----------------
    #
    # Nothing else is passed: not the operator's node id, not its public key,
    # not its certificate. The line is the whole of what crossed.
    prepared = json.loads(vm.root(
        f"omahouse --json machine prepare --invite {invite} --at {here}:{WIRE} "
        "--name 'the kitchen laptop'"))
    if prepared["conductor"] != said["nodeId"]:
        raise Failed("the prepared machine trusts somebody other than the operator")
    if prepared["nodeId"] == said["nodeId"]:
        raise Failed("both machines claim one identity; each must generate its own")
    if not prepared["serving"]:
        raise Failed("the prepared machine wrote its pairing but nothing is listening")
    pair = prepared["pair"]
    print(f"      the kitchen laptop is {prepared['nodeId'][:16]}…, and is listening")

    # The sudoers line is the one thing here that hands an account a route to
    # root, so it is read back rather than assumed: one binary, and no arguments
    # left open. A rule naming `ALL` would turn a Cue for one script into a
    # route to everything.
    rule = vm.root("cat /etc/sudoers.d/omahouse-node").strip()
    if rule != "omakure ALL=(root) NOPASSWD: /usr/bin/omahouse":
        raise Failed(f"the sudoers rule is not the narrow one: {rule!r}")

    # -- 3. and the operator writes it down ---------------------------------
    added = json.loads(dad.root(
        f"omahouse --json machine add 'the kitchen laptop' --pair {pair}"))
    if not added.get("trusted") or not added.get("serving"):
        raise Failed(f"the operator's machine did not finish the pairing: {added}")
    listed = json.loads(dad.root("omahouse --json machines"))
    if [m["name"] for m in listed] != ["the kitchen laptop"] \
            or not listed[0]["reachable"]:
        raise Failed(f"the house does not know its machine: {listed}")
    print("      each side wrote the other down, from the line and nothing else")

    # -- 4. and it is a real wire, because something crosses it -------------
    #
    # The whole point of the three verbs is that this works afterwards without
    # anybody touching a config. A Cue is the smallest thing that proves it: it
    # is dispatched over the transport, authorised by the receiver's own
    # registry, and it ends in a file on the far machine having changed.
    vm.make_profile({"session": 120}, default="allow")

    # The same environment the service runs with, and not merely a similar one.
    # A Battery installed under one workspace and looked for under another is
    # `accepted: false, code 1206`, which says "not declared" and means "not
    # where you are looking".
    def as_node(box, command, check=True):
        return box.root("-u omakure env HOME=/var/lib/omakure-workspace "
                        "OMAKURE_SCRIPTS_DIR=/var/lib/omakure-workspace "
                        f"omakure {command}", check=check)

    # Pairing installed the grant adapter in the service workspace.
    # A Cue carries no arguments, so the account comes from the node's own
    # environment.
    as_node(vm, f"env create house OMAHOUSE_BATTERY_USER={child}")
    as_node(vm, "env activate house")
    # Reload the environment selected above for the no-argument Cue.
    vm.root("systemctl restart omakure-node.service")

    # The bearer is on disk and was never printed: `machine invite` put the
    # hashed half in Omakure's tokens file and this half in a root-only file of
    # ours, because a working credential in a terminal's scrollback is a working
    # credential in the next screenshot.
    bearer = dad.root("cat /etc/omahouse/omakure-token").strip()
    if not bearer:
        raise Failed("machine invite left no bearer for this machine's own API")
    if dad.root("stat -c %a /etc/omahouse/omakure-token").strip() != "600":
        raise Failed("the bearer is readable by somebody other than root")

    def node_status():
        # Over the API and not over the CLI. The wire is live state of the
        # running service, and a CLI invocation is a different process reading
        # files -- it answers `transport: null` however connected the node is.
        out = dad.ssh(f"curl -s --max-time 10 -H 'Authorization: Bearer {bearer}' "
                      f"http://127.0.0.1:{API}/v1/node/status", check=False)[1]
        try:
            return json.loads(out)["data"]
        except (json.JSONDecodeError, KeyError, TypeError):
            return None

    # The session between them is not instant, and a Cue sent before it exists
    # is refused with `this node holds no session with that peer` -- which reads
    # like a trust problem and is a timing one. So it is waited for by name.
    def connected():
        wire = (node_status() or {}).get("transport") or {}
        return (wire.get("expected_peer_count") == 1
                and wire.get("expected_connected_peer_count") == 1)

    try:
        vm.wait_for(connected, vm.pace["patience_seconds"],
                    f"{dad.domain} to hold a session with {vm.domain}")
    except Exception:
        wire = (node_status() or {}).get("transport")
        raise Failed(
            "the two machines never held a session, so nothing was proved about "
            f"the wire\n    transport: {json.dumps(wire)[:300]}\n"
            f"    {vm.domain}: "
            f"{vm.root('systemctl is-active omakure-node', check=False)[1].strip()}\n"
            f"    {dad.domain}: "
            f"{dad.root('systemctl is-active omakure-node', check=False)[1].strip()}")
    print("      the wire came up on its own, with nobody editing a config")

    # A Cue accepted is a Cue enqueued, not a Cue run: the workers are what
    # drain it, and the drop-in `machine prepare` writes is what asks for one.
    def cue(script, seconds=90):
        body = json.dumps({"peer_node_id": prepared["nodeId"], "script": script,
                           "reason": "household", "wait_seconds": seconds})
        out = dad.ssh(f"curl -s --max-time {seconds + 30} "
                      "-H 'Content-Type: application/json' "
                      f"-H 'Authorization: Bearer {bearer}' "
                      f"--data-binary {json.dumps(body)} "
                      f"http://127.0.0.1:{API}/v1/node/cues", check=False)[1]
        try:
            return json.loads(out)
        except json.JSONDecodeError:
            raise Failed(f"the cue answered something that is not JSON: {out[:200]!r}\n"
                         f"    {vm.domain}: {vm.root('journalctl -u omakure-node -n 6 '
                                                     '--no-pager', check=False)[1][:400]}")

    before = json.loads(vm.root(f"omahouse status {child} --json"))
    granted = [b for b in before["budgets"] if b["id"] == "session"][0]["grantedSeconds"]

    answer = cue("omahouse-grant.sh")
    if not answer.get("ok") or not (answer.get("data") or {}).get("accepted"):
        # `ok` only says the Conductor's API answered. Whether the Performer
        # took it is `data.accepted`, and 1206 beside it is "the receiver does
        # not declare this script".
        declared = as_node(vm, "--json battery list", check=False)[1]
        raise Failed(f"the cue was refused across a wire that is up: "
                     f"{json.dumps(answer)[:400]}\n"
                     f"    declared: {declared.strip()[:400]}\n"
                     f"    {vm.domain}: "
                     f"{vm.root('journalctl -u omakure-node -n 12 --no-pager', check=False)[1][-600:]}")

    def landed():
        day = json.loads(vm.root(f"omahouse status {child} --json"))
        return [b for b in day["budgets"] if b["id"] == "session"][0]["grantedSeconds"]
    vm.wait_for(lambda: landed() > granted, vm.pace["patience_seconds"],
                "the grant the cue asked for")
    print(f"      a Cue crossed the wire and {child}'s granted time went {granted}s "
          f"-> {landed()}s, with nothing local doing it")

    # -- 5. and a line that arrived in pieces is refused as pieces ----------
    #
    # Here rather than in the CLI suite because here is where refusing costs
    # something: a truncated certificate accepted is a trust registry somebody
    # has to tear down by hand.
    code, out = dad.root(f"omahouse machine add spare --pair {pair[:len(pair) // 2]}",
                         check=False)
    if code == 0 or "damaged" not in out:
        raise Failed(f"half a pairing line was not refused as half a line: {out[:200]}")
    if [m["name"] for m in json.loads(dad.root("omahouse --json machines"))] \
            != ["the kitchen laptop"]:
        raise Failed("a machine was written down from a line that never arrived whole")
    print("      half a line was refused, and nothing was written from it")
