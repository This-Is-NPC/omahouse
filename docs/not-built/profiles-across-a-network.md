# Profiles across a network — designed, and mostly not built

> **Phases A and B shipped on 2026-09-08. Phase C did not, and §7 marks each
> step.** A budget can be `daily` or `never`; a profile can name no account and
> rule whoever sits at the machine; every profile records who wrote it and when;
> a manager pushes one through a staging file and stands off a human edit it did
> not make; a machine sends its own back in the same shape; and a merge answers
> four things about the two copies. What is open in phase B is the manager's own
> script, which lives in `omahouse-battery`. Phase C — one pool, reported and
> read back — is designed here and not built.
>
> The page also records a model that was built **wrong** and replaced the same
> day: a budget anchored at the *login*, with a sitting in the ledger. §1 keeps
> why it was wrong, because the allowance belongs to the person and does not
> come back on a new login. Everything else below is a record of a design decided
> on 2026-09-08, kept so that the next person who wants omahouse to govern more
> than one household spends an afternoon reading instead of a week rediscovering
> the same three corners.
>
> **What it is for.** The engine is already general.
> [`design.md` §4](../design.md) says so in its own words: *"The same schema with
> `default: "allow"` and a `deny` rule per distraction is a focus profile for an
> adult. Nothing in the engine changes."* The parent and the child are the way
> the product reaches people — omarchy-kids — and not the model. The model is
> accounts, scopes, budgets, machines and an authority. A lan house, an office
> and a school are the same nouns with more of them.
>
> **What already exists.** Profiles, rules, budgets, enforcement, the ledger,
> `omahouse day` and `omahouse collect`, a household that sums what several
> computers spent, and an allocation that divides a daily allowance between them.
> The accounting across machines is built. The transport is built.
>
> **What does not exist.** A budget whose clock starts at login. A profile that
> names no account. A profile that arrives from somewhere else. One pool of time
> spent wherever the person sits. A tiebreak between two people who wrote the
> same profile.
>
> §1 to §6 are the decisions and their reasons. §7 is the order of work. §8 is
> what was proposed on the way here and refused, which is the part worth reading
> before proposing it again.

---

## 1. The three shapes a profile has to express

Three examples decide the model, and they differ in one thing only: **whether the
clock goes back to zero, and when.**

| the shape | resets | exists today |
|---|---|---|
| five hours a day | at the turn of the local date | yes |
| unlimited, everything installed, counted | never runs out | yes |
| two hours, and when they are gone they are gone | **never resets** | **no** |

The first is a session budget with `dailyMinutes: 300`. The second is
`default: allow` with a budget whose limit is zero, which `Profile.h` already
describes: *"Zero or less is a budget with no limit: it counts, and it never runs
out."* Both are expressible in the file that exists.

The third is not, and it is the only new arithmetic on this page.

### The allowance belongs to the person, not to the sitting

**Two hours means two hours until somebody grants more.** Logging out and back
in does not return them. Moving to another machine does not return them. The
turn of the date does not return them. They are spent, and the operator grants
again or does not.

That is what a lan house sells and what an office allocates, and it is a
different thing from the household's daily allowance rather than a variation on
it. The household refills at midnight because tomorrow is a new day for a child.
A purchased hour has no tomorrow in it.

**So the field is `resets`, and its values are `daily` and `never`.** An absent
`resets` means `daily`, so every file on disk keeps the meaning it has. That is
the discipline §4 of `design.md` already applies to `kind`, `presence` and
`sites`: a file that already says what it means is not rewritten to say it
differently.

Both values are available in any installation. A household that wants a
purchased-credit budget beside its daily one may have it, and a lan house that
wants a daily cap beside its credit may have that. Nothing in the engine chooses
between them.

### Two rules a pot needed once the household could carry it

**The fold carries operator credit and not every grant.** It used to carry all of
them, which was cosmetic while the fold was local. Once the fold feeds the
household's number, an adjustment made on one machine would have become credit
for the whole house.

**`leave` is refused on a pot.** It answers *how much is left today* and a pot
has no today. Allowed, it had only two wrong answers available: expire at the
turn and undo itself, or survive and refill the pot every morning.

### The idea this replaces, and why it was wrong

