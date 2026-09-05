"""Scenario A of the spike: the operator and the fiscalised account share a machine.

The parent has an account on the same computer the child uses -- one machine, two
logins, which is the household this started as. The parent's half runs through
Omakure: the node's HTTP API on loopback, the Battery's scripts, and omahouse
underneath doing every decision.

What this case is really asking is whether the operator's console can be built on
that API at all, which comes down to two things the spike measured on the host
and has to see again on a machine:

  - a run started over HTTP carries **arguments**, so the console can say *which*
    account and *how many* minutes;
  - reading the run back brings **stdout**, so `omahouse status --json` reaches
    the parent rather than dying as an exit code.

Neither travels through a Remote Cue -- a Cue carries no arguments and the Health
Plane forbids stdout, usernames and process lists at any depth. So this is the
LAN half of the design, and the Cue is the other one.

The Battery is cloned from GitHub. Everything else is built here.
"""

import json
import time

MACHINE = "poc"
WHY = "the parent drives the child's rules through Omakure's API, on one machine"

REPO = "https://github.com/This-Is-NPC/omahouse-battery.git"
PORT = 7899


def run(vm):
    Failed = vm.Failed

    # The parent is the operator account the manifest already describes: in
    # wheel, with passwordless sudo. The child is the subject, with no
    # privilege. Nothing here creates either -- the machine is already the
    # household.
    parent, child = vm.operator, vm.subject
    if vm.ssh(f"id -nG {child}").strip().split() == ["wheel"]:
        raise Failed(f"{child} is in wheel; this case is about an account without privilege")

    vm.put(str(vm.omakure_binary), "/tmp/omakure")
    vm.ssh("chmod +x /tmp/omakure && sudo install -m 0755 /tmp/omakure /usr/local/bin/omakure")
    vm.ssh("rm -rf ~/ws ~/.omakure && mkdir -p ~/ws")

    def omk(command, check=True):
        return vm.ssh(f"/usr/local/bin/omakure --scripts-dir ~/ws {command}", check=check)

    # A profile to be about, written the way an operator would write it.
    vm.make_profile({"session": 120}, default="allow")

    # -- the Battery, the one thing off the network --------------------------
    omk(f"--json battery add {REPO} --ref master --name omahouse")
    omk("--json battery sync omahouse")
    for script in ("grant", "status", "health"):
        omk(f"--json battery install omahouse omahouse.{script}")

    # -- a token, and the parent's own console --------------------------------
    #
    # Argon2id hashes in the file and the plaintext only in the parent's hand.
    # `omakure token generate` is what writes it, so this case never invents a
    # credential format of its own.
    generated = json.loads(omk("--json token generate --id console "
                               "--scope runs:read --scope runs:write "
                               "--scope scripts:read --scope config:read"))
    token = generated["data"]["token"]
    vm.ssh("cat > ~/tokens.toml", stdin=generated["data"]["tokens_file_entry"])

    # Two processes and not one: `omakure api` serves and does not drain, so a
    # machine that answers HTTP and never runs anything is the shape somebody
    # gets by starting only the first. Measured on the host before this case was
    # written, and worth a machine confirming.
    vm.ssh(f"setsid --fork /usr/local/bin/omakure --scripts-dir ~/ws api "
           f"--bind 127.0.0.1:{PORT} --tokens-file ~/tokens.toml "
           "</dev/null >/tmp/api.log 2>&1", check=False)
    vm.ssh("setsid --fork /usr/local/bin/omakure --scripts-dir ~/ws queue worker "
           "</dev/null >/tmp/worker.log 2>&1", check=False)

    def api(method, path, body=None):
        data = f"-d {json.dumps(json.dumps(body))}" if body else ""
        head = "-H 'Content-Type: application/json'" if body else ""
        out = vm.ssh(f"curl -s -m 10 -X {method} http://127.0.0.1:{PORT}{path} "
                     f"-H 'Authorization: Bearer {token}' {head} {data}", check=False)[1]
        try:
            return json.loads(out)
        except json.JSONDecodeError:
            raise Failed(f"{method} {path} answered something that is not JSON:\n{out[:400]}")

    vm.wait_for(lambda: api("GET", "/v1/health").get("ok") is True,
                vm.pace["patience_seconds"], "the node's API to answer")

    def finished(run_id):
        def settled():
            row = api("GET", f"/v1/runs/{run_id}")["data"]
            return row["state"] not in ("queued", "running")
        vm.wait_for(settled, vm.pace["patience_seconds"], f"run {run_id} to finish")
        return api("GET", f"/v1/runs/{run_id}")["data"]

    # -- the parent hands over ten minutes, by name and by number -------------
    #
    # This is the whole point of the HTTP half. A Cue could start the script and
    # could not say who or how much; here both travel.
    started = api("POST", "/v1/runs",
                  {"script": "omahouse-grant.sh",
                   "args": ["--user", child, "--minutes", "10"]})
    if not started.get("ok"):
        raise Failed(f"the API refused the run: {started}")
    row = finished(started["data"]["run_id"])
    if row["exit_code"] != 0:
        raise Failed(f"grant came back {row['exit_code']}:\n{row['stderr'][:300]}")
    if child not in row["stdout"] or "+10m" not in row["stdout"]:
        raise Failed(f"the arguments did not reach the script:\n{row['stdout'][:300]}")
    print(f"      the parent's console said: {row['stdout'].strip()[:70]}")

    # And omahouse itself agrees, read on the machine rather than from the API.
    granted = json.loads(vm.root(f"omahouse status {child} --json"))
    session = [b for b in granted["budgets"] if b["id"] == "session"]
    if not session or session[0]["grantedSeconds"] != 600:
        raise Failed(f"the day's ledger does not hold the grant: {session}")

    # -- and the parent reads the child's day ---------------------------------
    #
    # The half a Remote Cue cannot do. If this comes back empty the console has
    # nothing to draw and the design is a write-only remote.
    started = api("POST", "/v1/runs",
                  {"script": "omahouse-status.sh", "args": ["--user", child]})
    row = finished(started["data"]["run_id"])
    try:
        document = json.loads(row["stdout"])
    except json.JSONDecodeError:
        raise Failed(f"status did not come back as a document:\n{row['stdout'][:300]}")
    if document.get("user") != child:
        raise Failed(f"the document is about {document.get('user')!r}, not {child!r}")
    print(f"      the parent read back {len(row['stdout'])} bytes of {child}'s day, "
          f"budgets: {[b['id'] for b in document.get('budgets', [])]}")

    # -- the child cannot do any of it ----------------------------------------
    #
    # The account under rules has no privilege, and the console is the parent's.
    # Both halves are asserted, because "she has no sudo" and "she cannot reach
    # the console" are different doors and only one of them is about omahouse.
    denied = vm.ssh(f"sudo -n -u {child} sudo -n omahouse profile enforce {child} --off",
                    check=False)
    if denied[0] == 0:
        raise Failed(f"{child} switched the rules off with sudo")
    reached = vm.ssh(f"sudo -n -u {child} curl -s -m 5 -o /dev/null -w '%{{http_code}}' "
                     f"http://127.0.0.1:{PORT}/v1/runs", check=False)[1].strip()
    if reached.endswith("200"):
        raise Failed(f"{child} read the console's runs without a token: {reached}")
    print(f"      {child}: no sudo, and the API answered her {reached} without a token")
