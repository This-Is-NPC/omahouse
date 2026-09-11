"""The number, against what was really on the screen -- docs/design.md §5.2.

A real Omarchy session, a real Chromium with the meter force-installed in it,
three sites visited in a known order for a known number of seconds, and the
screen turned off with the last one still in the front tab. Then `omahouse
report` beside the wall clock.

This is the case that decides whether time per site is worth having, and it is
here rather than in the unit suite for the reason `session_slice_untouched` is:
every part of it can be faked and the whole of it cannot. The extension really
has to install off-store under policy, the service worker really has to survive,
the native messaging host really has to be spawned as the child, the file really
has to be written where the daemon looks, and the presence really has to come
from a monitor that really went dark.

**Provenance.** The sequence below was driven by hand against `omahouse-omarchy`
on 2026-09-04 between 21:26 and 21:28 and the numbers it produced are in
docs/design.md §5.2. It has since been run by `vm/e2e.py --machine omarchy
--case sites` at the quick pace, and every claim it makes held the first time it
was asked automatically: the `.crx` installed under policy, the service worker
opened the port, Chromium spawned `omahouse meter` as kid, three sites came
back as three rows within a tick of the wall clock, and the dark window billed
nothing.

**What did have to change was one of its own clocks.** The lit window after the
screen came back was opened *before* the dispatch that turned it on, so it
counted the two ssh round trips and the poll that follow -- seconds the screen
was still off -- as seconds the site should have been billed for, and then read
that as the daemon losing time. It is the failure a hand run cannot have,
because a hand cannot be two round trips early. The window now opens where
`wait_for` says the screen really is on, and the tail is a whole
`site_seconds` rather than half of one, so a single tick of slop is the same
fraction of it as it is of the other three.

Two things the hand run found that are not in the VM runbook:

  * `hyprctl dispatch dpms off` is dead on this Hyprland. The dispatcher argument
    is Lua now, and the form that works is
    `hyprctl dispatch 'hl.dsp.dpms("off")'`. The old spelling fails with a parse
    error and an exit code nobody checks, so a case using it turns the screen off
    in the log and not on the machine -- which is the expensive kind of green.
  * `virsh screenshot` on this VM's `bochs` framebuffer returns the last drawn
    frame and does **not** go black when DPMS does. A screenshot cannot tell the
    dark window from the lit one here; `/sys/class/drm/*/dpms` can, and it is
    what this asserts on.
"""

import time

MACHINE = "omarchy"

WHY = "three sites in a real browser, and the dark minute in the middle counts nothing"

#: The three sites, in order. Ordinary pages that load quickly and are not each
#: other's subdomains, so the registrable domains in the report are three
#: distinct rows. `en.wikipedia.org` is there on purpose: what has to come out
#: of the report is `wikipedia.org`, which is the reduction doing its job.
SITES = ["example.com/", "en.wikipedia.org/wiki/Lan_house", "archlinux.org/"]