An earlier version of this page asked for a budget **anchored at the login**: a
clock that starts when somebody sits down and starts again when the next person
does. That was wrong, and the correction is recorded rather than removed because
the wrong version shipped before it was caught.

It was wrong because **the allowance is the person's**. A clock that restarts per
sitting hands two hours to anybody who logs out and logs back in, which is the
opposite of a limit. Nothing in the product ever wanted to count sittings.

The measurement that was made while chasing it is kept here because it is true
and because it is the reason the mistake was visible: `loginctl show-user
-p Timestamp` **does not move** when the same account logs out and logs in again.
Three sittings on a disposable guest, with lingering on and with it off, all
answered `Mon 2026-09-07 19:16:16 UTC`. That was read as a defect at the time. It
is the correct behaviour for the model this page now takes, and reading it as a
defect is what exposed the model as wrong.

**What has to be undone, and what must not be.** `resets: "daily" | "session"`
shipped on 2026-09-08, with the sitting in the ledger, grants stamped with a
sitting, and warning marks cleared at a new one. Three of those four serve an
anchor this page no longer asks for and come out. None of them reaches a verb or
a screen, so nothing on any machine behaves differently yet.

**The fourth stays, and it stays for a stronger reason than it arrived with.**
The seconds of a budget that does not reset live in a map of their own, beside
the daily one and never inside it. The ledger is one file per day, and
`consolidate` in `src/core/Fleet.cpp` adds `secondsFor` over every day and every
machine. A running total carried into the file of each day it crosses would be
**counted once per day** by everything that sums: `report`, `house`, `consolidate`
and `collect`.

That is not a consequence of sittings. It is a consequence of any counter that
outlives the day, and `never` outlives every one of them rather than a few. So
the separate map went from convenient to load-bearing, and pulling it out while
removing the sitting would reintroduce a double count that no test fails on until
somebody adds two days together.


### What "not an administrator" is, and what it is not

The second shape says *not an administrator*, and that word is only partly
omahouse's. Once a profile is pushed to machines, omahouse creates the account
there, so the profile does say whether that account is administrative, and
omahouse creates it that way.

It does not **hold** it. Somebody with root on the machine can undo it, and
[`design.md` §10](../design.md) stays true exactly as written: the force of a
rule is a property of who the operator is, not of the engine. omahouse declares.
It does not defend against the machine's own root.

---

## 2. A profile that names no account

`profiles.json` is keyed by login name, and the lookup is exact —
`profile.user == user`, then `uidForUser`. One profile names one real account.

A network of machines and rotating people cannot be written that way. Twenty
machines and forty customers is forty profiles typed by hand, and the fortieth
is written wrong.

**A fallback profile applies to any account with no profile of its own.** That
is what "administered on the machine rather than on the person" means in
concrete terms: the machine carries rules for whoever sits at it.

This has a consequence downstream that is easy to miss and is written in §6.

### The rules the fallback shipped with

Four decisions were taken while it was written, and they are here because none
of them is visible from the field alone.

**A profile of its own always wins**, and the whole list is walked by name before
the fallback is considered. The fallback can be written above the account's own
profile in the file, and *first match wins* would then answer with the wrong one.

**An administrator never inherits it.** `profile add` already refuses to write a
profile for somebody in wheel, saying an administrator does not fiscalise
themselves by accident; a fallback that caught them would do through the back
door what the verb refuses at the front. **This is a live filter and not a fact
about the file**: somebody put in wheel tomorrow has to fall out of a fallback
that was already written and is still correct, so it is evaluated every cycle
and cannot be moved into validation.

**The kind of machine is not a condition.** A fallback applies because somebody
wrote one. Making it depend on whether the machine is managed would add a second,
unwritten condition to a written thing — the exact shape of the house line that
one page promised and the code required more of.

**It answers under the real account name and never under `*`.** The reason is
not symmetry: the `omahouse allow` line a reader prints beside a verdict would
otherwise write a rule for an account that cannot exist.

**A fallback profile and a budget that never resets must not be used together.**
The pool belongs to the person and the fallback belongs to nobody in particular,
so on a shared account the two compose into one pool that empties and never
fills: the first person of the day spends the two hours and the second sits down
at a clock reading zero, with no midnight left to rescue them, because `never`
removed it. A fallback profile takes `daily` budgets. This belongs here rather
than in the head of whoever implements it.

