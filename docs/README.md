# The omahouse documentation

Every page in this directory, in the order somebody meets them, and what each
one is for. [The front door](../README.md) says what omahouse is and how to get
it; this page says which document answers which question.

---

## Start here

If omahouse is not on the machine yet, read them in this order. Each page is
written to be followed on its own, so skipping one costs nothing but the thing
it was about.

1. [**How to install omahouse, and how to take it off again**](how-to-install-and-remove.md)
   — from a checkout, or as a package; what it puts on the machine, and what
   `pacman -R` takes back.
2. [**How to put an account under rules**](how-to-put-an-account-under-rules.md)
   — writing a profile, what it is born as, and the one account it refuses.
3. [**How to say which programs may run**](how-to-release-programs.md) — finding
   out what programs are called, and the launcher trap that decides the shape of
   the list.
4. [**How to put a clock on the day**](how-to-limit-the-time.md) — minutes a
   day per program and for the whole session, and switching the teeth on.
5. [**How to read the day**](how-to-read-the-day.md) — what is left right now,
   where the afternoon went, and why a warning fired.
6. [**How to hand over more time**](how-to-hand-over-more-time.md) — ten more
   minutes, with the game still open.

A second computer is its own step, and needs 1 to 4 done on this one first:

7. [**How to link another computer**](how-to-link-another-computer.md) — one
   command over ssh, and one day read across both. The far computer gets the
   whole of omahouse and not an agent, which is what makes it still work when
   this one cannot be reached.

The browser is its own half, and needs nothing above it except a profile:

8. [**How to stop a site opening**](how-to-block-sites.md) — blocking, allowing,
   incognito, and the one surprise: the policy holds for the whole machine.
9. [**How to give a site so many minutes a day**](how-to-limit-time-on-a-site.md)
   — thirty minutes of YouTube rather than none, and why the minutes are counted
   only while somebody is in front of the screen.

**The shortest useful path** is 2, 3, 4 — write a profile, release the programs,
put a number on the day — and then a day of 5 before turning the teeth on. A new
profile counts and closes nothing on purpose, and that day of evidence is what
it is for.

## When you know what you want

| the page | what it holds |
|---|---|
| [the command line](cli.md) | every verb, every flag, in full. **Generated** from `omahouse.usage.kdl`; `mise run verify` refuses a commit where the two have come apart, so it is never out of date and never edited by hand. |
| [the window, in the order somebody meets it](screens.md) | every screen `omahouse-studio` draws, as a walk from opening it to running a profile day to day — with the keys, the chips, and the four things that have no screen. |
| [what does not work yet](what-does-not-work.md) | every defect, in one place, each with what to do in the meantime. Nothing else here names a defect that is not on that page. |
| [how it is built](design.md) | for contributors: the model, the eight rounds of measurement that changed it, the packaging, and the gate. Its section numbers are cited from comments throughout `src/`, so they do not move. |

## The record: what was proposed, and what became of it

Two of these pages are not documentation of a feature. They are the argument and
the measurement behind decisions that were made, kept because the next person to
reach for the same idea should find out in an afternoon what cost a week.

| the page | what it is |
|---|---|
| [what was proposed and not built](not-built/README.md) | the register: everything argued for and not built, with the reason for each. Start here rather than in the two pages below. |
| [network control per account — measured, and not taken](not-built/network-control-per-account.md) | a per-UID `nftables` design, measured in two VMs and abandoned. The browser sends no packet to port 53; a set of addresses named `youtube.com` does not contain the site. What is still a candidate is named as such. |
| [where the browser half was decided](the-browser-half.md) | the argument the shipped browser half came from — Chromium source reads, the policy constraints, and the ecosystem survey. Most of it shipped; where it disagrees with `design.md`, `design.md` wins. |

---

## Conventions across every page

**`kid` is a placeholder**, not a keyword. Every verb takes the login name of an
account that already exists on this machine as its first argument, spelled the
way `id` or `ls /home` spells it.

**Reading needs no privilege; writing needs root.** `omahouse status`,
`omahouse report` and `omahouse profile list|show` run for anybody, and the
person under rules runs them about themselves. Everything that writes needs
root, and the window gets there through `pkexec`.

**Two kinds of picture, and the pages say which.** Anything under `docs/img/` is
drawn by the studio itself, offscreen, by `mise run shots`, and
`mise run verify` refuses a commit where it is not what the window draws today.
Anything under `vm/shots/` is one run through a live Omarchy desktop on
2026-09-04 and is not regenerable — those are the pictures where the account
under rules is called `julia` while the commands beside them say `kid`.

**What is measured is said to be measured.** A number in these pages came off a
machine, and where something has only been argued or read out of somebody
else's source code, the page says so in those words.
