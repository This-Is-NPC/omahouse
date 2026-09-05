# What does not work yet

Every defect omahouse has, in one place, each with what to do in the meantime.
**Nothing else in this documentation names a defect that is not here**, and
nothing here has a fix in omahouse today.

The first five were measured on a real Omarchy 4.0.2, driven by keyboard and
captured frame by frame; the pictures come from that run, where the account
under rules was called `julia`. The rest are in the code as written, and the
last one was found by running the shipped binary while this page was being
assembled.

---

## 1. Programs opened from the menu cannot be allowed by name

**What happens.** The Omarchy menu (`SUPER + Space`) launches everything through
`gtk-launch`, so every entry collapses into that one scope id. Under
`default: deny` with the teeth in, the program is closed before its window
appears, and the notification accuses the launcher rather than the program:

![The Omarchy menu opened "Regras da Casa"; two seconds later the notification says `gtk-launch is not allowed — It is not one of the programs released for Júlia.` The window never appeared.](../vm/shots/08-menu-gtk-launch-negado.png)

What is left in the day's report is a line reading
`app-Hyprland-gtk\x2dlaunch-…: not on the list, and was closed`.

**What to do in the meantime.** Build the allowlist out of Omarchy's key
bindings, which give real ids, and open what is missing with
`uwsm app -- <name>.desktop` from a terminal that is allowed.
`sudo omahouse allow kid gtk-launch` makes the menu work — and releases the
whole menu, which is an unknown and moving set of programs.

## 2. If the screen locks on idle, the last warnings go unseen

**What happens.** Omarchy's idle service locks the screen and blanks the monitor
a second or two later. The one-minute warning and the grace warning go out
behind the lock screen, so somebody who steps away comes back to a session
already ended, having seen nothing:

![The hyprlock lock screen: blurred wallpaper and the Enter Password box in the middle. This is what the session's last two warnings went out behind.](../vm/shots/15-hyprlock-ocultou-avisos.png)

**What to do in the meantime.** Treat the five-minute warning as the last
reliable one. If a profile really has to warn to the end, turn the idle lock off
for that account — knowing that is loosening something else.

## 3. The warnings on a short budget all fire at once

**What happens.** The marks are 10, 5 and 1 minute left. On a budget smaller
than the largest mark, the marks that were born already past cross at the same
instant and go off together. On a 3 minute browser budget the 10 and 5 minute
marks came out together the moment the browser opened; only the 1 minute one
landed where it meant something. A 10 minute session fires its 10 minute mark at
login.

**What to do in the meantime.** Either give budgets larger than the largest
mark, or edit `warnAt` by hand in `/etc/omahouse/profiles.json` — there is no
verb for that field.

## 4. When the session runs out, the screen goes black

**What happens.** SDDM 0.21 reads a session ended by `loginctl terminate-user`
as `Process crashed` and does nothing further: no greeter, no new display.

![An entirely black screen. This is what SDDM leaves behind after loginctl terminate-user.](../vm/shots/16-tela-preta-pos-logout.png)

The demonstration VM carries a patch service that brings SDDM back up when
`seat0` is left with no session, and it took 9 seconds. **That service is not
part of omahouse.** On a stock Omarchy there is nothing that brings the greeter
back.

**What to do in the meantime.** Install an equivalent service — one that
restarts `sddm` when the seat is left with no session — or do not use a session
budget that logs out, on a machine nobody will be able to reach the console of.

## 5. Omarchy launches its own utilities as though they were apps

**What happens.** Omarchy's `autostart.lua` brings up `udiskie` and
`omarchy-hyprland-monitor-watch` through `uwsm-app --`. They are born as app
scopes, so under `default: deny` with no rule for them omahouse closes them two
seconds after login. And `udiskie` on its own is a live scope, so the session
budget — which matches everything — **runs from login onwards**, with the
machine idle and no window open.

**What to do in the meantime.** Put both on every profile's allowlist, and count
the day knowing it starts at login and not at the first window.

## 6. Chromium turns up as two ids

**What happens.** One Chromium window on real Omarchy produces two scopes:
`chromium`, holding the child processes, and `org.chromium.Chromium`, holding
the process that owns the window. Releasing or limiting only one closes the
browser for the wrong reason, and the grace arrives as two notifications:

![Two notifications stacked: "Time is up / org.chromium.Chromium closes in 20 seconds." and "Time is up / chromium closes in 20 seconds."](../vm/shots/11-aviso-chromium-carencia.png)

**What to do in the meantime.** Write both rules and both limits, with the same
number:

```bash
sudo omahouse allow kid chromium --limit 45m
sudo omahouse allow kid org.chromium.Chromium --limit 45m
```

## 7. The filter narrows all four lists at once

**What happens.** There is one filter in the window, and `/` applies it to the
people, the programs, the day and the sites together. A needle that misses the
person on the people list empties the people list — and then the programs view
has nobody to be about and draws *nobody is under rules yet* over a household
that is right there. The key sheet says `filter this list`. This is not yet
that.

![The programs view filtered by "o": the chip bar is replaced by a field reading "/ o", and the list is Code and Firefox. The status bar reads 1/2.](img/06-operator-filter.png)

**What to do in the meantime.** Filter with something that also matches the
profile's own name, or press `Esc` and walk the list with `j` and `k`.

## 8. A refusal loses its reason on the way to the window

**What happens.** Only the last line the CLI printed reaches the status bar. The
sentence that says *what* was refused and why — `profile add: howl is in wheel,
and an administrator does not fiscalise themselves by accident.` — is the line
above it, and it is dropped. What is on screen is the advice with the verdict
missing:

![The people view with the status bar reading, in red, "Take the account out of wheel first, or write the profile for somebody else."](img/19-operator-refusal.png)

**What to do in the meantime.** Run the same verb in a terminal to read the
whole refusal.

## 9. A scope nothing can name has no screen in the window

**What happens.** `omahouse status` reports these prominently — a
`tmux-spawn-<uuid>.scope` with twenty processes in it is somebody at the
keyboard, and the design asks for what cannot be accounted for to be said out
loud. The window computes the number for every profile and no view reads it.
What the status bar does show is the other number, the processes in
`session.slice`, which is a different fact.

**What to do in the meantime.** Use `omahouse status` for that number.

## 10. The key that opens more time today lands in the field

**What happens.** Pressing `+` on a program opens the *more time today* sheet
and the same keystroke arrives in the field it opened, so the field starts with
a lone `+` in it and `ok` is born greyed out:

![The "More time today for foot" dialogue with a lone + in the field and the "Enter ok" button greyed out.](../vm/shots/39-studio-mais-tempo-hoje.png)

**What to do in the meantime.** Clear the `+` before typing the number.

## 11. In a narrow window the header overlaps the tabs

**What happens.** The header draws over the subtitle and the tabs, and the
result is unreadable.

**What to do in the meantime.** Maximise the window.

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
scopes from login onwards. See 5 above.

**A blocked site with no count of attempts and no message on it.** A managed
policy blocks inside Chromium and reports nothing out, so omahouse never learns
the attempt happened at all. There is no number to show and there cannot be one.
