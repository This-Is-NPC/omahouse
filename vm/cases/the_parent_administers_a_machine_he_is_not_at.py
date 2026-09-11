"""Scenario B of the spike: two machines, and the parent is not at the child's.

The child's computer is fiscalised: omahouse, its daemon, and an account with no
privilege. The parent's computer has **no omahouse at all** -- it conducts and it
reads, and that asymmetry is the claim being tested. If the console needed
omahouse installed beside it, this would be two managed machines rather than an
operator and a subject.

Between them: Omakure's authenticated HTTP API, bound to the LAN rather than to
loopback. That is the channel the spike measured as the only one that carries
both halves of an instruction -- *which* account and *how many* minutes -- and
brings a document back. A Remote Cue carries neither: no arguments out, and the
Health Plane forbids stdout, usernames and process lists coming back.

What has to hold, in order:

  1. the parent reaches the child's machine over the network at all;
  2. he changes a rule there, with arguments, and omahouse on that machine
     really wrote it;
  3. he reads her day back as a document;
  4. she cannot do either -- not the rule, and not the console.
"""

import json

MACHINE = "poc"
WHY = "the parent's PC changes and reads the child's machine, with no omahouse on his"

REPO = "https://github.com/This-Is-NPC/omahouse-battery.git"
PORT = 7899


def run(vm):
    Failed = vm.Failed
    child = vm.subject

    # The parent's machine, brought up beside this one. It is disposable and has
    # nothing on it worth keeping.
    dad = vm.peer("dad")
    kid_ip, dad_ip = vm.address, dad.address
    if kid_ip == dad_ip:
        raise Failed("both roles landed on one address; this case needs two machines")

    if dad.ssh("command -v omahouse || true", check=False)[1].strip():
        raise Failed("the parent's machine has omahouse on it; "
                     "this case is about a console that does not need one")

    # -- the child's machine, which is the one under rules --------------------
    vm.put(str(vm.omakure_binary), "/tmp/omakure")
    vm.ssh("chmod +x /tmp/omakure && sudo install -m 0755 /tmp/omakure /usr/local/bin/omakure")
    vm.ssh("rm -rf ~/ws ~/.omakure && mkdir -p ~/ws")
    vm.make_profile({"session": 120}, default="allow")

    def omk(command, check=True):
        return vm.ssh(f"/usr/local/bin/omakure --scripts-dir ~/ws {command}", check=check)

    omk(f"--json battery add {REPO} --ref master --name omahouse")
    omk("--json battery sync omahouse")
    for script in ("grant", "status"):
        omk(f"--json battery install omahouse omahouse.{script}")

    generated = json.loads(omk("--json token generate --id dad "
                               "--scope runs:read --scope runs:write "
                               "--scope scripts:read --scope config:read"))
    token = generated["data"]["token"]
    vm.ssh("cat > ~/tokens.toml", stdin=generated["data"]["tokens_file_entry"])

    # Bound to the network and not to loopback, which Omakure makes an explicit
    # act: `--allow-non-loopback` is refused by default, so a machine is never
    # on the LAN because somebody forgot a flag.
    vm.ssh(f"setsid --fork /usr/local/bin/omakure --scripts-dir ~/ws api "
           f"--bind 0.0.0.0:{PORT} --allow-non-loopback --tokens-file ~/tokens.toml "
           "</dev/null >/tmp/api.log 2>&1", check=False)

    # -- everything from here runs on the parent's machine --------------------
    #
    # `dad.ssh`, never `vm.ssh`. A step that quietly ran on the child's machine
    # would prove nothing about administering one you are not sitting at.
    def console(method, path, body=None):
        payload = f"-d {json.dumps(json.dumps(body))} -H 'Content-Type: application/json'" \
            if body else ""
        out = dad.ssh(f"curl -s -m 10 -X {method} http://{kid_ip}:{PORT}{path} "
                      f"-H 'Authorization: Bearer {token}' {payload}", check=False)[1]
        try:
            return json.loads(out)
        except json.JSONDecodeError:
            raise Failed(f"{method} {path} from the parent's machine answered "
                         f"something that is not JSON:\n{out[:400]}")

    vm.wait_for(lambda: console("GET", "/v1/health").get("ok") is True,
                vm.pace["patience_seconds"],
                f"the child's machine to answer the parent's at {kid_ip}:{PORT}")

    # The worker only after the API is answering, and the two are separate
    # processes because `omakure api` serves without draining. Started together
    # they race for `runs.sqlite` and one of them loses: a run enqueued in that
    # window comes back `io_failed: Init runs db failed: database is locked`,
    # measured here. Serialising them is the order a deployment would use
    # anyway, and it is not a fix for the race -- that one belongs to Omakure.
    vm.ssh("setsid --fork /usr/local/bin/omakure --scripts-dir ~/ws queue worker "
           "</dev/null >/tmp/worker.log 2>&1", check=False)
    print(f"      the parent at {dad_ip} reached the child at {kid_ip}")

    def finished(run_id):
        vm.wait_for(
            lambda: console("GET", f"/v1/runs/{run_id}")["data"]["state"]
            not in ("queued", "running"),
            vm.pace["patience_seconds"], f"run {run_id}")
        return console("GET", f"/v1/runs/{run_id}")["data"]

    # 2. A rule changed from the other machine, with both halves of it.
    started = console("POST", "/v1/runs",
                      {"script": "omahouse-grant.sh",
                       "args": ["--user", child, "--minutes", "25"]})
    if not started.get("ok"):
        raise Failed(f"the child's machine refused the parent's run: {started}")
    row = finished(started["data"]["run_id"])
    if row["exit_code"] != 0:
        raise Failed(f"the grant came back {row['exit_code']}:\n{row['stderr'][:300]}")

    # And omahouse on her machine really wrote it -- read locally, so the proof
    # does not come from the same channel that made the claim.
    day = json.loads(vm.root(f"omahouse status {child} --json"))
    session = [b for b in day["budgets"] if b["id"] == "session"]
    if not session or session[0]["grantedSeconds"] != 1500:
        raise Failed(f"her ledger does not hold the parent's 25 minutes: {session}")
    print(f"      he granted 25m from his machine; her ledger says "
          f"{session[0]['grantedSeconds']}s")

    # 3. Her day, read back as a document rather than as an exit code.
    started = console("POST", "/v1/runs",
                      {"script": "omahouse-status.sh", "args": ["--user", child]})
    row = finished(started["data"]["run_id"])
    try:
        document = json.loads(row["stdout"])
    except json.JSONDecodeError:
        raise Failed(f"her day did not arrive as a document:\n{row['stdout'][:300]}")
    if document.get("user") != child:
        raise Failed(f"the document is about {document.get('user')!r}, not {child!r}")
    print(f"      he read {len(row['stdout'])} bytes of her day without leaving his desk")

    # 4. And she can do neither, on her own machine.
    if vm.ssh(f"sudo -n -u {child} sudo -n omahouse profile enforce {child} --off",
              check=False)[0] == 0:
        raise Failed(f"{child} switched her own rules off")
    code = vm.ssh(f"sudo -n -u {child} curl -s -m 5 -o /dev/null -w '%{{http_code}}' "
                  f"http://127.0.0.1:{PORT}/v1/runs", check=False)[1].strip()
    if code.endswith("200"):
        raise Failed(f"{child} reached the console on her own machine: {code}")
    print(f"      {child}: no sudo, and her own machine's console answered her {code}")
