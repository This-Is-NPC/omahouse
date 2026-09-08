# Profiles across a network — designed, and mostly not built

> **Almost nothing on this page is built, and the exception is named here.**
> Steps 1 to 3 of §7 shipped on 2026-09-08: a budget can be anchored at the
> login, the ledger carries the sitting, and the engine honours both. **Nothing
> reads logind yet and no verb writes `resets`**, so a profile cannot ask for one
> from the command line or the window — see [`design.md`](../design.md), which
> says so in its own words. Everything else below is a record of a design decided
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

Three examples decide the model, and they differ in one thing only: **when the
clock resets.**

| the shape | resets | exists today |
|---|---|---|
| five hours a day | at the turn of the local date | yes |
| unlimited, everything installed, counted | never | yes |
| two hours from the moment of login | at login | **no** |

The first is a session budget with `dailyMinutes: 300`. The second is
`default: allow` with a budget whose limit is zero, which `Profile.h` already
describes: *"Zero or less is a budget with no limit: it counts, and it never
runs out."* Both are expressible in the file that exists.

The third is not, and it is the only new arithmetic on this page. The ledger is
`/var/lib/omahouse/<user>/<YYYY-MM-DD>.json` and the balance turns with the local
date ([`design.md` §4](../design.md)). Two hours from login is a second anchor,
not a second engine.

**`Budget` gains `resets`, and an absent `resets` means `daily`.** Every file on
disk keeps the meaning it has. That is the discipline §4 already applies to
`kind`, `presence` and `sites`: a file that already says what it means is not
rewritten to say it differently.

The ledger gains the sitting, on disk rather than in memory, for the same reason
the `exhausted` event carries its instant — a daemon restarted in the middle of
somebody's two hours has to resume them and not reopen them.

**The sitting is an opaque word, not an instant, and that was measured rather
than argued.** This page first asked for the login instant and the seconds since
it. `loginctl show-user <uid> -p Timestamp` answers with a weekday name and a
locale, and `TimestampMonotonic` counts from a boot the ledger outlives. Neither
needs parsing, because the question is not *when*: the engine only needs to know
whether this is still the same sitting, which is an identity. The word is
compared by equality and nothing else — no parsing, no locale, no clock drift.

The seconds do not come from subtracting two clocks either. They come from the
ticks, which is what has always counted time here, and it is the right number: a
budget counts an app running and not wall time.

**A sitting crosses midnights, and that is ordinary rather than an edge.** The
session of the machine this was measured on began three days before it was
measured. So the session-anchored seconds live in a map of their own, beside the
daily one and never inside it: the running total is carried into the file of
every day the sitting touches, and everything that adds days or machines —
`report`, `house`, `consolidate`, `collect` — reads the daily map. Sharing one
map would count a sitting's total once per midnight crossed. Kept apart, every
one of those readers stays correct without knowing this exists, which is how §5's
invariant survives a second clock.

### Where the sitting has to come from, measured

The obvious source is wrong, and it was measured rather than reasoned about.

**`loginctl show-user <name> -p Timestamp` does not move between sittings.** On a
disposable guest, the same account's graphical session was ended twice and left
to log in again — sessions `2`, then `265`, then `268`. The user's timestamp
answered `Mon 2026-09-07 19:16:16 UTC` all three times, with the same monotonic
value. **Three sittings, one anchor**, and the second and third would inherit the
first one's clock, which is the failure the anchor exists to prevent and in the
direction that hands out free time.

**It does not depend on lingering.** Lingering was turned off between the second
and the third and the timestamp still did not move, because the account's
`manager` session stays alive either way.

What moves is `show-user -p Display`, which names the graphical session and went
`2` → `265` → `268`, and `show-session` on that id answers real, different
timestamps. So the sitting comes from the session, never from the user.

**And the session id alone is not enough, because the ledger outlives the boot
and the id does not.** logind keeps its session bookkeeping in
`/run/systemd/sessions`, which is `tmpfs` — measured on two machines — so the
state the counter is allocated from cannot survive a restart by construction. A
machine switched off at night hands session `1` to somebody who would inherit
session `1` of yesterday. That is not an unlikely collision inside one second; it
is every machine that gets turned off.

`/proc/sys/kernel/random/boot_id` is `0444` and unique per boot. **The boot id
with the graphical session's id is an opaque word with no collision available to
it**: unique per boot by construction, and unique within the boot by the counter.
It is still compared only by equality, and it still never reaches `evaluate`,
which takes the anchor and does not know where it came from.

That also buys a property nobody asked for. A sitting cannot be confused with one
that was open before a restart, so whatever the ledger carried into today's file
is discarded on the first tick after the boot — which is the right answer, because
the machine went off and the person got up.

### Two things the second anchor drags along

**A grant belongs to a sitting.** Grants live in the day's file and the day
outlives the sitting, so counting them all would hand the next person to log in
the ten minutes somebody else was given. The grant carries the sitting it was
made in. The line stays in the file either way, because it is the day's log and
it did happen; the stamp decides only whether it still counts.

**A new sitting forgets the warnings.** A session budget that ran out at ten has
already written that it gave its last warning. Without clearing that, the person
who sits down at eleven is cut off without hearing anything. A refusal that names
a scope rather than a budget stays, because that one belongs to the day.

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

**The pool is the person's, and any machine may serve from it.** An hour is an
hour wherever they sit. Forty minutes at one computer leaves twenty at the next.

A machine does not receive a portion in the morning. It borrows a short piece
when somebody sits down and renews it while they use it. **A machine that dies
loses one unrenewed loan, not the session**, which is the case that decides the
shape: the computer holding the record of forty spent minutes is the computer
that broke.

