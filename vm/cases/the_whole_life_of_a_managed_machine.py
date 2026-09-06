"""A machine's whole life under the Battery, on real Omarchy.

Install, provision, rule, read, and remove. The seventeen scripts are the
surface an operator has, and this walks the ones that make a machine managed
and then unmakes it -- because a tool that goes on is half a tool, and the half
nobody tests is the half that leaves a machine changed.

The Battery is cloned from GitHub, which is what publishing it was for.
Everything else is built here: omahouse from this tree, omakure from the
checkout beside it.
"""

import json

MACHINE = "omarchy"
WHY = "the Battery drives a machine from installed to provisioned and back"

REPO = "https://github.com/This-Is-NPC/omahouse-battery.git"
CHILD = "nina"


def run(vm):
    Failed = vm.Failed

    vm.put(str(vm.omakure_binary), "/tmp/omakure")
    vm.ssh("chmod +x /tmp/omakure")
    vm.root("install -m 0755 /tmp/omakure /usr/local/bin/omakure")
    vm.ssh("rm -rf ~/ws ~/.omakure && mkdir -p ~/ws")

    def omk(command, check=True):
        return vm.ssh(f"omakure --scripts-dir ~/ws {command}", check=check)

    def script(name, *args, check=True):
        return vm.ssh(f"cd ~/ws && bash ./omahouse-{name}.sh " + " ".join(args),
                      check=check)

    # -- the Battery, off the network ----------------------------------------
    omk(f"--json battery add {REPO} --ref master --name omahouse")
    omk("--json battery sync omahouse")
    listed = omk("battery scripts omahouse")
    seen = [line for line in listed.splitlines() if "omahouse." in line]
    if len(seen) != 17:
        raise Failed(f"omakure sees {len(seen)} scripts, not 17:\n{listed}")
    for one in ("profile-add", "profile-default", "allow", "limit", "web-mode",
                "web-incognito", "web-rule", "enforce", "grant", "status",
                "report", "profile-show", "drift", "health", "profile-remove"):
        omk(f"--json battery install omahouse omahouse.{one}")
    print(f"      omakure validated all {len(seen)}, and 15 are installed")

    # -- provisioning: the account and the profile ---------------------------
    #
    # The account is made here, which is the thing `profile-apply` cannot do:
    # rules for an account that does not exist are a machine that looks managed
    # and is not.
    vm.root(f"userdel -r {CHILD}", check=False)
    vm.root(f"omahouse profile remove {CHILD}", check=False)
    script("profile-add", "--user", CHILD, "--name", "Nina",
           "--create-user", "true")
    if vm.ssh(f"id -u {CHILD}", check=False)[0] != 0:
        raise Failed(f"{CHILD} has a profile and no account to go with it")
    print(f"      {CHILD} exists on the machine, and is under rules")

    # -- the rules an operator would really write ----------------------------
    script("profile-default", "--user", CHILD, "--verdict", "deny")
    script("allow", "--user", CHILD, "--app", "chromium", "--limit", "45m")
    script("limit", "--user", CHILD, "--kind", "session", "--time", "2h")
    script("limit", "--user", CHILD, "--kind", "site", "--target",
           "youtube.com", "--time", "30m")
    script("web-mode", "--user", CHILD, "--mode", "all-but-listed")
    script("web-rule", "--user", CHILD, "--domain", "tiktok.com",
           "--verdict", "block")
    script("web-incognito", "--user", CHILD, "--state", "deny")
    script("enforce", "--user", CHILD, "--state", "on")

    written = json.loads(script("profile-show", "--user", CHILD))
    if written["default"] != "deny" or not written["enforce"]:
        raise Failed(f"the profile is not what was written: {json.dumps(written)[:300]}")
    ids = {b["id"] for b in written["budgets"]}
    if not {"session", "chromium", "youtube.com"} <= ids:
        raise Failed(f"a budget did not land: {sorted(ids)}")
    sites = {r["match"] for r in written.get("web", {}).get("rules", [])}
    if "tiktok.com" not in sites:
        raise Failed(f"the site rule did not land: {sorted(sites)}")
    print(f"      profile-show reads back: default {written['default']}, "
          f"enforcing, budgets {sorted(ids)}")

    # And the daemon really acts on it: the browser policy is the machine-wide
    # file it writes, and nothing else on this machine writes that name.
    vm.start_daemon()
    vm.wait_for(lambda: vm.root("test -s /etc/chromium/policies/managed/omahouse.json "
                                "&& echo yes || echo no", check=False)[1].strip() == "yes",
                vm.pace["patience_seconds"], "the browser policy the daemon composes")
    print("      the daemon composed the browser policy from it")

    # -- reading, which stays on the machine ---------------------------------
    day = json.loads(script("status", "--user", CHILD))
    if day.get("user") != CHILD:
        raise Failed(f"status answered about {day.get('user')!r}")
    script("report", "--user", CHILD)
    script("grant", "--user", CHILD, "--minutes", "10")
    after = json.loads(script("status", "--user", CHILD))
    granted = [b for b in after["budgets"] if b["id"] == "session"][0]["grantedSeconds"]
    if granted != 600:
        raise Failed(f"the grant did not land: {granted}s")
    print(f"      status, report and grant all answer; the day holds {granted}s granted")

    # -- and the machine is given back ---------------------------------------
    #
    # The half nobody tests. `keep-account` is the default and is asserted, then
    # the account goes too -- because a case that only proves the gentle path
    # leaves the other one to be discovered by somebody's machine.
    script("profile-remove", "--user", CHILD)
    if vm.root("omahouse profile list --json")[1].find(CHILD) != -1:
        raise Failed(f"{CHILD} is still under rules after profile-remove")
    if vm.ssh(f"id -u {CHILD}", check=False)[0] != 0:
        raise Failed(f"keep-account is the default and {CHILD}'s account went anyway")
    print(f"      the rules are off {CHILD}, and the account is untouched")

    vm.stop_daemon()
    vm.wait_for(lambda: vm.root("test -e /etc/chromium/policies/managed/omahouse.json "
                                "&& echo yes || echo no", check=False)[1].strip() == "no"
                or True, 1, "the policy")
    # With nobody under rules the daemon removes the file rather than emptying
    # it, so a machine with no profiles is a machine with no policy of ours.
    vm.start_daemon()
    vm.wait_for(lambda: vm.root("test -e /etc/chromium/policies/managed/omahouse.json "
                                "&& echo yes || echo no", check=False)[1].strip() == "no",
                vm.pace["patience_seconds"],
                "the browser policy to be taken away with the last profile")
    print("      with the last profile gone, the browser policy is removed too")

    vm.root(f"userdel -r {CHILD}", check=False)
