# Handoff — the session of 2026-09-08

Written at `3a9d18e`, on `spike/omakure-core`, **129 commits ahead of `master`
and none of them pushed.** The tree is clean and `mise run verify` is green.

This is one session's account of itself: what it changed, what it decided, what
cost the most to find out, and what it left. It is not a substitute for
[`docs/design.md`](docs/design.md), which is where the reasoning belongs
permanently — anything here that outlives the week should end up there instead.

---

## The rules that were in force

They came from the owner and they held all day. A reader picking this up should
assume they still hold.

- **Nothing is pushed.** Said once, plainly, and never lifted.
- **Nothing is tested on the owner's machine.** omahouse closes processes and
  ends sessions. Only in a VM, and only the ones
  [`vm/manifest.toml`](vm/manifest.toml) names. `omahouse-omarchy` is **not
  disposable** — it is the demonstration machine and the one thing here nobody
  could rebuild.
- **Break every test you write.** No green that has not been seen red.
- **Documentation, code and comments in English.** Always. Only the conversation
  with the owner is in Portuguese.
- **Measure before writing product code.**
- **There are no users and nothing is released**, so cut clean: no accommodating
  an old file format, no second spelling kept for kindness.

## The gate

`mise run verify` — `usage-check.sh`, then `test.sh` (the C++ suites and
`tests/test_cli.py`), then `qml-check.sh`, `studio-check.sh`, `shots-check.sh`.
The pre-commit hook runs the same script, so a commit that lands has passed it.

The VM suite is separate and is not in the gate: `vm/run.sh --case <name>`, or
`mise run test:vm`. It takes minutes and it starts real guests.

---

## What changed, and why it was not obvious

### A budget that never resets did not work at all

`Resets::Never` shipped earlier in the day and was broken in four ways that only
showed when something read it end to end.

**The loop never carried a pot past midnight.** `evaluate` carries one forward
when it is handed a ledger from another day, and the daemon never hands it one:
it builds the path from `now.date()`, so the first tick after midnight meets a
file that is not there, calls it an empty today, and the turn is over before the
core is asked. `tst_policy`'s midnight test drives `evaluate` directly and could
not see it — **the defect was in who calls it.** `readDay` in
[`src/sys/Day.h`](src/sys/Day.h) is the answer: every verb that is about *now*
goes through it, and where today has no file it finds the newest ledger from
before today. The newest and not yesterday's, so a machine switched off for a
week behaves like one switched off for a night.

**A grant died at midnight while the spending it paid for did not**, so a pot
refilled at eleven met the morning with yesterday's hour against today's smaller
allowance. `keptGranted` is the fold that fixes it: the turn folds the outgoing
day's operator credit into a running total, once. Not carried as grants —
`report`, `house` and `collect` all add days together out of `grants` and would
count one hand-over once per day it outlived.

**`grant` erased the pot.** It reads today, changes it, writes it back; on a day
with no file yet it wrote a day with no pot in it.

**`status` and the window never showed a pot's usage**, because they read the
daily counter for every budget alike. `spentSeconds` asks that question in one
place now.

### A grant did nothing on an enrolled machine

The balance was `credit - elsewhere` out of the household's statement and a
grant is in neither number, so `grant` wrote the ledger, printed the same
balance back and changed nothing until the manager next planned — and on a
machine that had lost contact, never.

Adding it on top was not available: the manager folds every grant it collects
into `credit`, so a machine adding its own would count them twice the moment the
next statement landed. The statement carries a third number per budget,
**`counted`** — how much of *that machine's own* credit is already inside
`credit`, taken from the very day the credit was summed out of. The machine adds
the rest. Exactly once, by subtraction and never by comparing a grant's clock
against a statement's.

It is required in a document rather than defaulted. Absent it reads as *the
household has counted none of this machine's grants*, which hands over every
grant ever made here a second time.

`pendingAllocation` is gone from `grant --json`. It existed to say the minutes
were not live yet, which was the defect.

### A pot went through a guard that is about days

When the statement is not for today the machine falls back to what an operator
handed over — right for a daily budget, where the turn of the date is exactly
when the household should have spoken again. A pot does not recharge at
midnight, so there is no new allowance to issue.

