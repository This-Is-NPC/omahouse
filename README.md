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

Today the CLI reads. Acting on a decision arrives with stage 7 of `plan.md`.

```bash
omahouse status [user]     # live app scopes, what they are, and what is left
omahouse report <user>     # the day's ledger, or --since a day
omahouse profile list
omahouse profile show <user>
```

`docs/cli.md` is the whole of it, generated from `omahouse.usage.kdl`. Every
root moves by variable, which is how the end to end suite runs as an ordinary
user with nothing installed:

| variable | default | what |
|---|---|---|
| `OMAHOUSE_CONFIG_DIR` | `/etc/omahouse` | `profiles.json`, who is under rules |
| `OMAHOUSE_STATE_DIR` | `/var/lib/omahouse` | `<user>/<AAAA-MM-DD>.json`, the day |
| `OMAHOUSE_CGROUP_ROOT` | `/sys/fs/cgroup` | where the app scopes are read from |
| `OMAHOUSE_JSON` | unset | same as `--json` |

`status` also reports what it cannot see: a scope under `app.slice` whose name
it could not read, and the processes in `session.slice` it can neither count nor
close. That second number is never zero on a live session — an app started
outside `uwsm app` lands in the compositor's own cgroup, and `spec.md` §5 asks
for that to be said rather than hidden.

## Build

```bash
mise run deps      # Qt 6 + qmake
mise run build     # build/bin/omahouse
mise run test      # unit tests and the CLI end to end
mise run usage:gen # docs/cli.md, from omahouse.usage.kdl
mise run verify    # the local gate: docs current, then the tests
mise run hooks:install
```

Shadow build only — qmake refuses to configure inside the source tree.

## Requirements

- Qt 6: `qt6-base`
- [mise](https://mise.jdx.dev/) for the tasks above, and for the pinned `usage`
  that generates `docs/cli.md`

## Design

`spec.md` says what omahouse is and why the enforcement lives in a root daemon
rather than in the session. `plan.md` says in which order it gets built and what
each stage has to prove before the next one starts. Both are in Portuguese.

Released under the MIT license.
