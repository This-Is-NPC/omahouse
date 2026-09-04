# Using omahouse

This guide takes somebody who has installed omahouse as far as a profile that
stands up: an account with its programs released, a limit on each of them, a
limit on the day, and the rules in force.

All of it was watched working in a real Omarchy 4.0.2 session. The images are
from that run, and what does **not** work is in §6, under the name it has. A
guide that teaches a path with a hole in it lies the first time somebody walks
it.

Two things about the names below. Every verb takes the **login name of an
account that already exists on this machine** as its first argument; `kid` in
the commands is a placeholder for it, spelled the way `id` or `ls /home` would
spell it. And the account under rules on the machine the pictures came from was
called `julia`, which is drawn into them and cannot be regenerated — so the
pictures say `julia` where the commands beside them say `kid`.

---

## 1. What it is

omahouse keeps a **profile** for every account on the machine: which programs
that account may open and for how long, per program and over the whole day.
What counts and what acts is `omahouse watch`, a root daemon that wakes every
two seconds, reads the session's app scopes, debits the budgets, warns before
the time is out, closes the program whose time has gone and ends the session
whose day has. The fiscalised person's session decides nothing — it receives
the notifications and can read its own balance.

Reading needs no privilege: `omahouse status`, `omahouse report` and
`omahouse profile list|show` run for anybody. Writing needs root. The studio
(`omahouse-studio`) is never root: everything it writes goes through
`pkexec omahouse …`, and polkit asks for an administrator's password.

---

## 2. Building a profile

### 2.1 Create the profile

`kid` is the account name, not a keyword: put the login name of the account you
are putting under rules. `--name` is only the label the window shows.

```bash
sudo omahouse profile add kid --name "Kid"      # `kid` is the account name
```

```
kid: profile written to /etc/omahouse/profiles.json
  observing — it counts and reports and closes nothing. Watch a day of
  `omahouse report kid`, then `omahouse profile enforce kid --on`.
  every app is allowed: `omahouse profile default kid --deny` turns the
  rules into a list of what is allowed instead.
```

The profile is born **with no teeth** (`enforce: false`) and **allowing
everything** (`default: allow`). That is on purpose: a profile that counts
without biting buys you a day of the report before the rules go on, and a
half-written profile that denied everything would lock somebody out of their
own machine.

An account in `wheel` is refused, and it is the program's only hard refusal:

```
profile add: howl is in wheel, and an administrator does not fiscalise themselves by accident.
             Take the account out of wheel first, or write the profile for somebody else.
```

In the studio: `n` on the **people** tab, the field asks `Which account?`,
polkit asks for the password, and the `wheel` refusal turns up in the footer in
those same words.

### 2.2 Finding out what the programs are called

An app's identity, as far as omahouse is concerned, is its **systemd scope id**
— not the path of its executable. `omahouse status` is what lists those ids:

```bash
omahouse status
```

```
APP                             PIDS  SCOPE
gtk-launch                        21  app-Hyprland-gtk\x2dlaunch-fa8e3f64.scope
xdg-terminal-exec                  4  app-Hyprland-xdg\x2dterminal\x2dexec-151e8e07.scope
org.chromium.Chromium              3  app-org.chromium.Chromium-2587456.scope
omarchy-hyprland-monitor-watch     2  app-Hyprland-omarchy\x2dhyprland\x2dmonitor\x2dwatch-f5a7f3ab.scope
udiskie                            1  app-Hyprland-udiskie-983fb161.scope
code                               1  app-code-3579042.scope
```

The `APP` column is what goes into an `allow`. `status` also says, in a block of
its own, when the name lies:

```
Not what the name says
APP                             IS RUNNING                               PROCESSES  SCOPES
gtk-launch                      /usr/lib/chromium/chromium                19 of 21       1
code                            /usr/bin/dconf                            10 of 10      10
gtk-launch                      /usr/share/code/chrome_crashpad_handler     8 of 8       8
xdg-terminal-exec               /usr/bin/tmux                               2 of 4       1
udiskie                         /usr/bin/python3.14                         1 of 1       1
```

