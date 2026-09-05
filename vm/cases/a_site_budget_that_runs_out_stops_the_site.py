"""The teeth on the time per site -- docs/design.md §5.3.

§5.2 counted the number and acted on nothing, and the case beside this one is
what proved the number honest against a real screen. This is the stage after: a
site budget of a few seconds, a real Chromium browsing until it runs out, and the
three things that have to be true at once.

  * **the warning arrived** -- in the journal, in the sentence a person reads,
    and marked as delivered rather than as attempted;
  * **the site stopped opening** -- read off the browser's own window title,
    which is the only thing on this machine that knows the difference between a
    page and Chromium's block page. §11 is why there is nothing better: the
    policy blocks inside the browser and reports nothing out, so omahouse never
    learns the attempt happened and cannot be asked;
  * **the domain is in `URLBlocklist`** -- in the managed policy file, under the
    key Chromium reads.

And then the half that makes a site budget safe to ship at all: **it comes back
on its own.** An operator hands over ten minutes with the tab still open and the
site opens again, with nothing anywhere having been asked to unblock anything --
the daemon works out from today's ledger which sites are out of time right now
and makes the file say exactly that. The turn of the day is the same path and is
proved by the unit suite with an injected `now`; what is proved here is that the
mechanism fires against a real browser.

**What this case cannot prove, and says so rather than implying it.** Chromium
picks a changed managed policy up through a file watcher on its own schedule, so
every assertion about the browser's behaviour below is a `wait_for` with the
regime's patience on it, and the number of seconds it really took is printed. A
long regime run is what would catch a reload that only misbehaves after minutes.
"""

import json
import time

MACHINE = "omarchy"

WHY = "a site budget runs out, the site stops opening, and a grant opens it again"

#: The site. `example.com` because it loads in a second, because its title is a
#: sentence nobody else's is -- which is what makes the block legible in a window
#: title -- and because blocking it on this machine for ninety seconds costs
#: nobody anything. The policy is per machine (docs/design.md §11), so a case
#: that took youtube.com away would be taking it away from the operator too.
SITE = "example.com"
TITLE = "Example Domain"


def run(vm):
    """The case, and the profile put back before the next one runs.

    `put_the_state_back` restores this machine at the end of the whole run, which
    is what keeps the owner's demonstration profile safe. It is not enough here:
    the budget this case writes is about a site the case beside it browses, and a
    case that changed what its neighbour measures would be a case whose neighbour
    passes or fails depending on the order they ran in.
    """
    was = vm.root("cat /etc/omahouse/profiles.json")
    try:
        _run(vm)
    finally:
        vm.root("tee /etc/omahouse/profiles.json > /dev/null <<'OMAHOUSE_WAS'\n%s\n"
                "OMAHOUSE_WAS" % was.rstrip("\n"), check=False)