---

## 3. One pool, spent anywhere

### What the household does today, and the sentence that denies this

[`how-to-schedule-household.md`](../how-to-schedule-household.md) is explicit,
and it was a decision rather than an oversight:

> `house` shows a total of observations, not an authorization to spend the same
> remainder everywhere.

and

> Portions are absolute daily caps.
> An offline machine keeps its portion and can use it locally. Other machines
> cannot borrow it.

So an hour of credit across two machines is thirty minutes on each. A person
sitting at the first is stopped at thirty with half an hour of credit still in
their name. **The total is respected and the placement is wrong.**

The equal division is not laziness. It is the answer that survives a machine
being offline, and that property is why it was chosen.

### The rule this page takes instead

**The pool is the person's, the record of it lives at the manager, and a machine
asks.** An hour is an hour wherever they sit.

> **True for a credit as of 2026-09-08, and it was not true for most of that
> day.** The plan a manager issues was built out of days and a pot has no day, so
> nothing a pot spent reached the other machines while its refills did — two
> machines each told the whole pot. §7 step 16 records what closed it. **What no
> verb can do is ask for a pot**: `resets` is read and honoured and nothing
> writes it, so a credit arrives only in a profile written by hand or pushed by a
> manager. Forty minutes at one computer leave
twenty at the next.

A machine does not receive a portion in the morning and does not hold the
authoritative count. It counts what is being spent, reports it, and reads the
remaining balance back. The cadence is the one the counter already runs at, so
the manager is never more than a few seconds behind: a machine that dies loses
those few seconds and nothing more.

That answers the case that decides the shape. Somebody spends forty minutes at
one computer, it breaks, and they sit at another: the forty minutes are already
at the manager, because they left the machine while they were being spent rather
than when the session ended. The second computer asks and is told twenty.

**The local file is a cache, and a machine uses it only when it cannot reach the
manager.** It is not a second record to be reconciled.

### Reversing the decision is part of the work

The sentence quoted above is in a page a person can read. Changing the behaviour
without changing the page leaves a document that is wrong, and changing the page
without saying why invites the next person to put the division back. Both edits
are the same task.

---

## 4. The profile comes from a central omahouse

The profile is not composed on the machine. It comes from a central omahouse,
and the local file is a cache of the last version that machine was told.

### It is a push, because the wire only goes that way

An earlier draft of this page said the machine **pulls** the profile at login.
That named a direction the wire does not have, and it is corrected here rather
than quietly removed.

Omakure's fleet model has three roles and one session direction. The Conductor
opens the authenticated Noise session towards the Performer. What crosses towards
the machine is a **Cue** — the name of a local script to run — or a **Baseline**,
a signed set of script bodies. What comes back is Profile, Pulse and Signal:
bounded health facts, not an answer to a question. **There is no ask-and-answer
plane, so a machine cannot interrogate the manager at login.**

So the manager **pushes**. It cues a script on the machine, that script writes
the profile, and the machine holds it as a cache carrying the written-at stamp of
§5. A login reads the cache and waits on nothing. No login ever blocks on the
network, which is the property that matters at eight in the evening.

### The cache is a fallback, not a second source

A machine asks the manager. The cached profile is what it uses when the manager
cannot be reached, and nothing else. There is no reconciliation between two live
copies, because only one of them is ever live.

An earlier draft made the cache carry more than that: it had the login reading
the cache as a matter of course and a short loan taken against the pool to stop
the same minutes being spent twice. Both were invented. With the manager holding
the count and every machine reporting at the counter's own cadence, two machines
spending at once are two machines the manager can see, and the balance each of
them reads is already right.

**A machine out of contact spends what it has.** The cache holds the balance the
machine last read, and it spends that down to zero. Two hours granted and thirty
minutes spent before the wire dropped leaves an hour and a half, and it is used.
A profile with no limit goes on having no limit. There is no timeout to invent
and no ceiling to choose: the balance is the ceiling.

### What it costs, and the way out

**A machine that has been out of contact past the freshness limit stops opening
new sessions**, even a second after the network returns, until the next push
lands. So the push cadence and the freshness limit are one decision and not two,
and both belong to whoever runs the household.

