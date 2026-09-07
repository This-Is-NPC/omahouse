# How to link another computer

**The question:** the household has a second computer, and the rules should be
one household's rules rather than two machines' rules. How do I put omahouse on
it, link it here, and read one day across both?

This page is enough on its own. It ends with two computers under one profile:
each enforcing on its own, and this one adding their days up and pushing the
truth back.

---

## Before you start

- **omahouse is installed here.**
  [How to install it and take it off again](how-to-install-and-remove.md).
- **The other computer runs Omarchy and you have ssh to it.** That is the whole
  of what is needed there — no omahouse yet, no omakure, nothing installed by
  hand. `ssh arch@192.168.1.20 true` answering is the check.
- **You can become root on both.** On the far side that is your own sudo,
  through the same ssh.
- **You know this computer's address on the household network.** `ip -4 addr`
  shows it. `192.168.1.10` and `192.168.1.20` throughout this page are
  placeholders.

---

## 1. Link it

One command, run here:

```bash
sudo omahouse machine link arch@192.168.1.20 \
     --name "the kitchen laptop" --at 192.168.1.10:7879
```

```
This machine now has an Omakure identity of its own: omk1_24d5c376f4e….
the kitchen laptop: paired. This machine trusts it as a performer, and knows to
find it at 192.168.1.20:7879.

the kitchen laptop is linked. From here you can put an account on it under rules,
and `omahouse house <user>` will add its day to this one's.

If this computer ever cannot reach it, log in there as root: everything
omahouse does works on that machine on its own.
```

`--name` is the household's word for that computer — "the kitchen laptop", not a
hostname. It is the handle every other verb takes, and it is kept apart from the
identity underneath on purpose: renaming a computer must not break its trust.

`--at` is where the **other** computers reach **this** one. It is asked for
because a machine cannot know which of its addresses the household will use, and
a wrong one links fine and then does nothing.

**What that one command did**, in case you ever have to do it by hand:

| on | what |
|---|---|
| here | became the household's manager, made an identity, opened nothing |
| there | installed omahouse — which brings omakure and the browser's meter with it |
| there | made its own identity, trusted this one, opened its console to the household |
| here | trusted it back, wrote it into the list, and kept the key to read it |

It installs omahouse there **only if it is not already there**. Re-linking a
computer — an address changed, a manager was rebuilt — is an ordinary thing to
do and must not reinstall the package underneath somebody.

---

## 2. Ask each computer what it is

```bash
omahouse machine kind
```

```
the study
  the household's console: it holds the list of computers and adds up their days.
```

and on the other one, through the same ssh:

```bash
ssh arch@192.168.1.20 omahouse machine kind
```

```
the kitchen laptop
  managed from another computer, and still enforcing its own rules on its own.
  its manager is omk1_24d5c376f4e…
```

**"Still enforcing its own rules on its own" is the sentence that matters.** The
far computer has the whole of omahouse and not an agent. If this one is off, or
the network is down, or you sell it — the rules there still count, still warn and
still close. And you can sit at that computer as root and change any of it.

A computer that has never been linked says so too, and it is not a lesser state:

```
this machine
  on its own, and nothing is missing.

  Rules, budgets and the browser all work here exactly as they are.
  Linking is what a second computer needs, not something this one is missing.
```

---

## 3. Put the same person under rules on both

The profile is per machine, because the rules are the machine's and a manager
that held them would be a manager whose absence is a machine with no rules. So
write it in both places — [how to put an account under
rules](how-to-put-an-account-under-rules.md) is the whole of it, and here it is
twice:

```bash
sudo omahouse profile add kid --name "Kid"
sudo omahouse limit kid --session 2h

ssh arch@192.168.1.20 sudo omahouse profile add kid --name "Kid"
ssh arch@192.168.1.20 sudo omahouse limit kid --session 2h
```

**`2h` is two hours in the household, not two hours per computer.** That is the
whole reason the next step exists.

---

## 4. Read the day across both

```bash
omahouse house kid
```

