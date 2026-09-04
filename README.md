# omahouse

House rules for the accounts on an Omarchy machine: which programs each profile
may open, and for how long. The model is a lan house counter — an operator adds
time, the machine counts it, warns before it runs out and ends the session when
the credit does.

The model lives in `src/core` and it is pure. `evaluate` is handed the app
scopes that are running, the profile, the day's ledger and `now`, and gives back
the new ledger and what to do about it; nothing in it reads the machine or the
clock, which is how a two hour budget is proved in microseconds. Everything that
does read the machine is in `src/sys`, on the other side of a line the gate can
check.

## Using it

The CLI reads, configures, counts and acts. `watch` is the loop: it warns
before the time is out, closes an app when its budget does, and ends a session
when the day's is gone.

```bash
omahouse status [user]     # live app scopes, what they are, and what is left
omahouse report <user>     # the day's ledger, or --since a day
omahouse profile list
omahouse profile show <user>

sudo omahouse profile add julia --name "Júlia"
sudo omahouse profile default julia --deny     # only what is allowed runs
sudo omahouse allow julia minecraft-launcher --limit 45m
sudo omahouse limit julia --session 2h
sudo omahouse grant julia --session 10m        # with the game still open
sudo omahouse profile enforce julia --on       # after a day of the report

omahouse watch --once --dry-run   # one cycle: what is open, and what is left
sudo omahouse watch               # the loop, every two seconds
```

Reading needs no privilege — the fiscalised user runs `omahouse status` and sees
what is left of their own day. Writing needs root, and the window gets there
through `pkexec`. A new profile is born observing and allowing
everything: it counts and reports and closes nothing, which is a day of evidence
before the teeth go on. An account in `wheel` is refused a profile, because an
administrator does not fiscalise themselves by accident.

`docs/cli.md` is the whole of it, generated from `omahouse.usage.kdl`. Every
root moves by variable, which is how the end to end suite runs as an ordinary
user with nothing installed:

| variable | default | what |
|---|---|---|
| `OMAHOUSE_CONFIG_DIR` | `/etc/omahouse` | `profiles.json`, who is under rules |
| `OMAHOUSE_STATE_DIR` | `/var/lib/omahouse` | `<user>/<AAAA-MM-DD>.json`, the day |
| `OMAHOUSE_CGROUP_ROOT` | `/sys/fs/cgroup` | where the app scopes are read from |
| `OMAHOUSE_PROC_ROOT` | `/proc` | what the processes in a scope are running |
| `OMAHOUSE_USERADD` | `/usr/sbin/useradd` | what `--create-user` runs |
| `OMAHOUSE_NOTIFY_SEND` | `notify-send` | how a warning is said |
| `OMAHOUSE_SYSTEMD_RUN` | `systemd-run` | how it reaches another session |
| `OMAHOUSE_LOGINCTL` | `loginctl` | how a session is ended |
| `OMAHOUSE_JSON` | unset | same as `--json` |

The first two of those are also what keeps a run pointed somewhere else from
biting. A `Close` is refused unless the cgroup tree really is `/sys/fs/cgroup`,
because a tree in `$TMPDIR` holds pids somebody typed and those are real pids on
whatever machine is reading it; and a `terminate-user` is refused unless the
configuration really is `/etc/omahouse`, because the block behind it would be a
file no PAM stack reads. Both refusals name themselves in the journal.

`status` also reports what it cannot see: a scope under `app.slice` whose name
it could not read, and the processes in `session.slice` it can neither count nor
close. That second number is never zero on a live session — an app started
outside `uwsm app` lands in the compositor's own cgroup, and `spec.md` §5 asks
for that to be said rather than hidden.

And it reports what a scope holds when that is not what its name says. An app
launched through a shim takes the shim's name: this machine has seven scopes
called `gtk-launch` with VS Code inside them, so allowing `gtk-launch` allows
whatever it launches next. `status` prints what is really running under each id,
and `allow` says it again at the moment somebody writes the rule. It warns and
does not refuse — the same reading calls a flatpak a shim, because every flatpak
runs `/usr/bin/bwrap` — and the rule goes on matching the id, which is the only
thing the model decides by.

## What happens when the time is out

Three things, and the first two are one sequence.

**Closing** is `SIGTERM` to every process in the app's scope, then that scope's
`cgroup.kill` once the profile's `grace` has gone by. The polite half first, so
an editor writes its buffers; the write second, because it takes the whole
cgroup at once with no reaping order and no orphan. An app that leaves on its
signal is never written about.

