# How to read the day

**The question:** where did the afternoon go, what is left right now, and why
did that program close?

This page is enough on its own. **None of it needs privilege** — the person
under rules runs the same commands about themselves and sees the same numbers.

---

## Before you start

- **A profile exists for the account**, and the daemon has been running.
  [How to put an account under rules](how-to-put-an-account-under-rules.md).
- `kid` is a placeholder for the account's login name.

Two files hold everything below: `/etc/omahouse/profiles.json`, which is the
rules, and `/var/lib/omahouse/<user>/<YYYY-MM-DD>.json`, which is one day. Both
are world readable. Only the daemon writes the second, and it writes it
atomically, so a reader sees the whole of the last cycle or the whole of the one
before.

---

## 1. What is happening right now

```bash
omahouse status kid
```

```
omahouse status — nobody (Kid)
observing, default allow, 2026-09-05 · ledger on disk

BUDGET       LIMIT   USED   LEFT  WHEN OUT
chromium       45m    45m     0m  closes
code           45m     0m    45m  closes
session      2h10m  1h10m  1h00m  logs out
youtube.com    30m    25m     5m  stops opening
```

`LIMIT` already has today's grants in it. `WHEN OUT` is the whole vocabulary of
what running out can do: an app **closes**, the session **logs out**, a site
**stops opening**, and a budget with nothing behind it only warns.

With no argument it is about whoever ran it, by real uid rather than by `$USER`.
On an account with no profile it runs as a plain scan of the live app scopes,
which is the mode that answers *what would omahouse see here* on a machine
nobody has configured.

Four more blocks come with it, and each is there because something would
otherwise be invisible:

- **the app scopes**, with the verdict each would get — and, under `Not what the
  name says`, the ones whose id is a launcher with something else inside it.
- **`Counted, not named`** — a scope with processes in it and no id anything can
  match. It is debited by every budget whose selector is `*`, because somebody
  in a terminal all afternoon is somebody using the machine, and it can carry no
  rule and no limit of its own.
- **`Out of reach`** — processes in `session.slice`, started outside `uwsm app`.
  Not counted and not closeable, because taking that cgroup would take the
  session with it. **This number is never zero on a live session**, and it is
  printed plainly rather than as an alarm.
- **`Presence`** and **`TIME PER SITE`**, below.

## 2. Whether anybody was actually there

```
Presence
  nobody is away: not logged in.
  Screen on, seat showing uid 1000 — read from the kernel's DRM connectors and from
  logind, never from nobody's own compositor.
  Today: 40m screen-off, 1h10m using.
  Measured and reported, and it takes nothing away: an app is still billed for
  running, screen or no screen (docs/design.md §5).
```

Presence is two facts read every cycle: whether any connected screen is lit,
from the kernel's `/sys/class/drm/*/dpms`, and which session the seat is
showing, from root's own `loginctl`. It is never asked of the fiscalised
account's own compositor, because that socket lives in a directory that account
owns.

**It is measured and reported, and it acts on nothing** — with one exception,
which is the time per site. An app is billed for running, screen or no screen.

## 3. Which sites, and for how long

```
TIME PER SITE
  Only nobody and root can see which tab is in front right now: the browser
  writes it in nobody's own runtime directory. The day below is from the ledger.
  Today: 25m youtube.com, 8m wikipedia.org.
```

Asked about your own account, the first line is instead the live one — which
site is in the front tab, and whether it is being counted. **A site is counted
only while presence says somebody is in front of the screen**, and `status`
prints `is NOT being counted` in so many words when the two disagree. That is
why an afternoon can add up to less than it felt like.

It needs the browser meter, which arrives with the package.
[Minutes a day on a site](how-to-limit-time-on-a-site.md) is the whole of it.

## 4. The day, once it is over

```bash
omahouse report kid
omahouse report kid --since 2026-09-01
```

```
omahouse report — nobody

2026-09-05
BUDGET        USED
chromium       45m
session      1h10m
youtube.com    25m

PRESENCE
STATE         FOR
screen-off    40m
using       1h10m

TIME PER SITE
SITE           FOR
wikipedia.org   8m
youtube.com    25m

GRANTS
AT        BY    BUDGET   ADDED
09:12:00  howl  session   +10m

EVENTS
AT        KIND       BUDGET    WHAT
09:50:00  denied     —         not allowed: app-Hyprland-gtk\x2dlaunch-c5075ae6.scope
09:40:00  exhausted  chromium  ran out
09:35:00  warn       chromium  5 minutes left
```

