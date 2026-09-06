"""The last link: two nodes that know each other, and a Cue that really lands.

Everything else in this direction has been measured. The Battery's scripts run,
the HTTP console carries arguments out and a document back, the tick works as a
scheduled job. What was never exercised is the one thing the whole fleet story
rests on: **the wire and the trust gates.** A Cue dispatched for real, from one
machine to another, authorised by the receiver's own registry.

This is the household shape and not the school one. Enrollment is `manual`:
each side is told the other's identity by hand, which is what two or three
computers in a house want. The signed-bundle path -- an authority keypair, an
organization, bootstrap hashes -- is for the fleet nobody can walk to.

What has to hold, in order:

  1. both machines have an identity of their own;
  2. each trusts the other, explicitly, from its own registry;
  3. a Cue naming a Battery script lands, and omahouse on the far side really
     wrote what it was asked to write;
  4. a script the receiver did not declare is refused -- the gate is the
     receiver's, and nothing in the message contributes to it.
"""

import json
import time

MACHINE = "poc"
WHY = "one machine cues another, and the receiver's own registry is what allows it"

REPO = "https://github.com/This-Is-NPC/omahouse-battery.git"
API, WIRE = 7878, 7879


def run(vm):
    Failed = vm.Failed
    child = vm.subject

    # Every node operation runs **as the node's own account**, never as root.
    # Run as root they create root-owned lock files, and omakure then refuses
    # its own state on the next invocation -- rightly, since that directory is
    # 0700 secrets with a closed allow-list. `HOME` is the workspace and never
    # the state directory: an omakure command without `OMAKURE_SCRIPTS_DIR`
    # resolves its default workspace under `$HOME`, so pointing it at the
    # secrets directory bricks the node with its own scratch files.
    def as_node(box, command, check=True):
        return box.root("-u omakure env HOME=/var/lib/omakure-workspace "
                        f"omakure {command}", check=check)
    dad = vm.peer("dad")
    here, there = vm.address, dad.address

    for box in (vm, dad):
        box.put(str(vm.omakure_binary), "/tmp/omakure")
        box.ssh("chmod +x /tmp/omakure")
        box.root("install -m 0755 /tmp/omakure /usr/local/bin/omakure")
        box.ssh("command -v git >/dev/null || sudo pacman -S --noconfirm --needed git",
                timeout=300, check=False)

    # -- 1. an identity each ------------------------------------------------
    #
    # Generated on the machine and never shipped in an image: a bundle names the
    # node that will apply it and is checked against that node's own identity,
    # so there is nothing a fresh machine could be handed that it could apply to
    # itself.
    def write_config(box, peers, batteries):
        box.root(f"tee /etc/omakure/node.toml >/dev/null <<'TOML'\n"
                 "version = 1\n"
                 "\n[node]\n"
                 f'display_name = "{box.domain}"\n'
                 "\n[api]\n"
                 f'bind = "127.0.0.1:{API}"\n'
                 "\n[network]\n"
                 'mode = "direct"\n'
                 "relays = []\n"
                 f'direct_bind = "0.0.0.0:{WIRE}"\n'
                 f"static_peers = [{peers}]\n"
                 "max_message_bytes = 1048576\n"
                 "\n[trust]\n"
                 'enrollment = "manual"\n'
                 f"allow_remote_cues = {'true' if batteries else 'false'}\n"
                 "remote_cue_scripts = []\n"
                 f"remote_cue_batteries = [{batteries}]\n"
                 "allow_baseline_push = false\n"
                 "baseline_publishers = []\n"
                 "authorities = []\n"
                 'bootstrap_token_hash = ""\n'
                 'bootstrap_nonce_hash = ""\n'
                 "\n[discovery]\nenabled = false\n"
                 "\n[organization]\n"
                 'id = "one-household"\n'
                 'discovery_secret_ref = ""\n'
                 "TOML")
        box.root("chown root:omakure /etc/omakure/node.toml")
        box.root("chmod 0640 /etc/omakure/node.toml")

    def identity(box):
        # The node principal the shipped installer makes. Without it, `node
        # status` refuses with `node path is insecure: configured node service
        # user does not exist` -- the state directory is a 0700 secrets
        # directory and it will not be read by nobody in particular.
        #
        # The home is the workspace and never `/var/lib/omakure`: that is the
        # secrets directory, guarded by a closed allow-list, and any omakure
        # command run as this account without `OMAKURE_SCRIPTS_DIR` would point
        # its own scratch files at the one place that bricks the node.
        box.root("sh -c 'getent group omakure >/dev/null || groupadd --system omakure'")
        box.root("sh -c 'id omakure >/dev/null 2>&1 || useradd --system --gid omakure "
                 "--home-dir /var/lib/omakure-workspace --shell /usr/sbin/nologin "
                 "omakure'")
        # From nothing, every time. `node init` refuses to replace an existing
        # identity, so a half-made one from an earlier attempt is a machine that
        # can never be initialised again -- and this state belongs to the case
        # rather than to the machine, which is what makes wiping it honest here
        # and wrong anywhere else.
        box.root("rm -rf /var/lib/omakure /var/lib/omakure-workspace")
        box.root("mkdir -p /etc/omakure /var/lib/omakure /var/lib/omakure-workspace")
        # `-R`, because a `node init` that ran before the principal existed
        # leaves a root-owned lock file behind and every later run refuses:
        # "is owned by 0:0 but must be owned by 964:964". The refusal is right
        # -- that directory is 0700 secrets -- and it says its own fix.
        box.root("chown -R omakure:omakure /var/lib/omakure /var/lib/omakure-workspace")
        box.root("chmod 0700 /var/lib/omakure")
        box.root("chmod 0750 /var/lib/omakure-workspace")
        # The config is root's to write and the node's to read: `root:omakure`,
        # 0640, exactly as the shipped installer leaves it -- and it is written
        # *before* `node init`, because init runs as the node's account and
        # `/etc/omakure` is root's. Left to create the file itself, it answers
        # `io_failed: Permission denied`. The peers are filled in afterwards,
        # once each side knows the other's identity.
        write_config(box, "", "")
        started = as_node(box, "node init", check=False)
        answered = as_node(box, "--json node status", check=False)
        if answered[0] != 0:
            raise Failed(f"{box.domain} has no identity after `node init`:\n"
                         f"    init said: {started[1].strip()[:200]}\n"
                         f"    status said: {answered[1].strip()[:200]}")
        who = json.loads(answered[1])["data"]["identity"]
        # `node status` answers happily with a null identity, so the exit code
        # alone is not the question. The question is whether there is a node.
        if not who or not who.get("node_id"):
            raise Failed(f"{box.domain} answered without an identity after "
                         f"`node init`:\n    init said: {started[1].strip()[:200]}")
        cert = box.root("od -An -tx1 -v /var/lib/omakure/transport.cert").strip()
        cert = "".join(cert.split())
        if not cert or any(c not in "0123456789abcdef" for c in cert):
            raise Failed(f"{box.domain}: the transport certificate is not hexadecimal")
        return who["node_id"], who["public_key"], cert

    mine = identity(vm)
    theirs = identity(dad)
    if mine[0] == theirs[0]:
        raise Failed("both machines claim one identity; each must generate its own")
    print(f"      {vm.domain} is {mine[0][:16]}…, {dad.domain} is {theirs[0][:16]}…")

    # -- 2. each side's own registry ----------------------------------------
    # The child's machine is the one that accepts Cues, and it accepts them only
    # for the Battery it was told about. The parent's accepts none: a Conductor
    # that could be cued back is a Conductor somebody took.
    write_config(vm, f'"{theirs[0]}@{there}:{WIRE}"', '"omahouse"')
    write_config(dad, f'"{mine[0]}@{here}:{WIRE}"', "")

    def trust(box, peer, role):
        as_node(box, f"node trust --node-id {peer[0]} --public-key {peer[1]} "
                 f"--transport-certificate {peer[2]} --role {role} "
                 "--capability inventory-health --capability notifications "
             "--capability remote-run --actor household --reason household "
             "--confirmed")
    trust(vm, theirs, "conductor")
    trust(dad, mine, "performer")
    print("      each side wrote the other into its own registry")

    # -- the machine under rules, and the Battery it may be cued for ---------
    vm.make_profile({"session": 120}, default="allow")
    # The node's account needs a route to root, and exactly one. omahouse's
    # writing verbs need it because `/etc/omahouse/profiles.json` is a root
    # daemon's file, and the account that answers Cues is `omakure`, which has
    # no shell and no sudo. One binary, no arguments constrained -- the checks
    # are omahouse's own, and a rule naming `ALL` here would make a Cue for one
    # script a route to everything.
    vm.root("tee /etc/sudoers.d/omahouse-node >/dev/null <<'SUDO'\n"
            "omakure ALL=(root) NOPASSWD: /usr/bin/omahouse\n"
            "SUDO")
    vm.root("chmod 0440 /etc/sudoers.d/omahouse-node")

    # Into the **node's** workspace, as the node's account. The service that
    # answers a Cue is `omakure` with `/var/lib/omakure-workspace`, so a Battery
    # installed into somebody's home is a Battery the gate cannot see: the cue
    # comes back `accepted: false, code 1206`, which says "not declared" and
    # means "not there".
    as_node(vm, f"--json battery add {REPO} --ref master --name omahouse")
    as_node(vm, "--json battery sync omahouse")
    as_node(vm, "--json battery install omahouse omahouse.grant")
    # A Cue carries no arguments, so the account comes from the node's own
    # environment -- the layer this whole design leans on.
    as_node(vm, f"env create house OMAHOUSE_BATTERY_USER={child}")
    as_node(vm, "env activate house")

    # The Conductor's own API is where a Cue is asked for, and it wants a bearer.
    # `node serve` has no flag for the file; it reads `OMAKURE_TOKENS_FILE`, the
    # way the shipped unit sets it.
    # Both, and not only the Conductor. `node serve` always answers an API, so
    # it always wants auth material: without it the process refuses to start at
    # all -- `auth required: set OMAKURE_TOKENS_FILE` -- and then nothing is
    # listening on the wire either, which reads like a peering problem and is a
    # startup one.
    def give_token(box):
        made = json.loads(as_node(box, "--json token generate --id household "
                                       "--scope node:read --scope node:write"))["data"]
        box.ssh("sudo tee /etc/omakure/tokens.toml >/dev/null",
                stdin=made["tokens_file_entry"])
        box.root("chown root:omakure /etc/omakure/tokens.toml")
        box.root("chmod 0640 /etc/omakure/tokens.toml")
        return made["token"]

    give_token(vm)
    token = give_token(dad)

    # `--allow-non-loopback-direct`, because the wire binds 0.0.0.0 and Omakure
    # refuses that by default: a machine is never on the network because
    # somebody forgot a flag. The tokens file goes only where an API is really
    # answered -- the Conductor's -- since pointing at one that is not there is
    # `tokens file I/O error` and a service that never starts.
    def serve(box, tokens):
        box.root("-u omakure env HOME=/var/lib/omakure-workspace "
                 + (f"OMAKURE_TOKENS_FILE={tokens} " if tokens else "")
                 + "setsid --fork omakure node serve --allow-non-loopback-direct "
                 "--workers 1 </dev/null >/tmp/node.log 2>&1", check=False)

    serve(vm, "/etc/omakure/tokens.toml")
    serve(dad, "/etc/omakure/tokens.toml")

    # The session between them is not instant, and a Cue sent before it exists
    # is refused with `this node holds no session with that peer` -- which reads
    # like a trust problem and is a timing one. So the wire is waited for by
    # name: the Conductor's own status says how many peers it expects and how
    # many it has.
    def node_status():
        # Over the API and not over the CLI. The wire is live state of the
        # running service, and a CLI invocation is a different process reading
        # files -- it answers `transport: null` however connected the node is.
        out = dad.ssh(f"curl -s --max-time 10 -H 'Authorization: Bearer {token}' "
                      f"http://127.0.0.1:{API}/v1/node/status", check=False)[1]
        try:
            return json.loads(out)["data"]
        except (json.JSONDecodeError, KeyError, TypeError):
            return None

    def connected():
        answered = node_status()
        wire = (answered or {}).get("transport") or {}
        return (wire.get("expected_peer_count") == 1
                and wire.get("expected_connected_peer_count") == 1)

    try:
        vm.wait_for(connected, vm.pace["patience_seconds"],
                    f"{dad.domain} to hold a session with {vm.domain}")
    except Exception:
        wire = (node_status() or {}).get("transport")
        raise Failed(
            f"no session between them.\n"
            f"    {dad.domain} transport: {json.dumps(wire)[:300]}\n"
            f"    {vm.domain} listening: "
            f"{vm.root('ss -lntp 2>/dev/null | grep 7879 || echo none', check=False)[1].strip()[:160]}\n"
            f"    {dad.domain} listening: "
            f"{dad.root('ss -lntp 2>/dev/null | grep 7879 || echo none', check=False)[1].strip()[:160]}\n"
            f"    {vm.domain} log: {vm.root('tail -n 4 /tmp/node.log', check=False)[1].strip()[:250]}\n"
            f"    {dad.domain} log: {dad.root('tail -n 4 /tmp/node.log', check=False)[1].strip()[:250]}")
    print("      the two hold a live session over the wire")

    # A Cue accepted is a Cue enqueued, not a Cue run: the workers are what
    # drain it, and `node serve` starts none unless asked. Without them the
    # answer is `accepted: true` and nothing ever happens, which is the most
    # confusing shape a failure can take.
    def cue(script, seconds=90):
        body = json.dumps({"peer_node_id": mine[0], "script": script,
                           "reason": "household", "wait_seconds": seconds})
        out = dad.ssh(f"curl -s --max-time {seconds + 30} "
                      "-H 'Content-Type: application/json' "
                      f"-H 'Authorization: Bearer {token}' "
                      f"--data-binary {json.dumps(body)} "
                      f"http://127.0.0.1:{API}/v1/node/cues", check=False)[1]
        try:
            return json.loads(out)
        except json.JSONDecodeError:
            # An empty answer is almost always a service that is not there, so
            # the refusal carries both machines' logs rather than the silence.
            here_log = vm.root("tail -n 8 /tmp/node.log", check=False)[1]
            there_log = dad.root("tail -n 8 /tmp/node.log", check=False)[1]
            listening = dad.root("ss -lntp 2>/dev/null | grep -c omakure || echo 0",
                                 check=False)[1].strip()
            raise Failed(f"the cue answered something that is not JSON: {out[:200]!r}\n"
                         f"    {dad.domain} sockets: {listening}\n"
                         f"    {dad.domain} log: {there_log.strip()[:400]}\n"
                         f"    {vm.domain} log: {here_log.strip()[:400]}")

    # -- 3. a Cue that lands -------------------------------------------------
    before = json.loads(vm.root(f"omahouse status {child} --json"))
    granted = [b for b in before["budgets"] if b["id"] == "session"][0]["grantedSeconds"]

    answer = cue("omahouse-grant.sh")
    if not answer.get("ok") or not (answer.get("data") or {}).get("accepted"):
        # `ok` only says the Conductor's API answered. Whether the Performer
        # took it is `data.accepted`, and the code beside it is the gate that
        # said no -- 1206 is "the receiver does not declare this script".
        raise Failed(f"the cue was refused: {json.dumps(answer)[:400]}")
    print(f"      the cue was accepted: {json.dumps(answer.get('data'))[:90]}")

    def landed():
        day = json.loads(vm.root(f"omahouse status {child} --json"))
        return [b for b in day["budgets"] if b["id"] == "session"][0]["grantedSeconds"]
    vm.wait_for(lambda: landed() > granted, vm.pace["patience_seconds"],
                "the grant the cue asked for")
    print(f"      {child}'s ledger went from {granted}s to {landed()}s of granted time, "
          "and nothing local did it")

    # -- 4. and the gate is the receiver's ----------------------------------
    #
    # A script that is present in the workspace but not declared must be refused,
    # and the refusal has to come from the receiver's registry rather than from
    # anything the message said.
    as_node(vm, "--json battery install omahouse omahouse.enforce")
    vm.root("sed -i 's/^remote_cue_batteries = .*/remote_cue_batteries = []/' "
            "/etc/omakure/node.toml")
    vm.root("pkill -f 'omakure node serve' || true", check=False)
    serve(vm, "/etc/omakure/tokens.toml")
    time.sleep(10)
    refused = cue("omahouse-grant.sh", seconds=15)
    if refused.get("ok") and (refused.get("data") or {}).get("accepted"):
        raise Failed(f"the same cue was accepted with the Battery undeclared: "
                     f"{json.dumps(refused)[:300]}")
    print("      with the Battery undeclared, the same cue is refused by the receiver")