```
the house — kid, 2026-09-06

BUDGET    HERE  THE KITCHEN LAPTOP  IN ALL     OF   LEFT
session    30m                 40m   1h10m  2h00m    50m
```

The days of the other computers are collected into
`/var/lib/omahouse/elsewhere/`, and something has to put them there. On a
household that has run the Battery's scheduled job, they are already in. To
fetch one by hand, right now:

```bash
ssh arch@192.168.1.20 omahouse day kid | sudo omahouse collect "the kitchen laptop" kid
```

```
the kitchen laptop: kid's 2026-09-06 is in, and counts towards the house.
```

That pipe is the whole of the transport, and it is deliberately something you
can type. A day is only accepted for a computer that is in the list, and the
document names its own person and its own date — both are checked rather than
trusted, because a day filed under the wrong name is time added to somebody who
did not spend it and nothing downstream would notice.

**A computer that has not reported is not in the sum, and the answer says which:**

```
  Nothing today from the workstation, so it is not in the sum above.
```

A total quietly missing a computer reads exactly like a total of a quiet
afternoon, and only one of those is a fact.

---

## 5. Share the allowance safely

Collection alone does not prevent both machines spending the same daily limit.
For automatic sharing, follow [the Battery schedule setup](how-to-schedule-household.md).
It enrolls each computer, reserves exclusive portions and delivers absolute daily
caps. Omakure runs the schedule; omahouse decides and enforces the credit.

`leave` remains a standalone local adjustment. It must not be repeated as a
synchronization loop: consumption between calls would be refilled. New local
adjustments are excluded from household credit, and enrolled profiles refuse
`leave` altogether.

---

## In the window

Everything above is typed, and there is a screen for it. Open the studio, pick
the account on **people**, and press **f**:

![The machines view: `MACHINE / BUDGET` beside `USED / PORTION / LEFT`; `here / session` at 1h10m / 1h15m / 5m, marked `portion received` and `Last report: local`; and `station-02 / session` at 40m / 45m / 5m, marked `stale report; portion reserved` and `Last report: 2026-09-01T09:30:00`.](img/31-operator-machines.png)

One row is one budget on one computer. What that computer last reported
spending, the daily portion it was given, what is left of it, and when the
report came in.

`here` reports `local` and can never go stale. Every other row carries the
instant its report came in, and **that line is the one to read first.** A
portion stays reserved whether or not the computer is reporting, so `5m left`
beside a week-old observation is five minutes that may already have been spent
over there. The row says `stale report; portion reserved` in those words rather
than leaving somebody to work it out from the date.

**+** records household credit, through the same privileged CLI a terminal
would use. It does not deliver it: the next successful Battery cycle divides it
among the portions. Nothing in this window runs a scheduler, and nothing in it
reaches the other computer — it reads what was collected here.

The fiscalised person's own face cannot open this view.

---

## What this does not do

- **It does not create the account on the far computer.** `kid` has to exist
  there. `ssh arch@192.168.1.20 sudo useradd -m kid` is the whole of it, and
  omahouse deliberately does not do it for you: creating accounts on somebody
  else's computer from here is not a thing to do by accident.
- **It does not move the profile.** Two computers, two `profiles.json`. See
  step 3.
- **It does not schedule the collecting.** Enable the explicit
  [Battery schedule](how-to-schedule-household.md) for collection, reservation
  and delivery. Linking alone activates none of those jobs.

---

## Taking it back

```bash
sudo omahouse machine remove "the kitchen laptop"
```

```
the kitchen laptop: out of the house's list. Nothing on that machine changed —
its rules and its daemon are still running.
```

Read that second sentence. **Forgetting a computer does nothing to it.** Its
profile, its budgets, its daemon and its day are all exactly where they were,
and somebody sitting at it will not notice. To take the rules off, do it there:

```bash
ssh arch@192.168.1.20 sudo omahouse profile remove kid
```

and to take omahouse off entirely, [how to install it and take it off
again](how-to-install-and-remove.md) applies there exactly as it does here.