The way out is the reason omahouse is installed **whole** on every machine and
not as a thin client: an administrator sits at the machine and creates a profile
by hand. Everything needed to do that is already local.

A locally created profile has to be visible to the central omahouse when it comes
back, and mergeable there. This is the model a phone address book has used for
twenty years, and it works for the same reason: **every record knows where it came
from.**

### The gate a push goes through, and what that gate does not check

Every write to `profiles.json` today happens with a person at the keyboard, and
polkit is the gate: `packaging/org.omarchy.omahouse.rules` asks a named
administrator for their own password, and the file's own comment says that
action is the only gate in front of the CLI running as root.

**A profile pushed from the centre is a write with nobody at the keyboard, and
that door is already open.** It is not the wire that authorises it.
`letTheNodeReachOmahouse` in `src/cli/main.cpp` writes
`/etc/sudoers.d/omahouse-node`, mode 0440, holding one line:

```
<the node's account> ALL=(root) NOPASSWD: <path to the omahouse binary>
```

Both `machine invite` and `machine prepare` write it, on the manager and on the
managed machine, because the account that runs a Battery script has no shell and
no sudo while the writing verbs need root.

**The line names a binary and constrains no argument.** That is deliberate, and
its own comment gives the reason: *"The checks are omahouse's own, and a rule
naming `ALL` here would turn one script into a route to everything."*

The reasoning holds exactly as long as what travels is `collect`, which only adds
to a ledger. **The moment policy travels, the same line authorises `profile add`,
`allow` and `enforce`** — the writes §5 says have one correct version — and
omahouse's own check for those is the polkit gate, which is not in this path.

So the question is sharper than whether the wire is enough. It is: **once policy
travels, is a sudoers line that names a binary and no verb still the right
gate?** And what may a machine refuse from a manager it no longer recognises?
Neither is decided here, and both have to be answered before step 6 is written.

One part is settled already and needs no inventing. The line is treated as a
restraint and not as configuration: `packaging/omahouse.install` lists
`/etc/sudoers.d/omahouse-node` under `take`, and the removal message names it.

---

## 5. Two people wrote the same profile

### The tiebreak, and the smaller job it actually has

**The most recently written document wins.** An administrator who edited the
cache while the central omahouse was down beats the central copy, because their
edit is newer.

This requires fields the profile did not have. `schemaVersion` existed; a
written-at and a written-by did not, and without them there is no "most recent",
only two different files. They also do a second job: they are how the manager
tells a profile it issued from one an administrator created on a machine.

**The tiebreak decides which copy is current. It does not authorise discarding a
policy nobody has seen, and an earlier draft of this page conflated the two.**

The difference is not academic, because **the manager pushes on a schedule**.
Recency therefore favours the centre by construction: a hand edit made at two
o'clock is passed by a routine push five minutes later, with nobody deciding
anything and nobody knowing. That would defeat the reason omahouse is installed
whole on every machine — an administrator's fix that the next cycle erases was
never a fix.

So the push **stands off** rather than resolving. A local profile whose
`writtenBy` is not the node's account, carrying a stamp the manager never
issued, is a human edit the manager does not know about: it is left alone, and
the standing off is said out loud. The merge is where somebody decides, and it
proposes rather than deciding.

Without that, the merge would have nothing to propose — the push would have
destroyed the evidence before the merge ran, and the screen would list zero
forever without ever looking wrong, because zero is the ordinary answer.

**And the proposal carries the whole profile it believes it is replacing, not
its stamp.** The stamp has one-second resolution, so two edits inside the same
second are indistinguishable and a decision approved against the first would
land on the second. Carrying the document asks the question that has no clock in
it: *is this still what you were shown?*

### The trap: clocks

A tiebreak decided by a timestamp is decided by whichever clock is fast. A
machine running ahead wins every disagreement, including against edits made
later somewhere else, and nothing about the outcome looks wrong.

There is no clever fix. Either the clocks agree or the tiebreak is arbitrary.

[`how-to-schedule-household.md`](../how-to-schedule-household.md) already asks
for this in one line — *"Keep their local date and clocks aligned"* — as advice.
Under this design it stops being advice and becomes a requirement of the model.

### What the tiebreak must never touch

**Last writer wins is for policy. It is never for the ledger.**