**`session.slice` is never reached**, and that is structural rather than
careful. Only `app.slice` is walked, only a scope found in that walk can be
named, and the write is refused unless the path is inside that user's own
`app.slice`, is a `.scope`, has no `session.slice` in it, and lives in a real
cgroup tree. This is the failure that would end the product — an allowlist
taking Hyprland down two seconds after somebody logs in — so it is checked five
times over rather than implied once, and proved in a VM with a real Hyprland
on a real seat.

**Logging out is two things.** `loginctl terminate-user` on a machine with
autologin was measured putting the session straight back up, so the name goes
into `/etc/omahouse/blocked` first and stock `pam_listfile` refuses the next
login:

```
account required pam_listfile.so item=user sense=deny \
        file=/etc/omahouse/blocked onerr=succeed
```

`onerr=succeed` is not optional: a missing or unreadable file has to let
everybody in, or a machine locks itself out. The package installs that line and
takes it out again when it is removed, along with the list.

The name comes out on its own, and nothing has to remember to take it out.
Every cycle works out from today's ledger who should be refused right now and
writes exactly that, so the turn of the day, a `grant` of ten minutes,
`profile enforce --off` and `profile remove` each let somebody back in.

## The window

`omahouse-studio` is the same model with a Qt Quick front on it. It reads by
linking the two libraries directly — everything it reads is world readable — and
it writes by running `pkexec omahouse <verb>`, so the privileged half is the CLI
above, with the same argument checking and the same refusals. **It is never
root**, and it refuses to start as root rather than working and letting nobody
find out.

Two faces, and nobody picks one on screen. Whoever is in `wheel` gets the
operator's: the people under rules, the programs released to each of them, the
day's balance live, and the chips to change all three. Everybody else gets the
subject's, which is the same window with nothing to press — what is left today,
by program and in total.

Everything is on the keyboard, and the mouse does exactly the same thing. There
is one table of commands in the window; the chips are drawn from it, the keys are
looked up in it, and `:` and `?` list it — so an action cannot exist on only one
of the two.

- `j` / `k`, `↓` / `↑` — move the cursor
- `g` / `G`, `Home` / `End` — first, last row
- `l` / `Enter` — open the profile under the cursor
- `h` / `Esc` — back, or clear the filter, or leave a control
- `1` / `2` / `3` — the people, their programs, their day
- `/` — filter the list · `:` — commands · `?` — the key map
- `Tab` — next control · `Space` — press the one the keyboard is on
- `n` — put an account under rules · `e` — close programs, or only watch
- `d` — only the listed run, or everything but the listed
- `a` — release a program · `m` — minutes a day · `+` — more time today
- `x` — take a program off the list, or an account off the books

The picker behind `a` is fed from the installed `.desktop` entries, with whatever
is open in that account right now at the top — and it says when a scope is not
what its name says, so nobody releases `gtk-launch` without being shown the VS
Code inside it.

**It is not a security boundary.** A program started from inside a terminal
inherits the terminal's scope, so a profile with a terminal on its allowlist is
a profile that allows everything — and all of it counts as terminal time. Do not
allow a terminal in a profile that is meant to hold. What the model does hold,
because none of it depends on the goodwill of the session, is the clock, the
`loginctl` logout, the counting and the daemon — `spec.md` §10.

## Build

```bash
mise run deps        # Qt 6 + qmake
mise run build       # build/bin/omahouse and build/bin/omahouse-studio
mise run studio      # open the window
mise run test        # unit tests and the CLI end to end
mise run lint        # the QML, with every warning fatal
mise run studio:test # the window, by keyboard and by mouse, with no screen
mise run usage:gen   # docs/cli.md, from omahouse.usage.kdl
mise run verify      # the local gate: all of the above
mise run hooks:install
mise run test:vm     # the teeth, in the VM — never on this machine
```

`test:vm` is layer 2 of `testing.md` and is deliberately not in the gate. It
starts the `omahouse-poc` domain, waits for a real Hyprland session, installs
the build and the packaging, and then closes processes and ends a login. It
proves it is talking to that machine three ways — the domain and its disk, an
address that is not one of this machine's, and `uname -n` on the far side of the
ssh — before it loads a single case.

Shadow build only — qmake refuses to configure inside the source tree.

## Requirements

- Qt 6: `qt6-base`, and `qt6-declarative` for the window and its linter
- `polkit` for `pkexec`, which is how the window writes
- [mise](https://mise.jdx.dev/) for the tasks above, and for the pinned `usage`
  that generates `docs/cli.md`

## Design

`spec.md` says what omahouse is and why the enforcement lives in a root daemon
rather than in the session. `plan.md` says in which order it gets built and what
each stage has to prove before the next one starts. Both are in Portuguese.

Released under the MIT license.