Read that block before you write the list. It is the difference between
releasing a program and releasing a launcher.

### 2.3 The Omarchy menu is no way to build an allowlist

**This is the largest gotcha, and it decides the shape of your list.**

The Omarchy menu (`SUPER + Space` → *Apps*) launches **everything** through
`gtk-launch`. The scope that is born is called `gtk-launch`, not the name of the
program. Under `default: deny`, omahouse closes it on the spot — and the
notification names the launcher rather than the program the person was trying to
open:

![The Omarchy menu opened "Regras da Casa"; two seconds later the notification says `gtk-launch is not allowed — It is not one of the programs released for Júlia.` The window never appeared.](../vm/shots/08-menu-gtk-launch-negado.png)

Every entry in that menu collapses into the one id. Allowing `gtk-launch`
**allows the whole menu**, and the set of things it launches changes with every
`.desktop` installed.

While it is like this, build the list out of programs opened by Omarchy's **key
bindings**, which give real ids:

| key | id it is born with |
|---|---|
| `SUPER + Enter` | `xdg-terminal-exec` |
| `SUPER + Shift + B` | `chromium` **and** `org.chromium.Chromium` |

And open the studio itself from the command line, from a terminal that is
allowed:

```bash
uwsm app -- omahouse.desktop
```

### 2.4 Release a program

```bash
sudo omahouse allow kid chromium --limit 3m
```

```
kid: chromium allowed, 3m a day, and it closes when the time is out.
```

`--limit` is sugar: it writes the rule and the budget in one go. A program
released **without** a limit of its own runs and spends only the day's total.

Three things that have to be on the list and are not obvious:

- **Chromium has two ids.** One window produces `chromium` (the child
  processes) and `org.chromium.Chromium` (the one that owns the window).
  Releasing only one closes the browser for the wrong reason. Write both, and
  give both a limit.
- **Omarchy launches its own utilities as though they were apps.** Its
  `autostart.lua` brings up `udiskie` and `omarchy-hyprland-monitor-watch`
  through `uwsm-app --`; with no rule for them, omahouse closes them two seconds
  after login.
- **Do not release a terminal in a profile that is meant to hold.** A program
  started from inside a terminal inherits the terminal's scope: whoever has a
  terminal released runs whatever they like, and all of it counts as terminal
  time. This is [`design.md`](design.md) §10, and there is no fixing it in the engine.

The list on the run these images came from was built like this:

```bash
sudo omahouse allow kid xdg-terminal-exec
sudo omahouse allow kid omahouse
sudo omahouse allow kid chromium --limit 3m
sudo omahouse allow kid org.chromium.Chromium --limit 3m
sudo omahouse allow kid omarchy-hyprland-monitor-watch
sudo omahouse allow kid udiskie
```

In the studio, `a` opens a picker fed from the installed `.desktop` entries. It
**marks the shim**, which is what stops anybody releasing blind:

![The studio's "which program" picker: "Basecamp" and "Discord" say in red `that name is the launcher — inside it: omarchy-launch-webapp`, "Disk Usage" and "Docker" say `xdg-terminal-exec`, and "Chromium · chromium" comes up `already listed`. The rest — "Foot · foot", "btop++ · btop", the three Avahi browsers — carry an id that is the program itself, with the executable under it.](../vm/shots/34-studio-escolher-programa.png)

After the picker comes `How long a day for foot?`, and then polkit. The footer
confirms in the same sentence the CLI prints:
`kid: foot allowed, 5m a day, and it closes when the time is out.`

### 2.5 Set the session limit, and close the list

```bash
sudo omahouse limit kid --session 2h
sudo omahouse profile default kid --deny
```

```
kid: session 2h a day, and it logs out when the time is out.
kid: only what is on the list runs. 6 rules on it.
```

A length of time is `45m`, `2h`, `1h30m`, or a bare number of minutes. Anything
else is refused rather than turned into minutes.

In the studio: `s` on the **today** tab for the whole day, `m` on the
**programs** tab for one program's day, `d` to swap between "only what is on the
list runs" and "everything runs but what is on the list".

### 2.6 Look at it, and only then put the teeth in

```bash
omahouse profile show kid
```

```
omahouse profile — kid (Kid)

SETTING  VALUE
enabled  yes
enforce  yes
default  deny
warn at  10m, 5m, 1m left
grace    20s

RULES
VERDICT  APP
allow    xdg-terminal-exec
allow    omahouse
allow    omarchy-hyprland-monitor-watch
allow    udiskie
allow    chromium
allow    org.chromium.Chromium

BUDGETS
BUDGET                 APP                    A DAY  WHEN OUT
chromium               chromium                  3m  closes
org.chromium.Chromium  org.chromium.Chromium     3m  closes
session                *                      2h00m  logs out
```

Before switching it on, watch a cycle with no consequences at all:

```bash
omahouse watch --once --dry-run
```

It reads the tree, debits the tick, prints what it would have closed and who it
would have refused at the next login, and writes nothing, notifies nobody and
ends nobody's session. It needs no privilege.

On:

```bash
sudo omahouse profile enforce kid --on
```

```
kid: enforcing — budgets now close and log out.
```

In the studio, `e`.

---

## 3. What the fiscalised person sees

**There is no indicator on the bar.** Omarchy's `quickshell` stays as it came;
omahouse puts nothing in it. What the person sees is the notifications, and what
they can look up is `omahouse status` or the studio window.

### The warning

The default marks are 10, 5 and 1 minute left. The notification names the budget
and the clock time the cut happens at:

![A terminal on the left, Chromium on the right, and in the top right corner the notification `1 minute left / chromium closes at 08:34.` The clock on the bar reads 08:33.](../vm/shots/10-aviso-chromium-1min.png)

### The grace window

Once the budget is spent, twenty seconds open up between "time is up" and the
closing. Since Chromium has two ids, **two** notifications arrive, one per id:

![Two notifications stacked: `Time is up / org.chromium.Chromium closes in 20 seconds.` and `Time is up / chromium closes in 20 seconds.`](../vm/shots/11-aviso-chromium-carencia.png)

### The program closes, and the session stays

Twenty seconds later Chromium is gone. The terminal is still open, the bar is
still there, the session is still standing:

![The same screen without Chromium: only `julia`'s terminal maximised, the bar intact, the clock at 08:34.](../vm/shots/12-chromium-fechado.png)

This is the whole point of the design: `cgroup.kill` takes the app's scope and
`session.slice` is never touched.

### The session clock keeps running

The session budget matches everything, so it runs even with nothing happening on
screen — the `udiskie` Omarchy itself brings up is a live app scope from login
onwards:

![The notification `5 minutes left / Your session ends at 08:40.` over the terminal, at 08:35.](../vm/shots/13-aviso-sessao-5min.png)

### The end of the session, and the refused login

When the day is out, the name goes into `/etc/omahouse/blocked`, the
`pam_listfile` in `/etc/pam.d/system-login` starts refusing, and only then is the
session ended. The right password no longer gets in: the greeter's box turns
**red** and nothing happens.

![The Omarchy greeter with the password box red and the padlock red. No message explains why.](../vm/shots/18-login-recusado.png)

The journal on the other side says `pam_listfile(sddm:account): Refused user
kid`. The greeter says nothing — whoever is in front of the machine does not
find out from it that the time ran out.

### Reading your own balance

The fiscalised person runs `omahouse status`, or opens the studio, which to them
is read only:

![`julia`'s studio, **programs** tab: `xdg-terminal-exec` (open now, 2 processes, with the red warning `that name is not what is running: /usr/bin/bash — 1 of 2`), `House Rules · omahouse`, `Chromium · chromium` with `0m left of 3m` and the red bar full, `org.chromium.Chromium` the same, `omarchy-hyprland-monitor-watch`, and `udiskie` with the same red warning pointing at `/usr/bin/python3.14`.](../vm/shots/23-studio-julia-programas.png)

The header says what she is: `julia · subject · under rules · writes through
pkexec`. The footer counts what omahouse cannot see: `8 it cannot see`.

---

## 4. Day to day

### More time now, with the program open

```bash
sudo omahouse grant kid --session 10m
sudo omahouse grant kid --budget chromium=15m
```

```
kid: +10m of session, from howl. 2h10m left today.
```

The time goes into the day's ledger and expires with it. Two `grant`s add up. If
the name was in `/etc/omahouse/blocked`, it comes out on its own on the next
cycle — within two seconds the password gets into the greeter again, without
anybody having to know the file exists.

In the studio, `+`:

![The studio on `julia`'s **programs** tab once the grant is through: the footer says `julia: +10m of foot, from howl. 15m left today.`, and the Foot row reads `15m left of 15m` with `+10m handed over today` under it.](../vm/shots/40-studio-tempo-extra-concedido.png)

> **The `+` gotcha.** The `+` key itself leaks into the field and `ok` is born
> greyed out. Clear the `+` before typing the number.
>
> ![The "More time today for foot" dialogue with a lone `+` in the field and the `Enter ok` button greyed out.](../vm/shots/39-studio-mais-tempo-hoje.png)

> **Whose name is on it.** A `grant` is signed with the name of whoever asked
> for it. Through the studio that is the person polkit authenticated —
> `howl handed over 10m of foot`. Through `sudo omahouse grant` it is `root`,
> and the report a month later will say `root handed over 45m of session`. If
> the name matters in your records, grant from the studio.

### Loosening the limits for good

```bash
sudo omahouse limit kid --session 4h            # a longer day
sudo omahouse profile enforce kid --off         # counts and reports, closes nothing
sudo omahouse profile default kid --allow       # everything runs but what is denied
```

`enforce --off` is the observing mode, and it is where to go back to when
something is biting too hard and you do not yet know what.

### Taking a program off the list, or somebody off the books

```bash
sudo omahouse deny kid chromium
sudo omahouse profile remove kid
```

`profile remove` takes the profile away and touches neither the account nor the
history: the days already counted stay in `/var/lib/omahouse/kid/`, because a
report is evidence and outlives the rule that collected it. In the studio, `x`
does either one depending on the tab you are on.

### Reading the day

```bash
omahouse report kid
omahouse report kid --since 2026-09-01
```

```
omahouse report — kid

2026-09-04
  nothing counted

GRANTS
AT        BY    BUDGET   ADDED
09:01:50  howl  session   +10m
```

The same day in the studio, **today** tab:

![`julia`'s studio on the **today** tab: at the top `the whole day · session · running now`, `12m spent`, `52m left of 1h5m` and `+55m handed over today`; below it `Chromium` and `org.chromium.Chromium`, both `3m spent · 0m left of 3m`; and the day's list newest first, from the grants (`root handed over 45m of session`) down to the 08:30 warnings, with one red line at 08:32: `app-Hyprland-gtk\x2dlaunch-c5075ae6.scope: not on the list, and was closed`.](../vm/shots/24-studio-julia-hoje.png)

The events are not decoration. They are where a decision that can only happen
once remembers that it already has — reading the day is reading why a warning
went out, or why it did not.

---

## 5. The studio from the keyboard

```bash
uwsm app -- omahouse.desktop
```

The studio has **two faces**, and nobody picks which. Whoever is in `wheel` gets
the operator's; everybody else gets the subject's. The header says which:
`howl · operator · is in wheel · writes through pkexec` against
`kid · subject · under rules · writes through pkexec`.

Every action exists as a **key and as a chip**, because there is one table of
commands in the window: the chips are drawn from it, the keys are looked up in
it, and `:` and `?` list it. An action cannot exist on only one of the two.

`?` opens the map, and it changes with the context — the **here, right now**
section carries only what the row under the cursor will take at this moment:

![The operator's key map: the columns *move* (`j`/`k`, arrows, `g`/`G`, `Home`/`End`, `PgDn`/`PgUp`), *go* (`l`/`Enter` opens the profile, `h` goes back, `Esc` goes back or clears the filter, `1`/`2`/`3` for people, programs and today) and *window* (`/` filters, `:` commands, `?` this list, `Tab` next control, `Space` presses the control the keyboard is on); and under them **here, right now**: `n` puts an account under rules, `e` watch only, `d` lets everything run but the listed, `x` takes the account off the books.](../vm/shots/29-studio-howl-teclas.png)

The same `?` in the fiscalised person's session has **no** writing key at all —
no `n`, no `a`, no `+`, no **here, right now** section:

![`julia`'s key map: *move*, *go* and *window*, and nothing else.](../vm/shots/21-studio-teclas-julia.png)

The palette (`:`) obeys the same table: in the fiscalised person's session it
holds one command, `back to the people`.

The writing keys, all of them going through `pkexec`:

| key | what it does |
|---|---|
| `n` | put an account under rules |
| `e` | teeth in, teeth out (`enforce`) |
| `d` | allowlist or denylist |
| `a` | release a program |
| `m` | minutes a day for that program |
| `s` | how long the whole day is |
| `+` | more time **today** |
| `x` | take the program off the list, or the account off the books |

> **Layout gotcha.** In a narrow window the header overlaps the subtitle and the
> tabs, and the result is unreadable. Maximise the window.

---

## 6. What does not work yet

Everything below was measured broken. None of it has a fix in omahouse today.
This is the whole list: nothing else in the documentation names a defect that is
not here.

### 6.1 Programs opened from the menu cannot be allowed by name

**What happens.** The Omarchy menu (`SUPER + Space`) launches everything through
`gtk-launch`. Every entry collapses into the one id. Under `default: deny` the
program is closed before its window appears, and the notification accuses
`gtk-launch` (the image in §2.3). What is left in the day's report is the line
`app-Hyprland-gtk\x2dlaunch-…: not on the list, and was closed`.

**What to do in the meantime.** Build the allowlist out of Omarchy's key
bindings, which give real ids, and open what is missing with
`uwsm app -- <name>.desktop` from a terminal that is allowed.
`sudo omahouse allow kid gtk-launch` makes the menu work — and releases the
whole menu, which is an unknown and moving set of programs.

### 6.2 If the screen locks on idle, the last warnings go unseen

**What happens.** Omarchy's `hypridle` locks the screen with `hyprlock` and
blanks the monitor two seconds later. The one-minute warning and the grace
warning go out behind the lock screen. Somebody who steps away from the machine
comes back to a session already ended, having seen nothing.

![The `hyprlock` lock screen: blurred wallpaper and the `Enter Password` box in the middle. This is what the session's last two warnings went out behind.](../vm/shots/15-hyprlock-ocultou-avisos.png)

**What to do in the meantime.** Treat the five-minute warning as the last
reliable one. If the profile really does have to warn to the end, turn Omarchy's
idle lock off for that account — knowing that is loosening something else.

### 6.3 The warnings on a short budget all fire at once

**What happens.** The marks are 10, 5 and 1 minute left. On a budget smaller than
the largest mark, the marks that were born already past cross at the same instant
and go off together. On the 3 minute Chromium, the 10 and 5 minute marks came out
together at 08:30, the moment the browser opened; only the 1 minute one landed
where it meant something, at 08:33. The 10 minute session fired its 10 minute
mark at login. It is in the day's report, in the image in §4.

**What to do in the meantime.** Either give budgets larger than the largest
mark, or edit `warnAt` by hand in `/etc/omahouse/profiles.json` — there is no CLI
verb for that field.

### 6.4 When the session runs out, the screen goes black

**What happens.** SDDM 0.21 reads a session ended by `loginctl terminate-user` as
`Process crashed` and does nothing further: no greeter, no new display. The
screen looks like this:

![An entirely black screen. This is what SDDM leaves behind after `loginctl terminate-user`.](../vm/shots/16-tela-preta-pos-logout.png)

The demonstration VM carries a patch service (`omahouse-vm-greeter-guard`) that
brings SDDM back up when `seat0` is left with no session; it took 9 seconds. That
service is **not** part of omahouse. On a stock Omarchy there is nothing that
brings the greeter back.

**What to do in the meantime.** Install an equivalent service, one that restarts
`sddm` when the seat is left with no session, or do not use
`onExhausted: "logout"` on a machine nobody will be able to reach the console of.

### 6.5 Omarchy launches its own utilities as though they were apps

**What happens.** Omarchy's `autostart.lua` brings up `udiskie` and
`omarchy-hyprland-monitor-watch` through `uwsm-app --`. They are born as app
scopes, and under `default: deny` with no rule for them omahouse closes them two
seconds after login. On top of that, `udiskie` on its own is a live scope: the
session budget, which matches everything, **runs from login onwards**, with the
machine idle and no window open.

**What to do in the meantime.** Put both on every profile's allowlist, and count
the day knowing it starts at login and not at the first window.

### 6.6 Chromium turns up as two ids

**What happens.** One Chromium window on real Omarchy produces two scopes with
two ids: `chromium`, holding the child processes, and `org.chromium.Chromium`,
holding the process that owns the window. Releasing or limiting only one closes
the browser for the wrong reason, and the grace arrives as two notifications (the
image in §3).

**What to do in the meantime.** Write both `allow`s and both `--limit`s, with the
same number:

```bash
sudo omahouse allow kid chromium --limit 3m
sudo omahouse allow kid org.chromium.Chromium --limit 3m
```

### 6.7 `/` in the studio filters all three lists at once

**What happens.** There is one filter, and it is applied to the people, the
programs and the day together. A needle that misses the person on the people
list empties the people list — and then the programs view has nobody to be about
and draws *nobody is under rules yet* over a household that is right there. The
key sheet says `/ filter this list`. This is not yet that.

**What to do in the meantime.** Filter with something that also matches the
profile's own name, or press `Esc` and walk the list with `j` / `k`.

### 6.8 A refusal loses its reason on the way to the studio

**What happens.** Only the last line the CLI printed reaches the status bar
(`Admin::lastLine`). The sentence that says *what* was refused and why —
`profile add: howl is in wheel, and an administrator does not fiscalise
themselves by accident.` — is the line above it, and it is dropped. What is on
screen is the advice with the verdict missing.

**What to do in the meantime.** Run the same verb from a terminal to read the
whole refusal.

### 6.9 A scope nothing can name has no screen in the studio

**What happens.** `omahouse status` reports these prominently — a
`tmux-spawn-<uuid>.scope` with twenty processes in it is somebody at the
keyboard, and [`design.md`](design.md) §5 asks for what cannot be accounted for
to be said out loud. The window computes the number and no view reads it. What
the status bar does show is the other number, `n it cannot see`, which is the
processes in `session.slice` — a different fact.

**What to do in the meantime.** Use `omahouse status` for that number.

### 6.10 Studio defects, smaller

- The `+` key leaks into the "more time today" field and leaves `ok` greyed out.
  Clear the `+` before typing.
- In a narrow window the header overlaps the subtitle and the tabs. Maximise.

---

## What omahouse is not

**It is not a security boundary.** The allowlist judges app scopes. A program
started from inside a terminal inherits the terminal's scope, and a program
started by a raw `exec` in a keybinding lands inside the compositor's own unit,
where omahouse can neither count it nor close it — `omahouse status` reports that
number under "Out of reach", and it is never zero on a live session.

The force of a rule is a property of **who the operator is**, not of the engine.
A profile administered by somebody else holds for real; a profile somebody
imposes on themselves they undo whenever they like, and that is fine — it is
discipline, not a prison.

What the model does hold, because none of it depends on the goodwill of the
session: the clock, the `loginctl` logout, the counting (the ledger is written by
root) and the daemon itself (`Restart=always`).

[How it is built](design.md) says why each of those choices was made, and
[the command line](cli.md) has the verbs in full. The images in this guide come
from one run through a VM with Omarchy installed on it, driven by keyboard and
captured frame by frame; they are not regenerable, which is why they are kept.