def run(vm):
    Failed = vm.Failed
    seconds = vm.pace["site_seconds"]

    kid = vm.subject
    day = f"/var/lib/omahouse/{kid}/{vm.today()}.json"

    def screen():
        # Every connected connector, the way `screenStateFromDrm` reads them:
        # `On` if any of them is on. The connector is found rather than named --
        # `card0-Virtual-1` is what `bochs` gives this machine and `vkms` gives
        # the other one, and a case that spelled one of them out would be a case
        # that answered the empty string on the other and read as a dark screen.
        seen = vm.root(
            "sh -c 'for c in /sys/class/drm/*/status; do "
            "[ \"$(cat $c)\" = connected ] || continue; "
            "cat \"$(dirname $c)/dpms\"; done'", check=False)[1].split()
        if not seen:
            raise Failed("no connected screen under /sys/class/drm to read a dpms from")
        return "On" if "On" in seen else "Off"

    def hypr(command):
        listed = vm.root(f"ls /run/user/{vm.uid}/hypr", check=False)[1].split()
        if not listed:
            raise Failed(f"no Hyprland instance under /run/user/{vm.uid}/hypr to dispatch to")
        # `hyprctl dispatch dpms off` is dead on this Hyprland: the dispatcher
        # argument is Lua now. The old spelling fails with a parse error and an
        # exit code nobody checks, so the caller reads the output rather than
        # trusting the exit status -- a screen turned off in the log and not on
        # the machine is the expensive kind of green.
        said = vm.root(
            f"-u {kid} env XDG_RUNTIME_DIR=/run/user/{vm.uid} WAYLAND_DISPLAY=wayland-1 "
            f"HYPRLAND_INSTANCE_SIGNATURE={listed[0]} hyprctl {command}", check=False)[1]
        if "ok" not in said.lower():
            raise Failed(f"hyprctl {command} said {said.strip()!r} rather than ok")
        return said

    def go(where):
        """Ctrl+L, the address, Enter -- the way a person types one."""
        vm.press("29:1 38:1 38:0 29:0")
        time.sleep(1)
        vm.type_text(where)
        vm.press("28:1 28:0")

    # The browser, through Omarchy's own binding. Not `uwsm app` by hand: what is
    # under test includes the scope the browser really gets, and round 4 of
    # docs/design.md is the story of a launcher changing that.
    if not vm.pid_of("chromium"):
        vm.press("125:1 42:1 48:1 48:0 42:0 125:0")
        vm.wait_for(lambda: bool(vm.pid_of("chromium")), vm.pace["patience_seconds"],
                    "Chromium to open")
        time.sleep(5)

    # The native messaging host, which is the whole chain in one fact: the
    # extension installed off-store under policy, its service worker started, it
    # opened the port, and Chromium spawned `omahouse meter` as kid.
    #
    # Asked as `pgrep -u kid -x omahouse`. It used to be
    # `pgrep -f 'omahouse meter'`, which is an assertion that cannot fail: the
    # harness reaches the guest over ssh, sshd runs the command inside a shell,
    # and that shell's own command line holds the words being searched for -- so
    # it answered its own pid, every time, whether or not a host existed. The
    # line below it saved this case from being green on nothing;
    # `the_package_puts_the_meter_in_and_takes_it_out` is where the same mistake
    # was caught, by failing on a host that had never been there.
    vm.wait_for(lambda: bool(vm.root(f"pgrep -u {kid} -x omahouse || true",
                                     check=False)[1].split()),
                vm.pace["patience_seconds"],
                "the meter's native messaging host to be spawned by the browser")
    who = vm.ssh("ps -o user= -C omahouse | sort -u", check=False)[1]
    if kid not in who:
        raise Failed(f"the host is not running as {kid}: {who!r}")

    # A page that is none of the three, so the first site's clock starts when it
    # is asked for and not before.
    go("about:blank")
    time.sleep(4)

    # A clean day with time in it. The grants are three commands and not one:
    # `grant` takes `--session` or one `--budget`, never both.
    vm.stop_daemon()
    vm.root(f"rm -f {day}")
    # One budget covers both of Chromium's ids -- `vm/provision-omarchy.sh`
    # writes it with one `allow` -- so there is one to hand more of.
    for what in ("--session 180m", "--budget chromium=180m"):
        vm.root(f"omahouse grant {kid} {what}")
    vm.start_daemon()
    time.sleep(1)

    began = time.time()
    timeline = []
    for site in SITES:
        go(site)
        timeline.append((time.time(), site))
        time.sleep(seconds)

    # The screen off with the last site still in the front tab. This is the
    # crossing: the browser goes on reporting archlinux.org, because a browser
    # cannot tell -- the browser spike measured one answering `active`
    # through twenty-five minutes of a dark monitor -- and omahouse asks the
    # kernel instead.
    dark = vm.pace["screen_off_seconds"]
    if dark:
        # The long regime waits for the machine's own idle cycle, which is what a
        # child leaving the room really does. Measured at about five minutes on
        # this VM, with no hypridle and no hyprlock in it: it is the display
        # blanking.
        vm.wait_for(lambda: screen() == "Off", dark,
                    "the screen to go dark on its own, with nobody touching anything")
    else:
        # The quick regime asks for it, which proves the same crossing in seconds.
        hypr("dispatch 'hl.dsp.dpms(\"off\")'")
        vm.wait_for(lambda: screen() == "Off", vm.pace["patience_seconds"],
                    "the screen to go dark")
    went_dark = time.time()
    if not vm.pid_of("chromium"):
        raise Failed("Chromium closed while the screen was off, so the dark window "
                     "proves nothing about the crossing")
    time.sleep(seconds)
    if screen() != "Off":
        raise Failed("the screen came back on inside the dark window")
    still_dark = time.time()

    # The screen back on, and the lit window starting **after** it really is on
    # rather than before the dispatch that turns it on. Two ssh round trips and a
    # poll sit between the two moments, and every second of them is a second the
    # screen was still off: a window opened at the earlier moment credits the
    # site with time nobody could see it for, and then reads as the daemon having
    # lost seconds. That is a bookkeeping fault in the case and not a fault in
    # the meter, and it is the one the first automated run of this file found.
    hypr("dispatch 'hl.dsp.dpms(\"on\")'")
    lit_again = vm.wait_for(lambda: screen() == "On", vm.pace["patience_seconds"],
                            "the screen to come back")
    # A whole window and not half of one. The extension repeats itself every five
    # seconds and the daemon refuses a line older than fifteen, so the first beat
    # after a dark stretch is worth several seconds of nothing being billed --
    # measured below, and printed whether or not this passes. A tail shorter than
    # the other windows makes that beat most of the window.
    time.sleep(seconds)

    # Stopped, so that the end of the counting is a moment this case knows rather
    # than whenever the report happened to be read.
    vm.stop_daemon()
    # And the moment is the daemon's own last write, not the moment the ssh that
    # stopped it came back. Those are seconds apart -- a `systemctl stop` over
    # ssh is a connection, an authentication and a unit teardown -- and taking
    # the later one charged every one of those seconds to the tail as time the
    # site was in front while the daemon was already dead. It is the same
    # bookkeeping fault this case fixed at the other end of the window, at
    # `lit_again`, left standing at this end: one run had it cost 8 seconds and
    # go red, the next 3 and go green, with the meter behaving identically in
    # both. A case whose expectation moves by eight seconds between two runs of
    # the same mechanism is measuring its own ssh and calling it the meter.
    #
    # The ledger is written once a cycle whenever it changed, and through a lit
    # tail it changed every cycle, so its last write is the last cycle that was
    # counted. `stat` and `date` come back from one call and one clock, so the
    # age between them carries no skew; only the age crosses to this clock.
    aged = vm.root(f"sh -c 'stat -c %Y {day}; date +%s'").split()
    ended = time.time() - (int(aged[1]) - int(aged[0]))

    import json
    written = json.loads(vm.root(f"cat {day}"))
    sites = written.get("sites", {})
    presence = written.get("presence", {})

    # What the browser really said, and when. Printed every run and not only on a
    # failure: the file is the whole of what the daemon had to go on, and a row
    # that came out short is either a beat that never arrived or a tick that was
    # refused, which are two different bugs and look identical in the report.
    beats = vm.root(f"tail -n 12 /run/user/{vm.uid}/omahouse/focus", check=False)[1]

    # What the wall clock says each site was in front for. The tick is two
    # seconds and a page takes a moment to load and be reported, so the
    # comparison is against a window and not against a number -- but the window
    # is narrow, and a mechanism that was billing the wrong tab or billing a dark
    # room would miss it by tens of seconds rather than by two.
    expected = {}
    for index, (at, site) in enumerate(timeline):
        until = timeline[index + 1][0] if index + 1 < len(timeline) else went_dark
        name = site.split("/")[0]
        name = name.split(".", 1)[1] if name.startswith("en.") else name
        expected[name] = until - at
    # The last site again, for the lit part after the screen came back. Kept
    # apart as well as added in, because a tail that bills nothing and a head
    # that bills nothing are the same number here and not the same finding.
    last = list(expected)[-1]
    tail = ended - lit_again
    expected[last] += tail

    def whole(reason):
        return (f"the whole of it: sites {sites}, presence {presence}\n"
                f"    {last} was lit for {expected[last] - tail:.0f}s, dark for "
                f"{still_dark - went_dark:.0f}s, then lit again for {tail:.0f}s\n"
                f"    the last beats the browser sent:\n      "
                + beats.strip().replace("\n", "\n      ") + f"\n    {reason}")

    slack = 6
    for site, wanted in expected.items():
        got = sites.get(site, 0)
        if abs(got - wanted) > slack:
            raise Failed(f"{site}: the report says {got}s and the screen had it for "
                         f"{wanted:.0f}s\n    " + whole("more than the %ds this allows"
                                                        % slack))
        print(f"      {site:16} report {got:3}s   screen {wanted:5.0f}s")

    # And the dark window, which is the assertion this case exists for. The
    # browser was reporting a site throughout it and not one second of it was
    # billed. `still_dark` and not the moment the screen came back on: the window
    # asserted about has to be one the screen was provably off for all of, and
    # the seconds between the last check and the dispatch that lit it are seconds
    # nobody looked.
    was_dark = still_dark - went_dark
    if presence.get("screen-off", 0) < was_dark - slack:
        raise Failed(f"the screen was off for {was_dark:.0f}s and the day only records "
                     f"{presence.get('screen-off', 0)}s of it\n    "
                     + whole("the dark window is not in the day"))
    print(f"      {'the dark window':16} report   0s   screen {was_dark:5.0f}s  "
          f"(presence: {presence})")
    print(f"      {last} again once it was lit: {tail:.0f}s of window")

    # The journal said so at the time, in the sentence an operator would read.
    journal = vm.journal()
    if "not counted" not in journal:
        raise Failed("the journal never says a site was not counted, so the dark window "
                     "may simply not have happened:\n" + journal[-2000:])

    # And nothing about the apps changed because of any of it. docs/design.md §5
    # bills running time, which a dark screen does not alter, and this is where a
    # step that quietly started acting on presence would show.
    running = ended - began
    for budget in ("chromium", "session"):
        spent = written["budgets"].get(budget, 0)
        if spent < running - 3 * slack:
            raise Failed(f"the {budget} budget gained {spent}s over {running:.0f}s of a "
                         "session with a browser open, so something stopped counting when "
                         "the screen went dark")
    print(f"      budgets untouched by the dark window: {written['budgets']}")
    print("      " + vm.root(f"omahouse report {kid}").replace("\n", "\n      "))
