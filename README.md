# omahouse

House rules for the accounts on an Omarchy machine: which programs each profile
may open, and for how long. The model is a lan house counter — an operator hands
over time, the machine counts it, warns before it runs out, and ends the session
when the credit does.

![the operator's programs view](docs/img/02-operator-programs.png)
![the day, live](docs/img/03-operator-today.png)

## Install

Not published as an Omarchy default yet. From a checkout:

```bash
mise run deps      # Qt 6 + qmake
mise run build     # build/bin/omahouse and build/bin/omahouse-studio
mise run studio    # open the window
```

`packaging/` holds the unit, the polkit policy, the desktop entry and the icon.
The Arch recipe lives in the sibling `omarchy-pkgs` repository. Released under
the MIT license.

## Start here

Every verb takes the **name of an existing account on this machine** as its
first argument. `kid` below is a placeholder: put the login name of the account
you want to put under rules, as `id` or `ls /home` would spell it.

```bash
sudo omahouse profile add kid --name "Kid"     # `kid` is the account name
omahouse status                                # the ids of what is open
sudo omahouse allow kid chromium --limit 45m
sudo omahouse limit kid --session 2h
sudo omahouse profile default kid --deny       # only what is allowed runs
sudo omahouse profile enforce kid --on         # after a day of the report

sudo omahouse web block kid youtube.com        # and which sites open
sudo omahouse web incognito kid --deny

omahouse report kid
sudo omahouse grant kid --session 10m          # with the game still open
```

A new profile is born **observing and allowing everything**: it counts and
reports and closes nothing, which buys a day of evidence before the teeth go on.
An account in `wheel` is refused a profile, because an administrator does not
fiscalise themselves by accident.

**The web rules hold for the whole machine.** Chromium has no per-account policy
on Linux, so `omahouse web` writes one managed policy composed from every
profile at once — a site blocked for the kid is blocked for you too, and
profiles that disagree compose to the most restrictive with no precedence
between them. That cost was weighed and taken;
[`docs/design.md` §11](docs/design.md) says why, and the CLI says it once when
you write the rule. Taking the last rule back, or removing the package, takes
the file off the machine.

Reading needs no privilege — the fiscalised person runs `omahouse status` and
sees what is left of their own day. Writing needs root, and the window gets there
through `pkexec`. `omahouse watch` is the loop that counts, warns and acts;
`packaging/omahouse.service` runs it.

[The guide](docs/guide.md) walks from an installed omahouse to a profile that
stands up. Every verb is in [the command line](docs/cli.md).

## The window

`omahouse-studio` is the same model with a Qt Quick front on it. It reads by
linking the libraries directly and writes by running `pkexec omahouse <verb>`, so
the privileged half is the CLI, with the same refusals. **It is never root.**

Two faces, and nobody picks one on screen. Whoever is in `wheel` gets the
operator's: the people under rules, the programs released to each of them, the
day's balance live, and the chips to change all three. Everybody else gets the
subject's, which is the same window with nothing to press.

- `j` / `k`, `↓` / `↑` — move the cursor
- `g` / `G`, `Home` / `End` — first, last row
- `l` / `Enter` — open the profile under the cursor
- `h` / `Esc` — back, or clear the filter, or leave a control
- `1` / `2` / `3` — the people, their programs, their day
- `/` — filter · `:` — commands · `?` — the key map
- `Tab` — next control · `Space` — press the one the keyboard is on
- `n` — put an account under rules · `e` — close programs, or only watch
- `d` — only the listed run, or everything but the listed
- `a` — release a program · `m` — minutes a day · `+` — more time today
- `x` — take a program off the list, or an account off the books

![the key sheet](docs/img/04-operator-keys.png)

Every screen the window draws is in [the inventory](docs/screens.md).

## What it does not do

**It is not a security boundary.** A program started from inside a terminal
inherits the terminal's scope, so a profile with a terminal on its allowlist is a
profile that allows everything — and all of it counts as terminal time. Do not
release a terminal in a profile that is meant to hold.

The force of a rule is a property of who the operator is, not of the engine. A
profile administered by somebody else holds for real; one somebody imposes on
themselves they undo whenever they like. What the model does hold, because none
of it depends on the goodwill of the session, is the clock, the `loginctl`
logout, the counting and the daemon.

## What is broken

The first four were measured on a real Omarchy; the last three are in the code
as written. None of them has a fix here yet. [The guide
§6](docs/guide.md#6-what-does-not-work-yet) has all of them, each with what to
do in the meantime.

- Programs opened from the Omarchy menu all arrive as one id, `gtk-launch`, so
  they cannot be released by name.
- `hypridle` locks the screen, and the last warnings go out behind the lock.
- When a session runs out, SDDM stops and the screen stays black until somebody
  brings it back.
- Chromium turns up as two ids, and both need the rule.
- `/` in the studio filters all three lists at once, against what its own key
  sheet says.
- A CLI refusal loses its reason on the way to the studio: only the last line
  printed reaches the status bar.
- A scope nothing can name has no screen in the studio, though the CLI reports
  it.

## Build

```bash
mise run test        # unit tests and the CLI end to end
mise run lint        # the QML, with every warning fatal
mise run studio:test # the window, by keyboard and by mouse, with no screen
mise run usage:gen   # docs/cli.md, from omahouse.usage.kdl
mise run shots       # docs/img, from the studio itself, offscreen
mise run verify      # the local gate: all of the above
mise run hooks:install
mise run test:vm     # the teeth, in the VM — never on this machine
```

Shadow build only — qmake refuses to configure inside the source tree.
[How it is built](docs/design.md) is the model, the measurements that shaped it,
and what the gate refuses.

## Requirements

- Qt 6: `qt6-base`, and `qt6-declarative` for the window and its linter
- `polkit` for `pkexec`, which is how the window writes
- [mise](https://mise.jdx.dev/) for the tasks above, and for the pinned `usage`
  that generates `docs/cli.md`

`docs/img` is regenerated from the studio itself with `mise run shots`.
