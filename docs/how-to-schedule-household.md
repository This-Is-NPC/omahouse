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

This policy includes **the manager (`here`) and every registered machine**. All
must have that account. It does not divide anything. Each machine is told two
numbers per budget — the household's **credit** for the day, and how much of it
the **other** computers have already spent — and works out the same balance from
them. With 120 minutes and two machines, both see 120 minutes; when one has
spent 40, both see 80. An hour is an hour wherever the person sits.

**What that gives up is exclusivity.** Two computers both told there are thirty
minutes left can both start spending them, and the overspend is bounded by how
often the household reports and by nothing else. The reporting cadence is
therefore not a tuning knob: it is the thing that holds the sum together. Set it
as fast as the household can bear.

Enrollment is an explicit change: it turns enforcement on, sets grace to zero,
and refuses limited budgets that only warn. An enrolled account with no
statement for the current date has zero available time. Do this outside an active session:
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
days, persists the issued statements, delivers them and reads them back.
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

An already running job may finish. The last statement each machine received
remains in force until the local date changes — which means a household that
stops reporting goes on spending a balance nobody is updating. Disabling a
schedule neither removes profiles nor restores standalone limits.

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
of observations, and the balance under it is what every computer is told it may
spend — the same remainder in each of them, which is the point and also the
reason two of them can spend it at once.

New grants are household credit, and they are also time on the machine they
were typed on, straight away. The statement each computer holds says how much of
that computer's own credit the household has already folded into the total, so
the machine adds what it has handed over since — and when the next
synchronization folds it in, the credit and that number rise together and the
balance does not move. A grant is ten minutes once, whether the manager is
reachable or not.

What is still not promised is that a *remote* computer has the time: a grant
made here reaches the machine in the bedroom at the next synchronization and not
before. On a day the household has said nothing about at all, an enrolled
machine allows exactly what an operator has handed over and no daily number —
the household's allowance stays the household's to give.

Do not schedule `leave`: it is a local adjustment, and repeating it after
consumption refills a remainder. Enrolled profiles refuse it.

**A budget that never resets is shared like everything else, and it does not
expire.** One pot for the household: what a computer spends of it comes off the
others, and half an hour handed over on one is credit on all of them from the
next synchronization. What differs is that a pot's statement never goes stale.
Its two numbers are everything the pot has ever been given and everything the
other computers have ever spent of it, neither of which is a day — so the line
can drop for a week and what is left is still there to spend, down to zero, out
of the last statement the machine received. A daily budget on a day the
household has said nothing about is nothing plus what an operator handed over; a
pot is not, and that is the difference between an allowance and a credit.

`leave` is refused on a pot. It sets how much remains *today*, and a pot has no
today; `omahouse grant <user> --budget <id>=<duration>` is what puts more in
one.

In the operator's Studio, select an account and press **f** for machines. Each
budget shows the machine's last reported consumption, the household's credit,
what is left of it and the observation timestamp. **+** records household credit through
the same CLI. A recent report is not a live connection indicator. Pairing,
enrollment and schedule configuration remain explicit CLI/setup operations.

![The machines view: `MACHINE / BUDGET` beside `USED / CREDIT / LEFT`; `here / session` at 1h10m / 2h10m / 20m, marked `statement received` and `Last report: local`; and `station-02 / session` at 40m / 2h10m / 20m, marked `stale report; the balance below is behind` and `Last report: 2026-09-01T09:30:00`.](img/31-operator-machines.png)

## What a failure does to the balance

- Every plan requires enrolled observations at most 120 seconds old. A missing,
  stale or unreachable participant prevents a new plan; successful observations
  are still saved for the panel.
- **A statement is not a top-up.** Repetition, process restart and uncertain
  delivery change nothing: `credit` is whatever the household's number is now,
  and applying the same document twice is the same number twice.
- **What other computers spent only ever goes up.** A statement reporting less
  than the last one is refused, because consumption adds up and honouring it
  would hand the machine the same minutes twice.
- **An offline machine goes on spending the balance it last heard.** Nothing is
  reserved for it and nothing is held back from anybody else, so the house can
  overspend by roughly the reporting interval times the number of machines. That
  is the trade the balance makes and the reason the interval matters.
- Membership is frozen once a plan is issued for the day. Removing or adding a
  machine then stops planning. Change membership before the next day's first
  plan, with the intended profiles enrolled.
- The manager keeps the issued statements in
  `/var/lib/omahouse/allocations/<user>/<date>.json`. A machine holding a
  revision the manager has no record of is refused; restore a consistent backup
  or wait for a new day.
- At midnight, yesterday's statement expires. Offline machines receive no new
  day's credit until a synchronization succeeds.

The guarantee concerns issued credit. Enforcement still has the existing
watch interval, process termination latency and operating-system dependencies;
it is not second-perfect billing. A stopped daemon or a privileged account can
bypass local enforcement. Site policy also retains its documented machine-wide
browser scope.

The data goes through the paired node's authenticated console API. It does not
travel through Omakure's Health Plane or Remote Cue acknowledgements. Protect
that LAN/API transport using the same deployment requirements as pairing.
