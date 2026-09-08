# How to hand over more time

**The question:** the time has run out, or is about to, and they should have ten
minutes more — with the game still open. How?

This page is enough on its own. It is one verb, and it is the difference between
an operator and a form.

---

## Before you start

- **A profile exists, with the budget you are about to add to.** A grant against
  a budget nobody wrote is refused, because time added to a counter the daemon
  never looks at would read on the report as though it had been given.
- `kid` is a placeholder for the account's login name. Writing needs root.

---

## 1. More of the day

```bash
sudo omahouse grant kid --session 10m
```

```
kid: +10m of session, from howl. 2h10m left today.
```

## 2. More of one program

```bash
sudo omahouse grant kid --budget chromium=15m
```

The id is the budget's id — the app's scope id, as `omahouse profile show kid`
lists it under `BUDGET`.

## 3. More of one site

A site budget is a budget, and its id is the domain:

```bash
sudo omahouse grant kid --budget youtube.com=10m
```

```
kid: +10m of youtube.com, from howl. 40m left today.
```

If the site had run out and stopped opening, it opens again on the next cycle —
nothing has to remember to unblock it.

---

## What a grant is, exactly

**It goes into the day's own ledger and expires with it.** The balance resets at
the local turn of the date, and a grant that survived it would be tomorrow's
time given away today.

**It adds to the limit rather than replacing it,** and two grants add up:

```
kid: +10m of session, from howl. 2h10m left today.
kid: +5m of session, from howl. 2h15m left today.
```

**It takes effect on the next cycle, which is two seconds.** If the name was in
`/etc/omahouse/blocked` because the day had run out, it comes out on its own —
within two seconds the password gets into the greeter again, without anybody
having to know that file exists.

**It is signed with the name of whoever asked for it.** Through the window that
is the person polkit authenticated. Through `sudo omahouse grant` it is `root`,
and the report a month later will say `root handed over 45m of session`. If the
name matters in your records, grant from the window.

```
GRANTS
AT        BY    BUDGET   ADDED
00:56:52  howl  session   +10m
00:56:52  howl  session    +5m
```

---

## In the window

`+` on a program, a budget or a site. The footer says what happened, in the same
sentence the CLI prints:

![The studio's programs tab once the grant is through: the footer says "julia: +10m of foot, from howl. 15m left today.", and the Foot row reads "15m left of 15m" with "+10m handed over today" under it.](../vm/shots/40-studio-tempo-extra-concedido.png)

*(The pictures from the live machine were taken with the account under rules
called `julia`; the commands here say `kid`.)*

The sheet it opens:

![The "More time today for code" dialogue: a field with a `+` printed to the left of it as a lead mark, empty, and "Enter ok" greyed out.](img/11-operator-more-today.png)

The `+` on the left is drawn by the field and is not in it.

> **A gotcha, and it is a defect.** On a live session the `+` keystroke that
> opened the sheet lands in the field as well, and `ok` is born greyed out.
> Clear the `+` before typing the number.
>
> ![The "More time today for foot" dialogue with a lone `+` in the field and the `Enter ok` button greyed out.](../vm/shots/39-studio-mais-tempo-hoje.png)
>
> [The defect](what-does-not-work.md#5-the-key-that-opens-more-time-today-lands-in-the-field)

**more time today** is missing from the command palette when the row under the
cursor has no clock on it. That is the point of listing only what is usable:
there is nothing to hand more of.

---

## What can go wrong

**There is no such budget.**

```
grant: nobody has no budget called minecraft; omahouse profile show nobody lists them
```

**The budget has no limit.** The grant is written down and says so, because a
budget that never runs out has nothing to be added to.

---

## When it is not one afternoon: loosening the rules for good

A grant is for today. These change the rule:

```bash
sudo omahouse limit kid --session 4h            # a longer day, every day
sudo omahouse profile enforce kid --off         # counts and reports, closes nothing
sudo omahouse profile default kid --allow       # everything runs but what is denied
```

`enforce --off` is the observing mode a profile is born in, and it is where to
go back to when something is biting too hard and you do not yet know what. It
also lets a site that had run out open again, for the same reason a grant does:
the block is re-derived every cycle from what is true right now.

The numbers themselves are [how to put a clock on the day](how-to-limit-the-time.md).

---

## Next

- [Reading the day](how-to-read-the-day.md) — where the grants show up, and what
  the day went on.
- [How to put a clock on the day](how-to-limit-the-time.md) — the limits a grant
  is temporary relief from.
