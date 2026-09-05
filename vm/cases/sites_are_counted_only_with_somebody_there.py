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
docs/design.md §5.2. This file is that run written down so it can be run again;
it has not yet been executed through `vm/e2e.py --machine omarchy` end to end,
and the first person to run it should expect to fix a wait rather than a claim.

Two things the hand run found that are not in `.temp/docs/vm-runbook.md`:

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

    julia = vm.subject
    day = f"/var/lib/omahouse/{julia}/{vm.today()}.json"

    def screen():
        return vm.root("cat /sys/class/drm/card0-Virtual-1/dpms", check=False)[1].strip()

    def hypr(command):
        signature = vm.root(f"ls /run/user/{vm.uid}/hypr", check=False)[1].split()[0]
        return vm.root(
            f"-u {julia} env XDG_RUNTIME_DIR=/run/user/{vm.uid} WAYLAND_DISPLAY=wayland-1 "
            f"HYPRLAND_INSTANCE_SIGNATURE={signature} hyprctl {command}", check=False)[1]

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
    # opened the port, and Chromium spawned `omahouse meter` as julia.
    vm.wait_for(lambda: vm.ssh("pgrep -f 'omahouse meter'", check=False)[0] == 0,
                vm.pace["patience_seconds"],
                "the meter's native messaging host to be spawned by the browser")
    who = vm.ssh("ps -o user= -C omahouse | sort -u", check=False)[1]
    if julia not in who:
        raise Failed(f"the host is not running as {julia}: {who!r}")

    # A page that is none of the three, so the first site's clock starts when it
    # is asked for and not before.
    go("about:blank")
    time.sleep(4)

    # A clean day with time in it. The grants are three commands and not one:
    # `grant` takes `--session` or one `--budget`, never both.
    vm.stop_daemon()
    vm.root(f"rm -f {day}")
    for what in ("--session 180m", "--budget chromium=180m",
                 "--budget org.chromium.Chromium=180m"):
        vm.root(f"omahouse grant {julia} {what}")
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
    # cannot tell -- `.temp/spike-extension.md` §5 measured one answering `active`
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
    came_back = time.time()

    hypr("dispatch 'hl.dsp.dpms(\"on\")'")
    vm.wait_for(lambda: screen() == "On", vm.pace["patience_seconds"],
                "the screen to come back")
    time.sleep(seconds // 2)

    # Stopped, so that the end of the counting is a moment this case knows rather
    # than whenever the report happened to be read.
    vm.stop_daemon()
    ended = time.time()

    import json
    written = json.loads(vm.root(f"cat {day}"))
    sites = written.get("sites", {})
    presence = written.get("presence", {})

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
    # The last site again, for the lit part after the screen came back.
    last = list(expected)[-1]
    expected[last] += ended - came_back

    slack = 6
    for site, wanted in expected.items():
        got = sites.get(site, 0)
        if abs(got - wanted) > slack:
            raise Failed(f"{site}: the report says {got}s and the screen had it for "
                         f"{wanted:.0f}s\n    the whole of it: {sites}")
        print(f"      {site:16} report {got:3}s   screen {wanted:5.0f}s")

    # And the dark window, which is the assertion this case exists for. The
    # browser was reporting a site throughout it and not one second of it was
    # billed.
    was_dark = came_back - went_dark
    if presence.get("screen-off", 0) < was_dark - slack:
        raise Failed(f"the screen was off for {was_dark:.0f}s and the day only records "
                     f"{presence.get('screen-off', 0)}s of it: {presence}")
    print(f"      {'the dark window':16} report   0s   screen {was_dark:5.0f}s  "
          f"(presence: {presence})")

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
    print("      " + vm.root(f"omahouse report {julia}").replace("\n", "\n      "))
