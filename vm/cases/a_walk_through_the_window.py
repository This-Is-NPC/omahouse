"""The window, walked at reading speed, on real Omarchy.

This is not a test and it does not assert its way to a verdict. It exists to be
filmed: `docs/screens.md` in the order that page already puts things, driven
through a real keyboard on the guest's own seat, with a pause after every step
long enough for somebody watching to read the screen.

Kept out of the suites by `DEMO`. A case earns its place by refusing something;
this one earns its place by being watchable, and a suite that ran it would be
paying for pauses nobody is looking at.

    vm/record.sh --machine omarchy --demo

The camera is held until `FILM: rolling`. Everything before that -- the package
going on, the greeter, the password -- is minutes of nothing worth watching.

**The operator's session, not the subject's.** `docs/screens.md` §1 to §7 are
about the face somebody in wheel gets, and julia's face has nothing to press on
purpose. So SDDM is pointed at howl before the greeter is answered, and put back
where it was afterwards.
"""

import time

MACHINE = "omarchy"
DEMO = True
WHY = "docs/screens.md, walked at reading speed for the camera"

# Long enough to read a screen, short enough that the film is not a hostage.
BEAT = 3.5
SETTLE = 1.2


def run(vm):
    Failed = vm.Failed
    operator, subject = vm.operator, vm.subject
    try:
        walk(vm, operator, subject)
    finally:
        # Whatever happened, the greeter goes back to asking about the subject.
        # A demo that dies halfway and leaves SDDM pointed at the operator
        # blocks the next run before it even starts, and the message it gives is
        # about julia rather than about the demo -- measured, twice.
        vm.root(f"-u root sh -c \"pkill -x omahouse-studio; "
                f"sed -i 's/^User=.*/User={subject}/' /var/lib/sddm/state.conf\"",
                check=False)