A policy is a decision and has one correct version. Consumption is an
observation and **adds up**. If synchronisation overwrote observations by
timestamp, the forty minutes spent at one computer would disappear the moment a
second computer reported later.

The model already separates the two correctly and must keep doing so — though not
by the mechanism this page claimed when it was written.

**`omahouse collect` does replace.** It writes the arriving machine's snapshot
for that day over the stored one. That is correct: a machine's day is cumulative,
so its second report contains its first, and adding two reports from one machine
would count everything twice. What adds up is `consolidate`, and it adds up
across machines rather than within one.

So what protects consumption is not the absence of a replacement. It is
**monotonicity, checked before the write**. Inside the lock, `collect` refuses a
snapshot older than the one it holds — `stale snapshot` — and walks the stored
budgets to refuse any that arrives with fewer seconds than it already had —
`consumption moved backwards`. The second guard is *spending never goes
backwards* written as code, and it is the sentence this section rests on.

**Both guards sit behind a condition the documented path does not meet.** They
run only when the stored snapshot carries an `observedAt`, and `omahouse day`
stamps that field only for a profile enrolled in allocation. A household
following [`how-to-link-another-computer.md`](../how-to-link-another-computer.md)
— `omahouse day | ssh | sudo omahouse collect` — never stamps it. Neither
document has the field, the condition is false, nothing is checked, and a late or
smaller report overwrites a newer one in silence.

That is a defect in what exists today rather than in this design, and it is
written here because §5 is what found it: the section was drafted against the
source, and the source disagreed with it.

If `observedAt` becomes load-bearing for a network, **every `day` has to stamp
it** and not only an enrolled machine's.

The matching posture is already written down for missing reports, and it stays:

> inspect a failed row instead of treating missing reports as zero consumption

A machine that spends two hours offline and then dies before reconnecting has
produced a day nobody collected. That is an unreported day to investigate, not a
zero, and not a double spend.

---

## 6. What this does not become

**Not a file sync.** Profiles, rules, budgets and days move. `$HOME` does not.
omahouse has never had anything to do with a person's files and does not start
here.

**Not a security boundary.** [`design.md` §10](../design.md) survives this page
unchanged. A released terminal still launches anything; the clock, the logout,
the counting and the daemon still hold because none of them depends on the
goodwill of the session.

**Not a change to what omastore may do.** The store reads
`/etc/omahouse/profiles.json`, reimplements the verdict, and never writes —
which is why §2 has a consequence there. **A fallback profile means that "this
account has no profile" stops meaning "this account has no rules."** A reader
that treats a missing profile as freedom will show freedom where there is a rule.
That reader exists today, in another program, and it has to learn the new answer
at the same time.

The other program that has to follow is the Battery. It divides a daily
allowance, and a budget anchored at login is not a daily allowance. **It must
refuse to allocate one** rather than divide it into portions that mean nothing.

---

## 7. The order of the work

Phase B does not depend on A or C. Phase C depends on the reset being decided
first, because a balance that refills at midnight and one that never refills are
different sums to report and to read back.

### Phase A — the profile can say the three shapes

1. **Done.** **Swap the sitting for no reset.** Three pieces go: the anchor, the sitting
   stamped on a grant, and the clearing of warning marks. **The separate map
   stays** — see below, because removing it reintroduces double counting that no
   test would catch. *core, tests*
2. **Done.** **Give `Budget` an explicit reset.** `resets: "daily" | "never"`,
   absent meaning daily — not for the files of a product with no users, but
   because most budgets have no opinion about it and writing the default into
   every one of them is noise in a file people read. *core*
3. **Done.** **Prove that `never` does not refill.** Midnight turns and the daily budget
   goes back to zero while the credit does not; a second login spends the same
   balance the first one left. *tests*
4. **Say what the account is, not only what it opens.** The profile carries
   whether the account is administrative, and omahouse creates it that way, and
   documents that it does not defend it. *core, sys*

### Phase B — the profile arrives from somewhere else

5. **Done.** **A profile
   that names no account**, marked `*`, applying to any account without one of
   its own. `sys` is in this step and the page first missed it: the watch walks
   profiles and asks which account each names, and a profile that names none has
   no way into that loop, so the cycle must also walk **accounts with a live
   session** and ask which have no profile of their own — a capability `Proc` did
   not have. *core, sys, cli, studio*