A day with no file is a day nobody spent, and it is left out of a range rather
than printed as a row of zeroes. `--since` adds a total across the days it
found. The sites are printed **beside** the budgets and never among them: a site
is not a budget, and adding `youtube.com` to a sum of budgets would be a report
saying the day was twice as long as it was.

`report` does not ask whether the account still exists. A profile, and the days
it accumulated, can outlive the account they were written for, and a report is
exactly what somebody would want in that case.

**The events are not decoration.** They are where a decision that can only
happen once remembers that it already has: a `warn` carries the mark that fired,
which is what stops the same warning going out every two seconds, and an
`exhausted` carries the instant that opens the grace window, read from disk
rather than from a counter in memory, so a daemon restarted mid-window resumes
it instead of reopening it. Reading them is reading **why** a warning went out,
or why it did not.

## 5. For a script

`omahouse --json status kid`, and the same for `report`, `profile list` and
`profile show`. One document on stdout and nothing else; a note about a file
that is not there still goes to stderr, so a pipe gets JSON and a person still
gets told.

A budget with no limit has `limitSeconds` and `leftSeconds` null rather than
zero: zero left is a budget that has run out, and the two must never read the
same.

---

## In the window

The **today** view is `3`. The session budget first, because it is what somebody
opened this window for; then a budget per program; then the day's log, newest
first, as one list so that `j` and `k` walk the whole of it:

![The today view: "the whole day · session · running now", "1h10m spent · ends the session when it runs out", "1h left of 2h10m" and "+10m handed over today"; then Code and Firefox; then four log lines newest first, the 09:50 one in red.](img/03-operator-today.png)

A site budget is a budget and is on this list too, with *stops opening when it
runs out*, which is the fourth thing running out can do.

The same view in the fiscalised person's own session, which is the same window
with nothing to press:

![The today view on the subject face: the whole day, the three program budgets, the site budget, and the day's log.](img/26-subject-today.png)

And on the live machine, with a real day behind it:

![julia's studio on the today tab: at the top "the whole day · session · running now", "12m spent", "52m left of 1h5m" and "+55m handed over today"; below it Chromium and org.chromium.Chromium, both "3m spent · 0m left of 3m"; and the day's list newest first, down to a red line at 08:32 reading "app-Hyprland-gtk\x2dlaunch-c5075ae6.scope: not on the list, and was closed".](../vm/shots/24-studio-julia-hoje.png)

*(The pictures from the live machine were taken with the account under rules
called `julia`; the commands here say `kid`.)*

The **sites** view is `4`, and it is the one that answers *where did the half
hour go* — the rule, the clock, and the screen-off time that explains why the
number is smaller than the afternoon felt.

---

## What the person under rules sees

**There is no indicator on the bar.** Omarchy's `quickshell` stays as it came.
What arrives is notifications — the warnings, the grace, the refusals — and what
can be looked up is `omahouse status` or the window, which for them is read
only:

![julia's studio, programs tab: xdg-terminal-exec open now with a red warning that the name is not what is running, House Rules · omahouse, Chromium with "0m left of 3m" and the red bar full, and udiskie with the same red warning.](../vm/shots/23-studio-julia-programas.png)

The header says what they are: `julia · subject · under rules · writes through
pkexec`.

---

## What can go wrong

**`/` in the window filters all four lists at once.** A needle that misses the
person on the people list empties the people list, and then the programs view
has nobody to be about.
[The defect](what-does-not-work.md#4-the-filter-narrows-every-list-at-once).

**There is a number the window computes and never shows** — the processes in
scopes nothing can name. `omahouse status` is where to read it.
[The defect](what-does-not-work.md#6-a-scope-nothing-can-name-has-no-screen-in-the-window).

---

## Next

- [How to hand over more time](how-to-hand-over-more-time.md) — once you have
  read what the day cost.
- [How to put a clock on the day](how-to-limit-the-time.md) — once you have read
  a day of it with no teeth in.
- [Every screen the window draws](screens.md).
