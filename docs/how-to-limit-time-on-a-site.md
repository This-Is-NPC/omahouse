# How to give a site so many minutes a day

**The question:** thirty minutes of YouTube a day, not none. How do I write
that, what makes the number honest, and what happens when it runs out?

This page is enough on its own. Stopping a site from opening at all is
[the other page](how-to-block-sites.md).

---

## Before you start

- **A profile exists for the account.**
  [How to put an account under rules](how-to-put-an-account-under-rules.md).
- **omahouse was installed as a package.** The browser meter — the extension
  that reports which site is in the front tab — is signed and forced into
  Chromium by the install scriptlet. A build straight from a checkout has no
  meter, and with no meter no site is ever reported and no site budget is ever
  spent. [How to install it](how-to-install-and-remove.md).
- **The browser is Chromium**, and it is the *only* browser this account can
  open. The meter is a Chromium extension: it reports nothing from Firefox and
  nothing from anything else, so a second browser on the allowlist is an hour a
  day that no site budget will ever see. Leaving it off the allowlist is what
  makes the number mean anything — [how to release programs](how-to-release-programs.md).
- `kid` is a placeholder for the account's login name. Writing needs root.

**One thing to know before writing the number:** the browser policy is one file
for the whole machine, so a site that has run out **stops opening for every
account on this machine, yours included**, until the day turns. omahouse says so
when you write the limit. [`design.md` §11](design.md) is why that was accepted.

---

## 1. Write the limit

```bash
sudo omahouse limit kid --site youtube.com=30m
```

```
kid: youtube.com 30m a day, and it stops opening when the time is out.
omahouse: the browser policy is one file for the whole machine. A site blocked here is
          blocked for everyone who opens Chromium on it, including you. Chromium has no
          per-account policy on Linux (docs/design.md §3.1), and that was accepted.
```

**It is the same noun as `--budget` with a domain where a scope id would be.**
The warning marks, the grace window, the notification, the grant and the
ledger's memory of what has already been said are the ones an app budget already
had, unchanged. Two things differ, and they are the two ends: what spends it,
and what running out does.

A bare domain covers its subdomains, and a whole address is refused — the same
refusal `web block` makes, by the same routine, so that a site cannot be named
one way in the budgets and another way in the report. An id that already names a
budget of the other kind is refused rather than converted: `org.freedesktop.Platform`
is a scope id with dots in it, and one id meaning both would be two rows of the
report that are the same row.

## 2. Know what spends it

Three parts, and the split is the point — **the extension is an eye, the engine
is the brain**:

- **the extension**, in Chromium, reports the *registrable domain* of the active
  tab of the focused window. `youtube.com`, never `youtube.com/watch?v=…`. The
  URL never crosses the wire, so there is no bug and no file by which the page
  somebody was on could be read out of omahouse.
- **`omahouse meter`**, the native messaging host, runs as the person being
  measured and appends `<epoch> <site>` to a file in their own runtime
  directory. It holds no state and decides nothing.
- **`omahouse watch`**, as root, reads that file every cycle, crosses it with
  whether anybody is in front of the screen, and debits the tick.

**A second is billed only where the browser and the screen agree.** That
crossing is what makes the number honest, and it is there because of one
measurement: a spike put an extension on a real Omarchy and asked `chrome.idle`
ninety-four times through thirty minutes of an empty room. It answered `active`
every single time — including the last twenty-five minutes **with the monitor
physically off**. A browser is a reliable witness to *what* is on the screen and
a proven liar about *whether anybody is looking at it*.

So presence is asked of the machine instead: the kernel's
`/sys/class/drm/<connector>/dpms` for the screen, and root's own `loginctl` for
which session the seat is showing. A tab left on YouTube overnight adds nothing,
and `status` says `is NOT being counted` in so many words when the two disagree.

It was measured end to end on real Omarchy, against a wall clock:

| what was on the screen | what the report said |
|---|---|
| `example.com`, 22s | **22s** |
| `en.wikipedia.org`, 22s | **22s** — as `wikipedia.org` |
| `archlinux.org`, lit, 22s + 12s | **34s** |
| `archlinux.org`, screen off, 22s | **0s** |

The twenty-two dark seconds are in the day's `presence` and in no site. The
app budgets gained the whole time either way, screen or no screen — an app is
billed for running, and that did not change.

## 3. Know what running out does

The domain goes into the browser's blocklist for the rest of the day. It warns
first, at the same marks, and waits out the same grace:

```
grace: Time is up — example.com stops opening in 3 seconds.
```