6. **Done.** **Push from the manager, cache locally.** A cued script writes the profile;
   the local file is the last version this machine was told; a cache past the
   freshness limit opens no new session. *core, sys, cli, omahouse-battery*
7. **Done.** **Written-at and written-by on the profile.** The tiebreak needs it
   and the merge screen needs it, and it is one field for both. Stamped in
   `saveProfiles` and only on the profiles that actually differ, because a stamp
   each verb has to remember is a stamp the next verb will not, and stamping all
   of them turns *who changed a rule* into *who ran a command last*. *core*
8. **Done on the machine's side; the manager's script is open.** **Merge of
   locally created profiles.** The manager lists what a machine has and offers to
   take it in, and it answers **four** things rather than two: `new` (the machine
   has one the manager never issued), `changed` (both have one and the rules
   differ -- the rules and never the stamp, because a pushed copy is restamped
   on arrival and a merge that read the stamp called every machine changed
   forever; found and fixed on 2026-09-09),
   `gone` (the machine says it has none and the manager has one), and
   `not collected`, which is not an answer and is named rather than folded into
   *no differences*. **A collected document with an empty list is the form of "I
   have none"** — no second shape to learn, and it is what tells a deliberate
   removal apart from a machine that has not reported. *core, cli, studio,
   omahouse-battery*
9. **The window says the three shapes without three screens.** The studio has the
   rule and budget editors already; one control changes, not a view. *studio*

### Phase C — the time is one pool

10. **Reverse the written decision.** The household page says `house` is a total
    and not an authorisation. Change the behaviour and the page together, with
    the reason. *docs*
11. **Replace division with asking.** No morning portion. The manager holds the
    balance and a machine reads it. *core, cli*
12. **Superseded.** ~~Report while spending, from the watch loop.~~ The report
    rides the cued script instead, which is the section below and needs nothing
    of the watch loop. Kept struck out rather than deleted, because sending from
    the loop is the obvious idea and somebody will have it again. *nothing*
13. **The Battery changes job.** From dividing once a day to carrying the report
    and the balance at the counter's cadence, through a wrapper that holds the
    schedule. The scripts themselves need no change. *omahouse-battery*
14. **Spend the cache down.** A machine out of contact keeps spending the balance
    it last read, to zero, and reports when it can. No timeout and no ceiling.
    *core, sys*
15. **Half done, and the other half was the wrong instruction.** omastore learned
    the fallback profile. The Battery was told to **refuse** a credit rather than
    divide it, and that was written when the manager handed out portions — with a
    balance there is nothing to divide, and refusing would kill the case this page
    exists for. *omastore*

16. **Done.** **The plan carries a pot.** It used to sum the daily counter, which
    is zero for anything that carries over, so a pot's spending reached nobody
    while its refills reached everybody — two machines each told the whole pot,
    which is worse than having no household and fails towards giving time away.
    Neither of a pot's numbers is a day: what it has spent is the running total,
    and what it has been given is the fold beside it. **Nothing new had to
    travel** — a collected day was already a whole ledger and carried both, and
    the plan simply had to ask. *core, cli*

17. **No verb writes `resets`, so nobody can ask for a pot.** The model does the
    lan house and the interface does not let anyone request it. This is the last
    thing between the code and what §3 promises, and it is a gap in the interface
    rather than in the model. *cli, studio*

**Omakure does not change.** It is the wire, and `collect` already travels on it.
**The browser extension does not change.** A site budget uses the same machinery
as an app budget, so a credit reaches it for free.

---

### The report rides the schedule that exists

The reporting half needs no new mechanism. `omahouse-day.sh` and
`omahouse-collect.sh` are already in the Battery and neither carries a
`Schedule` of its own, deliberately: the schedule lives in a wrapper the
operator configures. So the cadence is a local file and not a change to
anything shipped.

Three things were measured in Omakure on 2026-09-08 and they decide the shape:

- `omakure serve` scans the workspace **every five seconds** and accepts
  six-field cron, so `*/5 * * * * *` is exactly its floor rather than an
  arbitrary choice;
- **a fire is skipped when the previous run of the same schedule is still
  alive**, so a slow report is dropped rather than queued, and nothing piles up
  behind a machine that is struggling;