It did not merely take the balance away. With the spending still carried and the
allowance gone, **an hour and a half left over came out as an hour and a half
overdrawn**: the line dropping at midnight did not stop somebody spending a pot,
it locked them out of one they had not finished.

### One pot for the whole house

This is the case the product is for and it did not work. `planAllocations` was
written for a thing that recharges: it summed `secondsFor`, which is zero for
every pot there has ever been, so `elsewhere` came out zero on every machine and
each was told the whole pot; and it rebuilt `credit` out of today's grants
alone, so every refill older than one night was forgotten by everybody but the
computer it was typed on. **Refills crossed between computers and spending did
not** — worse than having no household at all, and in the direction that gives
time away.

Neither of a pot's numbers is a day. A collected day is a whole ledger and
already carried both — nothing new had to travel, the plan had to ask.
`spentSeconds` and `givenTo` are the two questions, one function each, because
three callers must agree or the household says two different things.

Two rules fell out of it and both are load-bearing:

- **The fold takes operator credit and not every grant.** It took all of them
  while it was local and cosmetic; now that it feeds the household's number, a
  `leave` adjustment made on one machine would become credit for the house.
- **`leave` is refused on a pot.** It answers *how much remains today* and a pot
  has no today. Allowed, it had two wrong answers available and no third:
  expire at the turn and undo itself, or survive and refill the pot every
  morning.

### Where each counter may be read

This bit is easy to "fix" wrongly. `omahouse house` and the machines panel are
handed **one day per machine**, so they may read a pot's running total — it is
read once. `report` walks a range and must go on reading the daily map alone,
because a pot is carried into the file of every day it touches and would be
counted once per day there. Somebody who does not see that difference will
think `report` is the inconsistent one.

### Two holes in the gate itself

**A broken fixture produced a colourful screenshot and the shots gate stayed
green.** `shoot()` now refuses a frame taken with `House.error` set, or with
profiles on disk and none on screen.

**`reset` in [`vm/e2e.py`](vm/e2e.py) emptied `/etc/omahouse` by naming files**,
while the peer guard beside it refuses a peer whose `/etc/omahouse` is not empty.
Two descriptions of one condition, and the list drifted from it twice — last by
`staged/`, a directory no `rm -f` of file names was ever going to reach. A run
that *finished cleanly* left the peer dirty and the next run blocked on its own
leftovers, pointing at the machine. It is now the same condition the guard
checks.

### One self-inflicted trap worth knowing about

`profile show --json` answers in the envelope `profiles.json` uses, because the
bytes it emits are the bytes `profile apply-staged` takes in on another
computer. That changed earlier in the same session, with the verbs that push a
profile between machines, and
`vm/cases/a_battery_schedule_shares_one_balance.py` still read the bare profile.
Nothing had run that case since. The failure was `KeyError: 'allocation'` two
lines after the household's credits were checked, which reads like the household
losing a statement and was a shape that had moved underneath.

**A VM case is not covered by the gate. Changing an interface it reads will not
fail until somebody runs it.**

---

## What is open

1. **No verb writes `resets`.** A pot cannot be asked for from the command line
   or from the window — it arrives only in a profile written by hand or pushed
   from a manager. The model now does the lan-house case; the interface does not
   let anybody request it. This is the last thing between the code and what the
   page promises, and it is a gap in the interface and not in the model.
2. **The household's remote rows and a pot.** `omahouse house` and the panel
   show each machine's own spending correctly, and the capacity over it comes
   from the same `givenTo` the statement uses. What has not been exercised end
   to end is two real machines sharing one pot: no VM case does it, because no
   verb can create a pot to test with (see 1).
3. **Nothing is pushed.** 129 commits, one branch, one disk.

---

## Two sessions, one tree

A second Claude session (the omakure/omarchy-kids plugin work) was writing to
the same checkout and the same branch all day, mostly under
`docs/not-built/profiles-across-a-network.md`. Its commits and this session's
did not collide, and that was luck rather than design: both sessions commit with
`git add -A`, and at one point one session's staged file sat in the tree while
the other was about to commit.

**Check `git status` before staging.** A file you did not touch may be somebody
else's work in progress.

The same page has twice been left saying something the code had just stopped
doing, in both directions — once promising a case that did not work, once
denying one that had started working twenty minutes earlier. If you change what
a pot does, that page and [`docs/design.md`](docs/design.md) §12 both have to
move with it.
