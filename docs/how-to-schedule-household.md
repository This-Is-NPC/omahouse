# Share a daily allowance across computers

Omahouse owns profiles, credit, accounting, enforcement and the operator's
window. The omahouse Battery coordinates collection and delivery. Omakure owns
the schedule, execution queue, authenticated API and run history. Installing
omahouse on one computer requires neither a Battery nor an Omakure scheduler.
Omastore also remains independent; it is not required for this workflow.

This is daily account credit, not a commercial till: there are no payments,
customer sessions, receipts or credit carried into tomorrow.

## Prepare the household

[Link the computers](how-to-link-another-computer.md) first. Each must run a
build with `omahouse allocation`, have the account and matching limited budget
IDs, and have its local `omahouse watch` service running. Keep their local date
and clocks aligned. The manager's profile supplies the household's daily limit.

This first policy includes **the manager (`here`) and every registered
machine**. All must have that account. It divides the unspent credit equally,
reserving already recorded consumption first. For example, with 120 minutes
and two idle machines, each gets 60 minutes. The manager is one of those two.
A separate management-only computer currently still reserves a share; there is
no selectable participant group or transfer between machines during the day.

Enrollment is an explicit change: it turns enforcement on, sets grace to zero,
and refuses limited budgets that only warn. An enrolled account with no portion
for the current date has zero available time. Do this outside an active session:
enrollment can end that session before the first synchronization finishes.
An administrator can still change or remove a profile; these are trusted
administrative overrides, not operations that preserve exclusive credit.

Upgrade every participant before enrollment. Do not downgrade an enrolled
machine: older binaries do not implement the allocation contract. Back up the
profiles and state together. Historical `leave` entries without a `kind` cannot
be distinguished from genuine credit; start this mode on a new day after the
upgrade, rather than interpreting old entries as new household credit.

## Install the Battery scripts

Use the trusted workspace of the account that runs `omakure node serve`, on the
manager and each participant. Substitute your actual workspace and Battery URL.
The commands below run **as that service account**, not as the person whose time
is limited.

```bash
omakure --scripts-dir /var/lib/omakure-workspace battery add \
    /path/to/omahouse-battery --ref master --name omahouse
omakure --scripts-dir /var/lib/omakure-workspace battery sync omahouse
omakure --scripts-dir /var/lib/omakure-workspace battery install \
    omahouse omahouse.day
omakure --scripts-dir /var/lib/omakure-workspace battery install \
    omahouse omahouse.allocation
omakure --scripts-dir /var/lib/omakure-workspace battery install \
    omahouse omahouse.sync
```

The service account needs noninteractive permission for the writing omahouse
verbs. The pairing workflow provisions that access. Check that the node service
uses the intended workspace and restart it after installing the scripts. Do not
start a second scheduler for the same workspace.

## Enroll, then enable the schedule

On the manager, as the node service account:

```bash
python3 /var/lib/omakure-workspace/omahouse-sync.py \
    --action enroll --users 'kid'
```

This initializes a manager authority, binds participants to it, collects their
days, persists exclusive reservations, delivers portions and reads them back.
Require `ok: true`. If any part fails, fix the named machine and repeat the same
command. Enrollment and delivery are idempotent; failure does not reset time.

From the Battery checkout, create the separate, locally owned scheduled script:

```bash
python3 .scripts/configure-sync.py \
    --workspace /var/lib/omakure-workspace --users 'kid' --cron '* * * * *'
```

The helper asks Omakure to validate and create `omahouse-schedule.py`, with an
explicit enabled `Schedule`. Omakure's existing node scheduler executes it every
minute. Installing or updating the Battery does not activate a schedule or
replace this locally owned wrapper. Several accounts can be named as a
space-separated string; they must exist on every participating machine.

To disable future scheduled runs, repeat the setup with `--disable`:

```bash
python3 .scripts/configure-sync.py \
    --workspace /var/lib/omakure-workspace --users 'kid' --disable
```

An already running job may finish. Existing portions remain in force until the
local date changes; disabling a schedule neither removes profiles nor restores
standalone limits.

## Operate and inspect

```bash
omakure --scripts-dir /var/lib/omakure-workspace --json history list --limit 20
omakure --scripts-dir /var/lib/omakure-workspace --json history show RUN_ID
omahouse allocation show kid
omahouse house kid
sudo omahouse grant kid --session 10m
```

A scheduled success has `trigger: Scheduled`, `state: completed` and exit 0.
The run output identifies the account, stage and revision; inspect a failed row
instead of treating missing reports as zero consumption. `house` shows a total
of observations, not an authorization to spend the same remainder everywhere.

New grants are household credit. The next successful synchronization divides
them among the existing portions. The CLI and window do not promise that a
remote computer received time merely because the grant was recorded. Do not
schedule `leave`: it is a local adjustment, and repeating it after consumption
refills a remainder. Enrolled profiles refuse it.

In the operator's Studio, select an account and press **f** for machines. Each
budget shows the machine's last reported consumption, its received portion,
remaining time and observation timestamp. **+** records household credit through
the same CLI. A recent report is not a live connection indicator. Pairing,
enrollment and schedule configuration remain explicit CLI/setup operations.

## Failures preserve reservations

- Every plan requires enrolled observations at most 120 seconds old. A missing,
  stale or unreachable participant prevents a new plan; successful observations
  are still saved for the panel.
- Portions are absolute daily caps. Repetition, process restart and uncertain
  delivery cannot top them up. Applied documents must match on readback.
- An offline machine keeps its portion and can use it locally. Other machines
  cannot borrow it. New grants wait for a complete collection.
- Membership is frozen once a plan is issued for the day. Removing or adding a
  machine then stops planning; it does not reclaim credit. Change membership
  before the next day's first plan, with the intended profiles enrolled.
- The manager persists reservations before delivery in
  `/var/lib/omahouse/allocations/<user>/<date>.json`. Never delete or roll back
  this state to retry a job. Missing or older reservations with issued portions
  are refused; restore a consistent backup or wait for a new day.
- At midnight, yesterday's portions expire. Offline machines receive no new
  day's credit until a complete synchronization succeeds.

The guarantee concerns issued credit. Enforcement still has the existing
watch interval, process termination latency and operating-system dependencies;
it is not second-perfect billing. A stopped daemon or a privileged account can
bypass local enforcement. Site policy also retains its documented machine-wide
browser scope.

The data goes through the paired node's authenticated console API. It does not
travel through Omakure's Health Plane or Remote Cue acknowledgements. Protect
that LAN/API transport using the same deployment requirements as pairing.