- what crosses the wire is still whatever the script arranges for itself, since
  Omakure carries the instruction to run and not the result.

That last one is the price, and it is why a fourth Omakure plane was designed on
2026-09-08 and then deliberately not waited for. It would put the report in the
authenticated envelope and take root out of the reporting path. Both are real
and neither blocks anything here, so this page takes the schedule that exists
and treats the plane as an improvement that can arrive later without changing
what omahouse sends.

---

## 8. What was proposed on the way here and refused

Three ideas were argued and dropped. They are recorded because each of them is
what a reader would reach for first.

**A drop-in directory with a precedence.** `/etc/omahouse/profiles.d/*.json`
composed over `profiles.json`, Chromium's shape, with a rule for which file wins
and provenance so a person could see it. **Refused because a single central
source has nothing to merge.** A profile has one authoritative version and a
cache of it. Composition, precedence and per-rule provenance all disappear, and
with them the hardest part of the design. Provenance came back in §5 for a
different and smaller reason: not *which file*, but *who wrote this and when*.

**A ceiling on how much a machine may spend while disconnected.** Argued as
protection against the same hour being spent on three machines at once.
**Refused because the case cannot happen.** Spending twice at the same time
requires logging in twice at the same time, and §4 makes the second login
impossible. The ceiling solved a problem the rule already removes.

**Mandatory and recommended policy, as Chromium has.** Proposed to make a rule
the local root cannot undo. **Refused as the wrong axis.** The hard case is not
a company: it is a laptop whose user administers it, and a lan house is the
*easiest* case there is, because the operator owns the machine and the customer
has no root. Where an administrator is genuinely local and trusted, §5's tiebreak
is the answer and it is simpler.

There is a second argument for the refusal, and it is measured rather than
argued. **The gate that decides who may change a rule already exists and is
per-person, not per-file.** `packaging/org.omarchy.omahouse.rules` makes polkit
ask a given administrator for *their own* password, and deliberately refuses a
blanket `auth_self` — its own comment says why: that action is the only gate in
front of the CLI running as root, because omahouse does not refuse a write from
an account under rules once it is already root. A flag in the policy file would
be a second gate, weaker than the one there, in front of the same door.

---

## What on this page is measured, and what is argued

One more decision belongs with these, and it was forced by a reader in another
program. `profiles.json`, the day's ledger, `machine.json` and `machines.json`
all carried **one** schema version. The effect of one number for four files is
that it never rises: raising it is always too expensive for whatever is at hand,
so the check that exists to refuse a misread document sits there and never fires.
That is how a fallback profile reached omastore in silence — structurally valid,
semantically ignored, and no version to say otherwise. The four now carry their
own numbers and only the profile's rose.

**Measured**, by reading the source and the pages on 2026-09-08: that the profile
lookup is an exact string match — `profile.user == user`, which is a different
question from the rule matching of `selectorMatches`, and only the first is
what §2 changes; that a budget of zero counts without running out;
that the ledger is keyed by date, so a balance that does not
reset has nowhere to live in it; that the household
divides equally, caps absolutely, and does not transfer during the day; that
`collect` replaces a machine's snapshot and `consolidate` is what adds up; that
`collect`'s two guards are monotonicity checks that run only when the stored
snapshot has an `observedAt`, which `omahouse day` stamps only for an enrolled
profile; that omastore reimplements the verdict and
reads the same file; that the polkit rule asks a named administrator for their
own password while refusing a blanket `auth_self`; and that
`/etc/sudoers.d/omahouse-node` names the omahouse binary with no verb after it,
is written by both `machine invite` and `machine prepare`, and is removed by
`packaging/omahouse.install`.

Also measured, on a disposable guest on the same day, and kept because it is what
exposed a wrong model rather than because the model needs it: that `loginctl
show-user -p Timestamp` does not move when the same account logs out and logs in
again, across three sittings, with lingering on and with it off.

Also measured, in Omakure on the same day: that the Conductor opens the session
towards the Performer, that Cue and Baseline are what cross towards a machine,
that Profile, Pulse and Signal are what come back, and that no plane carries a
question from a machine to its manager.

**Argued**, and nothing more: everything in §3 through §5. No line of it has run.