def walk(vm, operator, subject):
    Failed = vm.Failed

    # -- everything the camera should not see --------------------------------
    vm.keyboard()
    was = vm.root("cat /var/lib/sddm/state.conf", check=False)[1]
    if f"User={operator}" not in was:
        vm.root("sed -i 's/^User=.*/User=%s/' /var/lib/sddm/state.conf" % operator)
        vm.root("systemctl restart sddm")

    def greeter_is_up():
        return bool(vm.ssh("pgrep -f sddm-greeter || true", check=False)[1].strip())

    if not vm.pid_of("Hyprland", operator):
        # The greeter has to be *listening* before a password is typed at it.
        # Typing six seconds after `systemctl restart sddm` sends the whole word
        # into a screen that is not there yet, and the wait afterwards then
        # blames the session for never starting.
        vm.wait_for(greeter_is_up, vm.pace["patience_seconds"],
                    "the greeter to come up for the operator")
        time.sleep(4)

        # Twice, because the first keystroke sometimes lands while the field is
        # still taking focus and the word arrives one letter short. A second
        # attempt costs four seconds and saves a run.
        for attempt in (1, 2):
            vm.type_text(vm.about.get("operator_password", operator))
            vm.press("28:1 28:0")
            deadline = time.time() + vm.pace["boot_seconds"] / 2
            while time.time() < deadline:
                if vm.pid_of("Hyprland", operator):
                    break
                time.sleep(2)
            if vm.pid_of("Hyprland", operator):
                break
            if attempt == 2:
                raise Failed(f"the greeter never let {operator} through. "
                             "Look at `journalctl -u sddm` on the guest.")
            vm.press("14:1 14:0" * 20)
            time.sleep(3)
        time.sleep(8)

    # A profile for the walk to be about does not exist yet; the walk makes it.
    vm.root(f"omahouse profile remove {subject} --keep-account", check=False)
    vm.start_daemon()

    def as_operator(command):
        env = (f"XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 "
               f"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus")
        return vm.root(f"-u {operator} env {env} {command}", check=False)

    as_operator("setsid --fork sh -c 'cd /tmp && exec uwsm app -- omahouse-studio' "
                "</dev/null >/dev/null 2>&1")
    vm.wait_for(lambda: bool(vm.pid_of("omahouse-studio", operator)),
                vm.pace["patience_seconds"],
                f"the studio window in {operator}'s session")
    time.sleep(4)

    # -- and from here, the camera is on -------------------------------------
    print("      FILM: rolling")

    def beat(narration, keys=None, text=None, pause=BEAT):
        """One step of the walk: say it, do it, then hold long enough to read."""
        print(f"      {narration}")
        time.sleep(SETTLE)
        if text is not None:
            vm.type_text(text)
            time.sleep(SETTLE)
        if keys:
            # One press for the whole thing. `?` is shift held *across* slash,
            # and sending them as two presses types `/`.
            vm.press(keys)
        time.sleep(pause)

    ENTER, ESC, TAB = "28:1 28:0", "1:1 1:0", "15:1 15:0"

    def authenticate(why):
        """Answer the polkit prompt, because every write raises one.

        The studio is never root: it reads with no privilege at all and every
        write it does is `pkexec omahouse <verb>`. The policy asks `auth_admin`
        and not `auth_admin_keep` on purpose -- what is being changed is
        somebody else's evening, and a five minute window where the machine will
        do it again without asking is a window where the person it is about is
        standing at the same keyboard.

        So the prompt is not in the way of the demonstration. It *is* part of
        it, and the first version of this walk typed the whole rest of the
        script into the password field.
        """
        print(f"      {why}: every write asks, because the studio is never root")
        time.sleep(2.5)
        vm.type_text(vm.about.get("operator_password", operator))
        time.sleep(0.8)
        vm.press(ENTER)
        time.sleep(3.5)
    letter = {"n": "49:1 49:0", "a": "30:1 30:0", "m": "50:1 50:0",
              "b": "48:1 48:0", "l": "38:1 38:0", "h": "35:1 35:0",
              "s": "31:1 31:0", "o": "24:1 24:0", "plus": "42:1 13:1 13:0 42:0",
              "1": "2:1 2:0", "2": "3:1 3:0", "3": "4:1 4:0", "4": "5:1 5:0",
              "slash": "53:1 53:0", "question": "42:1 53:1 53:0 42:0"}

    # 1. Opening it -- docs/screens.md §1
    beat("House Rules, opened by somebody in wheel: the people this machine "
         "knows, and the header says which face this is", pause=BEAT + 2)
    beat("`?` is the whole key sheet, and every key on it is answered",
         keys=letter["question"], pause=BEAT + 2)
    beat("back to the window", keys=ESC)

    # 2. Putting an account under rules -- §2
    beat("`n` opens a new profile: it asks for an account this machine already has",
         keys=letter["n"])
    beat(f"typing {subject}", text=subject)
    beat("and it is written -- observing, allowing everything, which is where "
         "every profile starts", keys=ENTER, pause=BEAT + 2)
    authenticate("writing the profile")

    # 3. Saying which programs may open -- §3
    beat("`2` is the programs of whoever is under the cursor", keys=letter["2"])
    beat("`a` releases one, and the picker offers what is really installed",
         keys=letter["a"], pause=BEAT + 1)
    beat("Chromium", text="chromium")
    beat("released", keys=ENTER, pause=BEAT + 1)
    authenticate("releasing a program")
    beat("`m` puts a clock on that program", keys=letter["m"])
    beat("forty-five minutes a day", text="45m")
    beat("written", keys=ENTER, pause=BEAT + 1)
    authenticate("putting a clock on it")

    # 4. Putting a clock on the day -- §4
    beat("`3` is the day itself: the session, and every budget beside it",
         keys=letter["3"], pause=BEAT + 1)
    beat("`m` here is the session's own clock", keys=letter["m"])
    beat("two hours", text="2h")
    beat("and when it runs out, the session ends", keys=ENTER, pause=BEAT + 2)
    authenticate("the session's own clock")

    # 5. Saying which sites open -- §5
    beat("`4` is the sites", keys=letter["4"], pause=BEAT + 1)
    beat("`b` stops one opening", keys=letter["b"])
    beat("tiktok.com, and its subdomains with it", text="tiktok.com")
    beat("written into the browser's own policy by the daemon, on its next cycle",
         keys=ENTER, pause=BEAT + 2)
    authenticate("blocking a site")

    # 6. Taking something back -- §6, and the verb a household really uses
    beat("`3` again, and `+` hands over more time today", keys=letter["3"])
    beat("more today", keys=letter["plus"], pause=BEAT)
    beat("ten minutes, which die with the day and leave nothing to undo",
         text="10")
    beat("handed over", keys=ENTER, pause=BEAT + 2)
    authenticate("handing over more time")

    # 7. Finding your way around -- §7
    beat("`1` is back to the people, and the profile is there with its rules on it",
         keys=letter["1"], pause=BEAT + 2)

    print("      FILM: that is the walk")


