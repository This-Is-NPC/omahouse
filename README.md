# omahouse

House rules for the accounts on an Omarchy machine: which programs each profile
may open, and for how long. The model is a lan house counter — an operator adds
time, the machine counts it, warns before it runs out and ends the session when
the credit does.

The model lives in `src/core` and it is pure. `evaluate` is handed the app
scopes that are running, the profile, the day's ledger and `now`, and gives back
the new ledger and what to do about it; nothing in it reads the machine or the
clock, which is how a two hour budget is proved in microseconds. Reading the
real cgroup tree and acting on a decision arrive with stages 3 and 7 of
`plan.md` -- for now the CLI answers `--version` and `--help` and nothing else.

## Build

```bash
mise run deps      # Qt 6 + qmake
mise run build     # build/bin/omahouse
mise run test      # unit tests
mise run verify    # the local gate
mise run hooks:install
```

Shadow build only — qmake refuses to configure inside the source tree.

## Requirements

- Qt 6: `qt6-base`
- [mise](https://mise.jdx.dev/) for the tasks above

## Design

`spec.md` says what omahouse is and why the enforcement lives in a root daemon
rather than in the session. `plan.md` says in which order it gets built and what
each stage has to prove before the next one starts. Both are in Portuguese.

Released under the MIT license.