The cadence already exists. Allocation today refuses to plan on any observation
older than 120 seconds, so machines already report at that rate.

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

### The cache has a freshness limit, and it is not the double-spend defence

A cache older than the household's limit does not open a new session. That bounds
how stale a **policy** may be before a machine stops acting on it.

**It does not bound double spending, and the earlier draft leaned on it as though
it did.** With the network up, two machines both holding a fresh cache would each
let the same person in, and each would spend. The login was never the gate.

**The loan is the gate.** Phase C's short borrow, taken when somebody sits down
and renewed while they use the machine, is the only thing that stops the same
minutes being spent twice, because it is the only thing that talks to the pool
while the spending happens.

That gives the honest shape of a machine that cannot reach the manager: the cache
may still say who the person is and what they may open, and no loan can be taken,
so there is no time to spend. Whether that reads to the person as *refused at the
login screen* or as *logged in with nothing left* is a product choice and is not
made here.

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

### The tiebreak

**The most recently written document wins.** An administrator who edited the
cache while the central omahouse was down beats the central copy, because their
edit is newer.

This requires a field the profile does not have today. `schemaVersion` exists;
a written-at and a written-by do not. Without them there is no "most recent" —
there are only two different files.

That field also does the second job: it is how the central omahouse tells a
profile it issued from one an administrator created on a machine, which is what
the merge screen has to show.

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

Phase B does not depend on A or C. Phase C depends on step 1, because lending
from a pool against a daily clock and against a session clock are different sums.

### Phase A — the profile can say the three shapes

1. **Give `Budget` an explicit reset.** `resets: "daily" | "session"`, absent
   meaning daily, so every file on disk keeps its current meaning. The reader
   already holds this discipline: `wantsNames` in `src/core/Profile.cpp` reads
   `match` written either way and a case pins it. *core*
2. **Record the sitting in the ledger.** An opaque word compared by equality,
   plus the seconds the ticks counted, in a map of their own so that nothing
   which adds days or machines has to know. *core*
3. **Prove the two clocks disagree.** Midnight turning inside a sitting: the
   daily budget resets, the session budget does not, one tick spends both. And an
   empty anchor means *nobody was asked*, never *nobody is sitting there*. *tests*
4. **Say what the account is, not only what it opens.** The profile carries
   whether the account is administrative, and omahouse creates it that way, and
   documents that it does not defend it. *core, sys*

### Phase B — the profile arrives from somewhere else

5. **A profile that names no account.** A fallback applying to any account
   without one of its own, so a machine can carry rules for whoever sits at it.
   *core, cli, studio*
6. **Push from the manager, cache locally.** A cued script writes the profile;
   the local file is the last version this machine was told; a cache past the
   freshness limit opens no new session. *core, sys, cli, omahouse-battery*
7. **Written-at and written-by on the profile.** The tiebreak needs it and the
   merge screen needs it, and it is one field for both. *core*
8. **Merge of locally created profiles.** The central omahouse lists what an
   administrator created on a machine and offers to take it in. *core, cli, studio*
9. **The window says the three shapes without three screens.** The studio has the
   rule and budget editors already; one control changes, not a view. *studio*

### Phase C — the time is one pool

10. **Reverse the written decision.** The household page says `house` is a total
    and not an authorisation. Change the behaviour and the page together, with
    the reason. *docs*
11. **Replace division with lending.** No morning portion. A short loan when
    somebody sits down, renewed while they use it. *core, cli*
12. **Renew while spending.** The watch loop that already debits per tick renews
    in the same pass; a refused renewal is the end of time, down the path that
    already handles an exhausted budget. *sys*
13. **The Battery changes job.** From dividing once a day to serving requests:
    grant, renew, expire, return. The largest conceptual change outside omahouse.
    *omahouse-battery*
14. **The downstream readers follow.** omastore learns the fallback profile; the
    Battery refuses a session-anchored budget instead of dividing it wrongly.
    *omastore, omahouse-battery*

**Omakure does not change.** It is the wire, and `collect` already travels on it.
**The browser extension does not change.** A site budget uses the same machinery
as an app budget, so the anchor reaches it for free.

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

**Measured**, by reading the source and the pages on 2026-09-08: that the profile
lookup is an exact string match — `profile.user == user`, which is a different
question from the rule matching of `selectorMatches`, and only the first is
what §2 changes; that a budget of zero counts without running out;
that the ledger is keyed by date with no session anchor; that the household
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

Also measured, on this machine and on a disposable guest on the same day: that
`loginctl show-user -p Timestamp` answers with a weekday name and a locale, that
its monotonic form counts from a boot the ledger outlives, and that it does not
move across three sittings with lingering on or off; that `show-user -p Display`
does move and `show-session` on that id answers different timestamps; that
`/run/systemd/sessions` is `tmpfs` and `/proc/sys/kernel/random/boot_id` is
`0444`; and that this machine's own session had been open for three days, which
is why a sitting crossing midnight is ordinary.

**Inferred, and worth marking as such:** that session ids number from low again
after a restart. Nobody rebooted a machine to watch it. The evidence is the tmpfs
and two machines whose numbers run the wrong way for a lifetime counter — the one
up for three days holds `1` and `2`, the guest holds `265` and `268` — and that
evidence is strong without being a measurement.

Also measured, in Omakure on the same day: that the Conductor opens the session
towards the Performer, that Cue and Baseline are what cross towards a machine,
that Profile, Pulse and Signal are what come back, and that no plane carries a
question from a machine to its manager.

**Argued**, and nothing more: everything in §3 through §5. No line of it has run.
