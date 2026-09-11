# What does not work yet

Every defect omahouse has, in one place, each with what to do in the meantime.
**Nothing else in this documentation names a defect that is not here**, and
nothing here has a fix in omahouse today.

The first two were measured on a real Omarchy 4.0.2, driven by keyboard and
captured frame by frame; the pictures come from that run, where the account
under rules was called `kid`. The rest are in the code as written.

---

## 1. If the screen locks on idle, the last warnings go unseen

**What happens.** Omarchy's idle service locks the screen and blanks the monitor
a second or two later. The one-minute warning and the grace warning go out
behind the lock screen, so somebody who steps away comes back to a session
already ended, having seen nothing:

![The hyprlock lock screen: blurred wallpaper and the Enter Password box in the middle. This is what the session's last two warnings went out behind.](../vm/shots/15-hyprlock-ocultou-avisos.png)

**What to do in the meantime.** Treat the five-minute warning as the last
reliable one. If a profile really has to warn to the end, turn the idle lock off
for that account — knowing that is loosening something else.

## 2. When the session runs out, the screen goes black

**What happens.** SDDM 0.21 reads a session ended by `loginctl terminate-user`
as `Process crashed` and does nothing further: no greeter, no new display.

![An entirely black screen. This is what SDDM leaves behind after loginctl terminate-user.](../vm/shots/16-tela-preta-pos-logout.png)

The demonstration VM carries a patch service that brings SDDM back up when
`seat0` is left with no session, and it took 9 seconds. **That service is not
part of omahouse.** On a stock Omarchy there is nothing that brings the greeter
back.

**This one is a known limitation and not work in progress.** The defect is
SDDM's: nothing omahouse can write changes how SDDM reads a session it did not
end itself. Shipping a watchdog for somebody else's display manager was weighed
and not taken.

**What to do in the meantime.** Install an equivalent service — one that
restarts `sddm` when the seat is left with no session — or do not use a session
budget that logs out, on a machine nobody will be able to reach the console of.

## 3. A refusal loses its reason on the way to the window

**What happens.** Only the last line the CLI printed reaches the status bar. The
sentence that says *what* was refused and why — `profile add: howl is in wheel,
and an administrator does not fiscalise themselves by accident.` — is the line
above it, and it is dropped. What is on screen is the advice with the verdict
missing:

![The people view with the status bar reading, in red, "Take the account out of wheel first, or write the profile for somebody else."](img/19-operator-refusal.png)

**What to do in the meantime.** Run the same verb in a terminal to read the
whole refusal.

## 4. A scope nothing can name has no screen in the window

**What happens.** `omahouse status` reports these prominently — a
`tmux-spawn-<uuid>.scope` with twenty processes in it is somebody at the
keyboard, and the design asks for what cannot be accounted for to be said out
loud. The window computes the number for every profile and no view reads it.
What the status bar does show is the other number, the processes in
`session.slice`, which is a different fact.

**What to do in the meantime.** Use `omahouse status` for that number.

---

## What is not a defect, and is easy to mistake for one

**A site that will not open for you either.** The browser policy is one file for
the whole machine, and that was weighed and taken rather than overlooked —
[`design.md` §11](design.md) has the argument, and
[how to stop a site opening](how-to-block-sites.md) says it where somebody is
about to write the rule.

**A number under `Out of reach` that is never zero.** Processes in
`session.slice` cannot be counted or closed, and the compositor's own processes
are always in it. It is reported rather than hidden.

**A session budget that is already running with nothing on screen.** The session
is the budget whose selector is `*`, and Omarchy's own utilities are live app
scopes from login onwards. What is held out of `*` is the session's own
furniture — `udiskie` and `omarchy-hyprland-monitor-watch`, which are not
evidence that anybody is at the keyboard. Everything else somebody opened is,
and it counts from login. [`design.md` §5](design.md) has the argument.

**A blocked site with no count of attempts and no message on it.** A managed
policy blocks inside Chromium and reports nothing out, so omahouse never learns
the attempt happened at all. There is no number to show and there cannot be one.