def _run(vm):
    Failed = vm.Failed
    seconds = vm.pace["site_seconds"]
    patience = vm.pace["patience_seconds"]

    julia = vm.subject
    day = f"/var/lib/omahouse/{julia}/{vm.today()}.json"
    policy = "/etc/chromium/policies/managed/omahouse.json"

    def hypr(command):
        listed = vm.root(f"ls /run/user/{vm.uid}/hypr", check=False)[1].split()
        if not listed:
            raise Failed(f"no Hyprland instance under /run/user/{vm.uid}/hypr")
        return vm.root(
            f"-u {julia} env XDG_RUNTIME_DIR=/run/user/{vm.uid} WAYLAND_DISPLAY=wayland-1 "
            f"HYPRLAND_INSTANCE_SIGNATURE={listed[0]} hyprctl {command}", check=False)[1]

    def title():
        """What the browser's own window says it is showing.

        The one signal on this machine that tells a page from Chromium's block
        page. A screenshot cannot: `virsh screenshot` on this `bochs`
        framebuffer returns the last drawn frame, and telling two drawn frames
        apart is not something a test should be doing.
        """
        said = hypr("activewindow -j")
        try:
            return json.loads(said).get("title", "")
        except json.JSONDecodeError:
            return ""

    def blocklist():
        rc, out = vm.root(f"cat {policy}", check=False)
        if rc != 0:
            return None
        try:
            return json.loads(out).get("URLBlocklist", [])
        except json.JSONDecodeError:
            raise Failed(f"the managed policy is not JSON:\n{out}")

    def go(where):
        """Ctrl+L, the address, Enter -- the way a person types one."""
        vm.press("29:1 38:1 38:0 29:0")
        time.sleep(1)
        vm.type_text(where)
        vm.press("28:1 28:0")

    # The machine as it was found: no policy of omahouse's on it. Asserted rather
    # than assumed, because every claim below is about this file appearing and
    # going away, and one left over from something else would make all of them
    # read as passing.
    if blocklist() is not None:
        raise Failed(f"there is already an omahouse policy at {policy}; this case cannot "
                     "tell what it wrote from what was there")

    if not vm.pid_of("chromium"):
        vm.press("125:1 42:1 48:1 48:0 42:0 125:0")
        vm.wait_for(lambda: bool(vm.pid_of("chromium")), patience, "Chromium to open")
        time.sleep(5)
    vm.wait_for(lambda: vm.ssh("pgrep -f 'omahouse meter'", check=False)[0] == 0, patience,
                "the meter's native messaging host to be spawned by the browser")

    # A day with the site nearly spent on it, and everything else with time to
    # burn. The limit is whole minutes -- docs/design.md §4 -- so the seconds
    # come from seeding what has already been spent, which is the same shape as a
    # machine that has been on since lunch.
    vm.stop_daemon()
    minutes = vm.whole_minutes(seconds)
    vm.root(f"omahouse limit {julia} --site {SITE}={minutes}m")
    # The demonstration profile's own grace is twenty seconds, which is the one a
    # household would really set and is `[pace.long]`'s. The quick regime asks
    # for its own, and it is the only field of the profile this case changes.
    vm.root("python3 -c " + _patch_grace(vm.pace["grace_seconds"]))
    vm.seed_ledger({SITE: vm.already_spent(seconds)})
    for what in ("--session 180m", "--budget chromium=180m",
                 "--budget org.chromium.Chromium=180m"):
        vm.root(f"omahouse grant {julia} {what}")

    # It loads. This is the before, and without it the after proves nothing: a
    # window title that never said `Example Domain` is a page that never opened,
    # and that is not the same thing as a page that was stopped.
    go(f"{SITE}/")
    vm.wait_for(lambda: TITLE in title(), patience, f"{SITE} to open at all")
    print(f"      before   the window says {title()!r}")

    vm.start_daemon()
    began = time.time()

    # The whole of the budget, spent by browsing. Nothing here touches the
    # keyboard: the site is in the front tab and somebody is in front of the
    # screen, and those two agreeing is what spends it.
    vm.wait_for(lambda: blocklist() is not None, seconds + patience,
                f"{SITE} to run out of time and reach {policy}")
    ran_out = time.time()
    have = blocklist()
    if SITE not in have:
        raise Failed(f"the policy was written and {SITE} is not in it: {have}")
    print(f"      out      {SITE} reached URLBlocklist after {ran_out - began:.0f}s: {have}")

    # The warning, in the journal, in the sentence a person reads -- and marked
    # as delivered. `not sent` is what a notification that never left looks like,
    # and a case that only grepped for the words would pass on one.
    journal = vm.journal()
    lines = [line for line in journal.splitlines() if "stops opening" in line]
    if not lines:
        raise Failed("the journal never says the site stops opening, so nobody was warned "
                     "before it was taken away:\n" + journal[-2000:])
    if any("not sent" in line for line in lines):
        raise Failed("the warning was built and not delivered:\n    "
                     + "\n    ".join(lines))
    print("      said     " + lines[-1].split(": ", 1)[-1])

    # And the browser really stops opening it. Chromium picks a changed managed
    # policy up on its own schedule, so this is a wait with the regime's patience
    # on it rather than an assertion about the next instant -- and how long it
    # took is printed, because that number is the one thing here a quick regime
    # cannot promise anything about.
    asked = time.time()
    def refusedNow():
        go(f"{SITE}/")
        time.sleep(2)
        return TITLE not in title()
    vm.wait_for(refusedNow, patience, f"Chromium to stop opening {SITE}")
    print(f"      shut     the window says {title()!r} after {time.time() - asked:.0f}s")

    # -- and out again --------------------------------------------------------
    #
    # The half that makes this safe to ship. Nothing is asked to unblock
    # anything: the operator hands over ten minutes, and the next cycle works out
    # from today's ledger that the site is no longer out of time and makes the
    # file say so. The turn of the day is the same path, and is proved by the
    # unit suite with an injected `now` rather than by waiting until midnight.
    vm.root(f"omahouse grant {julia} --budget {SITE}=10m")
    vm.wait_for(lambda: blocklist() is None, patience,
                f"{policy} to be taken off the machine once the site had time again")
    print(f"      back     {policy} is gone, and nothing was asked to remove it")

    journal = vm.journal()
    if "opens again" not in journal:
        raise Failed("the journal never says the site opens again, so the release happened "
                     "without a line anybody could find later:\n" + journal[-2000:])

    asked = time.time()
    def opensNow():
        go(f"{SITE}/")
        time.sleep(2)
        return TITLE in title()
    vm.wait_for(opensNow, patience, f"Chromium to open {SITE} again")
    print(f"      open     the window says {title()!r} after {time.time() - asked:.0f}s")

    # And the app half is untouched by any of it. docs/design.md §5 bills running
    # time and a site budget does not get to change that -- this is where a step
    # that had quietly wired the two together would show.
    vm.stop_daemon()
    written = json.loads(vm.root(f"cat {day}"))
    running = time.time() - began
    for budget in ("chromium", "session"):
        spent = written["budgets"].get(budget, 0)
        if spent < running - 20:
            raise Failed(f"the {budget} budget gained {spent}s over {running:.0f}s of a "
                         "session with a browser open, so something stopped counting when "
                         "the site was blocked")
    print(f"      budgets untouched by the block: {written['budgets']}")


def _patch_grace(grace):
    """The one field of the demonstration profile this case changes.

    Written as a `python3 -c` against the file rather than through a verb because
    `grace` has no verb -- it is a field of docs/design.md §4 that this build's
    CLI does not expose, exactly as `vm/e2e.py`'s `make_profile` says. Everything
    else about the profile is the owner's, and `put_the_state_back` restores all
    of it byte for byte.
    """
    import shlex
    return shlex.quote(
        "import json\n"
        "p='/etc/omahouse/profiles.json'\n"
        "d=json.load(open(p))\n"
        "for profile in d['profiles']:\n"
        f"    profile['grace'] = {int(grace)}\n"
        "open(p,'w').write(json.dumps(d, indent=2))\n")
