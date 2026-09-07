# Household scheduling validation — 2026-09-07

The feature was exercised from the local working trees of omahouse and
omahouse-battery. The implementation is recorded in omahouse `5221fe8` and
omahouse-battery `ff26a04`. Nothing was published or installed as a host service.

## Host

The official `.scripts/verify.sh` gate passed: 343 Qt model/sys test entries,
75 CLI cases, 14 Studio entries, fatal-warning QML checking, generated usage
checking and byte comparison of generated screenshots. Qt totals include test
setup/cleanup entries. The two screenshot-producing Studio cases skip in the
behavior run and run separately through the screenshot gate.

`python3 ../omahouse-battery/.scripts/test-sync.py` passed ten cases using real
CLI processes and disposable file roots. Remote transport is replaced in this
suite. It covers repeated consumption, partial delivery, an offline peer and a
later grant, matching readback, explicit enrollment, overlapping coordinator
runs, concurrent grants, stale/backward observations, concurrent operator edits
and Omakure validation/disable/refusal of the locally owned schedule wrapper.

The leave/house regression was first observed against the original arithmetic:
leaving 30 minutes locally changed the household's 120-minute credit to 60.
The permanent CLI regression now checks unchanged household credit through
30m, 45m and 0m local adjustments, followed by a genuine 10-minute grant.

## Two disposable guests

```bash
vm/run.sh --case a_battery_schedule
```

Guests: `omahouse-poc` and `omahouse-dad`, through the existing guarded VM
harness. The case uses actual Omakure binaries, a locally installed Battery,
paired console HTTP authentication, the real scheduler and systemd service
restart. The manager-owned wrapper is created by the Battery's actual
`.scripts/configure-sync.py`, with a ten-second cron for the test.

Observed result: **PASS**, 120 seconds for the case, 196 seconds including
provisioning and cleanup. Assertions required:

- Explicit enrollment succeeded, with two issued documents totaling 7,200s.
- At least two real `Scheduled` history rows completed successfully; the
  persisted reservation remained identical.
- A manager service restart produced another scheduled success and did not
  change the peer's received allocation.
- Stopping the peer's Omakure service produced a failed scheduled run without
  releasing or modifying reserved credit.
- Restarting the peer and adding ten minutes produced a new revision totaling
  exactly 7,800s, with that revision confirmed on the peer.
- Both disposable guests were reset and shutdown was requested by the harness;
  no cleanup failure was reported.

The first attempt failed its history assertion because it searched for
`succeeded`; Omakure's documented success state is `completed`. The corrected
case uses that state and the actual schedule configurator. The full successful
console output was retained during development at
`/tmp/omahouse-scheduled-vm-2.log`; this checked-in record and the executable
case are the durable evidence, since `/tmp` is temporary.

These checks prove the issued-credit protocol and scheduled integration. They
are not a commercial billing certification or a claim of exact-second process
termination. Local enforcement retains omahouse's existing watcher, PAM and
browser-policy constraints. Read the
[operating policy](../docs/how-to-schedule-household.md) before enrollment.
