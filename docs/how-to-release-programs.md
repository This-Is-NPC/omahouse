# How to say which programs may run

**The question:** this account should be able to open the browser and the
terminal and nothing else. How do I write that list, and how do I find out what
the programs are called?

This page is enough on its own. It ends with a profile whose rules are a list of
what may open. Putting a clock on any of it is
[the next page](how-to-limit-the-time.md).

---

## Before you start

- **A profile exists for the account.**
  [How to put an account under rules](how-to-put-an-account-under-rules.md).
- `kid` in every command below is a placeholder for the account's login name, as
  `id` or `ls /home` spells it.
- **You can become root.** Reading needs no privilege; writing does.

---

## 1. Find out what the programs are called

**An app's identity, as far as omahouse is concerned, is its systemd scope id**
— not the path of its executable. That is what groups a browser's twenty-one
processes into one app, and it is what goes into a rule. The executable is a
second opinion and never a replacement: it is consulted only for a scope whose
id is not the name of what is running, which is [the launcher below](#2-the-menu-names-every-program-after-the-launcher).
`omahouse status` is what lists the ids that are open right now:

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

The `APP` column is what goes into a rule. It needs no privilege, and it works
on an account with no profile at all — that is the mode that answers *what would
omahouse see here*.

**Read the second block before you write the list.** `status` also says, under a
heading of its own, when the name lies:

```
Not what the name says
APP                             IS RUNNING                               PROCESSES  SCOPES
gtk-launch                      /usr/lib/chromium/chromium                19 of 21       1
code                            /usr/bin/dconf                            10 of 10      10
gtk-launch                      /usr/share/code/chrome_crashpad_handler     8 of 8       8
xdg-terminal-exec               /usr/bin/tmux                               2 of 4       1
udiskie                         /usr/bin/python3.14                         1 of 1       1
```

That is the difference between releasing a program and releasing a launcher. It
is a sentence and never a verdict: the rule goes on matching the id, and the
executable is there so that nobody releases blind. A flatpak reads the same way
for the opposite reason, since every flatpak on a machine runs `/usr/bin/bwrap`.

## 2. The menu names every program after the launcher

The Omarchy menu (`SUPER + Space` → *Apps*) launches **everything** through
`gtk-launch`. The scope that is born is called `gtk-launch`, not the name of the
program, and every entry in that menu collapses into that one id.

**You still write the program's own name.** Where a scope's id is not the name
of what is running, omahouse asks what is: a rule about `code` reaches the
program the menu opened, and so does its limit. The id is never overruled by
this — it only gains a second way to be named — so the rule you wrote is the
rule you can read back.

```bash
sudo omahouse allow kid code --limit 45m
```

That covers the menu, `uwsm app --`, and a terminal, because all three end up
running the same executable inside whatever scope launched them.

*(The pictures from the live machine were taken with the account under rules
called `julia`; the commands here say `kid`. They are the same placeholder.)*

**`gtk-launch` is still not a name to write.** Allowing it allows the whole
menu — an unknown set of programs that changes with every `.desktop` file
installed. It is a launcher, and what you want released is what it launches.

**And `bwrap` is not one either, for the opposite reason.** Every flatpak on the
machine runs `/usr/bin/bwrap`, so releasing the runner releases all of them. A
flatpak's id is already correct — `org.freedesktop.Platform` — and that is the
name to write.

## 3. Write the rules

```bash
sudo omahouse allow kid xdg-terminal-exec
```

```
kid: xdg-terminal-exec allowed. No limit of its own, so it spends the session's.
```

```bash
sudo omahouse deny kid steam
```

```
kid: steam is not allowed to run.
```

A rule that is already there is changed in place rather than appended to: the
first rule that names an app is the one that wins, so a second line about it
would be a line that never fires. `deny` is also how a rule written by mistake
is taken back.

`--limit 45m` on an `allow` writes the rule and the budget in one go —
[how long a day](how-to-limit-the-time.md) is that page.

**Three things that have to be on the list and are not obvious:**

- **Chromium turns up as two ids.** One window produces `chromium` (the child
  processes) and `org.chromium.Chromium` (the process that owns the window).
  Releasing only one closes the browser for the wrong reason. Write both.
- **Omarchy launches its own utilities as though they were apps.** Its
  `autostart.lua` brings up `udiskie` and `omarchy-hyprland-monitor-watch`
  through `uwsm-app --`; with no rule for them, and the teeth in, omahouse
  closes them two seconds after login.
- **Do not release a terminal in a profile that is meant to hold.** A program
  started from inside a terminal inherits the terminal's scope, so whoever has a
  terminal released runs whatever they like — and all of it counts as terminal
  time. There is no fixing this in the engine;
  [`design.md` §10](design.md) is the standing position.

A list built the way the pictures on this page were:

```bash
sudo omahouse allow kid xdg-terminal-exec
sudo omahouse allow kid omahouse
sudo omahouse allow kid chromium --limit 3m
sudo omahouse allow kid org.chromium.Chromium --limit 3m
sudo omahouse allow kid omarchy-hyprland-monitor-watch
sudo omahouse allow kid udiskie
```

## 4. Turn the list into an allowlist

Until you say otherwise, a profile's rules are a list of what may **not** run.
This is the switch:

```bash
sudo omahouse profile default kid --deny
```

```
kid: only what is on the list runs. 6 rules on it.
```

`--allow` puts it back to *everything runs except what is denied*. The engine is
the same either way; what changes is what happens to an app no rule names.

Nothing bites yet. Rules are enforced only once the profile has its teeth in,
which is [the next page](how-to-limit-the-time.md).

## 5. Read it back

```bash
omahouse profile show kid
```

```
RULES
VERDICT  APP
allow    xdg-terminal-exec
allow    omahouse
allow    omarchy-hyprland-monitor-watch
allow    udiskie
allow    chromium
allow    org.chromium.Chromium
```

The rules are printed in the order they are read, and the first that names an
app wins.

---

## In the window

On the **programs** view (`2`), `a` opens a picker fed from what is open right
now and from the installed `.desktop` entries. **It marks the shim**, which is
what stops anybody releasing blind:

![The program picker: a query field reading "which program", then Code · code (already listed, open now) with /usr/share/code/code under it; gtk-launch, in red, "that name is the launcher — inside it: /usr/share/code/code"; and Firefox · firefox with /usr/lib/firefox/firefox.](img/08-operator-choose-program.png)

Against a real catalogue, on the live machine, most of it is shims:

![The studio's "which program" picker: "Basecamp" and "Discord" say in red `that name is the launcher — inside it: omarchy-launch-webapp`, "Disk Usage" and "Docker" say `xdg-terminal-exec`, and "Chromium · chromium" comes up `already listed`.](../vm/shots/34-studio-escolher-programa.png)

Choosing a row asks how long a day for it, and then polkit. The list that comes
out reads like this:

![The programs view: Code with "open now · 5 processes" and "20m left of 45m"; Firefox with "0m left of 1h" and a full red bar; gtk-launch with the red line "that name is not what is running: /usr/share/code/code — 3 of 3"; steam marked "not released".](img/02-operator-programs.png)

Four rows and four different things: a program running under its limit, one that
has run out, one whose scope name is a launcher shim with something else inside
it — said in red — and one that is named and refused.

The other keys on that view: `x` takes a program off the list, `d` on the people
view swaps between *only the listed run* and *everything runs but the listed*.
`x` is one of the two things that ask first:

![A confirmation bordered in red: "Take code off the list?" — any limit written for it stays where it is.](img/14-operator-drop-program.png)

On a profile that has just been created:

![The programs view empty: "no program has been named yet — press a, or click the chip", and the status bar says "everything runs but the listed".](img/21-operator-programs-empty.png)

---

## What can go wrong

**The id names a launcher and not a program.** `allow` warns and writes the rule
anyway, printing what is running inside the scopes that are open under that id.
It may be exactly what was meant — a flatpak reads the same way — so this is a
warning and not a refusal. With nothing open under that id there is no evidence,
and nothing is said.

**Some scopes have no id at all.** A `tmux-spawn-<uuid>.scope` with twenty
processes in it is somebody at the keyboard; omahouse counts it and cannot name
it. No rule can reach it, so its verdict is the profile's default — which under
`default: deny` with the teeth in means it is closed. `omahouse status` reports
these under `Counted, not named`, and
[the window has no screen for them](what-does-not-work.md#6-a-scope-nothing-can-name-has-no-screen-in-the-window).

**Some programs are out of reach entirely.** Anything started by a raw `exec` in
a keybinding, or from inside a terminal, lands in the compositor's own cgroup,
where it cannot be told apart from Hyprland itself. It is not counted and it
cannot be closed. `omahouse status` prints that number under `Out of reach`, and
it is never zero on a live session. **This is why omahouse is not a security
boundary** — see [what it is not](design.md) §10.

---

## Next

- [How long a day](how-to-limit-the-time.md) — the clock on a program, the clock
  on the session, and switching the teeth on.
- [Which sites open](how-to-block-sites.md) — the same idea, one layer up, in
  the browser.
- [Reading the day](how-to-read-the-day.md) — what the list turned out to cost.