**And it comes back on its own.** Nothing remembers a blocked site: every cycle
works out from today's ledger which sites are out of time *right now* and makes
the policy file say exactly that. So the turn of the local date lets the site
open again, and so does a grant, and so does `profile enforce --off`, and so
does removing the profile — with none of those verbs knowing that the browser's
policy file exists.

Measured on real Omarchy, on a one minute budget with fifty seconds already
spent:

| | |
|---|---|
| the domain reached the blocklist | 12s after the daemon started counting |
| Chromium stopped opening it | 10s after the policy file changed |
| the grant, and the file taken off the machine | on the next cycle |
| Chromium opened it again | 9s after that |
| the app budgets over the whole of it | untouched |

Those ten seconds are Chromium's own file watcher noticing a changed managed
policy, not omahouse.

## 4. Hand over more of it

A site budget is a budget, and it is granted by the name it has — its domain:

```bash
sudo omahouse grant kid --budget youtube.com=10m
```

```
kid: +10m of youtube.com, from howl. 40m left today.
```

The minutes go into the day's own ledger and expire with it, and the site opens
again on the next cycle. The whole verb is
[how to hand over more time](how-to-hand-over-more-time.md).

## 5. Read it back

`omahouse status kid` carries a `TIME PER SITE` block with what is in front
right now and whether it is being counted, and `omahouse report kid` carries
`TIME PER SITE` for a day and `TOTAL PER SITE` over a range. A site with a
budget on it appears twice, and the two rows are two different things: the site
table is every domain that was ever in front, budget or no budget, and the
budget table is what a limit has been spent.

An operator asking about somebody else sees the totals and not the live tab —
the ledger is world readable and the browser's file is 0600 in that account's
own runtime directory — and that asymmetry is deliberate.

---

## In the window

Before adding a rule or setting a limit, choose the profile in **Who is this
for?** Click the profile or filter its name and press Enter. This choice is
required even with one profile; Escape cancels. The following form names the
chosen account, and an automatic list refresh cannot change the recipient.

`m` on the **sites** view (`4`) is minutes a day on the row under the cursor:

![The "Minutes a day on youtube.com" dialogue, pre-filled with 30m selected: "30m, 1h. Counted only while somebody is in front of the screen, and the site stops opening once it is spent — until the turn of the day, or until more time is handed over."](img/18-operator-site-minutes.png)

Two things are said there that are not said about a program: the minutes are
crossed with presence, and what running out does is stop the site opening rather
than close anything.

The view itself carries a line above the list that is about the whole day and
not about any row — the presence, because it is what explains a number smaller
than the afternoon felt:

![The sites view: a line reading "Minutes here are counted only while somebody is in front of the screen. Today: 40m screen-off, 1h10m using.", under it the reach of a browser policy, then three rows — tiktok.com "does not open"; youtube.com "stops opening when the time is up" with "5m left of 30m"; wikipedia.org "no rule and no clock — the minutes are counted and nothing else" with "8m today".](img/15-operator-sites.png)

The third row is why the view is worth opening on a profile with no web rules at
all: it is the afternoon's browsing, counted whether or not anybody ever wrote a
rule about it.

A site budget is a budget, so it is also on the **today** view (`3`), where `m`
changes it and `+` hands over more of it. The verb underneath is `limit --site`
and not `limit --budget`: the window carries which namespace an id is in rather
than inferring it from the shape of the string.

---

## What can go wrong

**Nothing is reported at all.** Either no browser with the meter in it is open,
or nobody has it installed — `status` says which. A second browser is a door
with no clock on it, and browsing done in it is browsing that no site budget can
account for.

**The person can stop the reporting, and it wins them nothing but anonymity.**
The file the host writes belongs to them: they can truncate it, delete it, fill
it with rubbish or kill the host. The daemon treats it as untrusted input — no
symlinks, a regular file owned by that account, a bounded tail, and only the
last complete line — and every way it can be wrong is one answer, which is that
nothing is billed for that tick. What that buys is a short per-site table and a
session that ends at exactly the same minute: the total time on the machine is
held by the cgroup walk and by the PAM line, and neither is reachable from that
file.

**The number is measured only where the screen is lit**, and the screen going
dark is coarser than *this person is reading this page*. It is a fair number
about a browser tab and not a measurement of attention.

---

## Next

- [How to stop a site opening](how-to-block-sites.md) — the rule rather than the
  clock.
- [Reading the day](how-to-read-the-day.md) — the tables above, in full.
- [How it is built](design.md) §5.2 and §5.3 — the mechanism, and why the file
  and not a socket.
