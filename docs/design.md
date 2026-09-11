# How it is built

This page is for contributors. What the commands do belongs in
[the how-to pages](README.md) and [the command line](cli.md); this one is the
model, the measurements the model was changed by, and the gate that protects it.

The section numbers below are load-bearing. Comments across `src/`, `tests/`,
`packaging/` and `vm/` cite them as `docs/design.md §N`, and §1 to §10 are the
same numbers they have always had.

---

## 1. The map

The problem is not new. It is lan house management: an operator hands over time,
the terminal counts it, warns before it runs out, and ends the session when the
credit does.

| lan house | omahouse |
|---|---|
| the terminal | a Linux account, without privilege |
| the operator at the counter | whoever is in `wheel` |
| the token, the credit | the day's budget in minutes |
| start of the session | login |
| "five minutes left" | a notification in the profile's session |
| ending it and freeing the machine | `loginctl terminate-user` |
| the programs released | an allowlist over app identities |
| closing the till | the day's report |

Three things that model settled, copied to the letter:

**Everything is a balance, not a prohibition.** A program outside the list is a
program with zero credit. That collapses allowlist and time limit into one
concept.

**The terminal does not trust itself.** What decides is a root daemon; the
fiscalised session only displays. This is why the enforcement cannot live in an
`omarchy-shell` plugin — that runs as the fiscalised user, who ends it with a
`pkill`.

**The operator hands over time with the program still open**, without restarting
anything.

The engine does not know what a child is. It knows a profile, a rule and a
budget. Limiting a daughter's screen time and limiting your own Twitter time are
the same configuration with different numbers.

---

## 2. The model

Three nouns, and none of them is specific to a use case.

**Profile** — an account under rules. It has a default verdict, a list of rules
and a list of budgets.

**Rule** — a pair `(selector, verdict)`. The selector is the app identity of §5;
the verdict is `allow` or `deny`. The profile's default verdict decides what
happens to whatever matches no rule: `deny` is an allowlist, `allow` is a
denylist.

**Budget** — a daily counter with a selector, a limit and an action on
exhaustion.

```
Budget { id, match, kind, dailyMinutes, resets, onExhausted }

  kind:         app      the selector is an app's scope identity (§5)
                site     the selector is a site's registrable domain (§5.2)

  resets:       daily    the clock goes back to zero at the turn of the date
                never    it does not go back to zero at all

  onExhausted:  close    closes the processes matching the selector   (app)
                logout   ends the user's session                      (app)
                block    stops the site opening, until the day turns  (site)
                warn     says something, and records it               (either)
```

`match` is a list of names, and several names is one clock between them. One
program is not always one id: a single Chromium window produces `chromium`,
holding the child processes, and `org.chromium.Chromium`, holding the one that
owns the window. Two budgets of forty-five minutes is not a browser limited to
forty-five minutes — it is two clocks that happen to agree, and half a browser
with no limit the moment somebody writes only one of them.

The generalisation that makes three nouns enough: **the session is the budget
whose selector is `*`**. There is no separate concept of user time and app time.
In the code there is no special case for the session.

`resets` is the option **not to reset** and not a second engine. `never` is a
pot rather than an allowance: two hours are two hours until somebody hands over
more, and logging out does not give them back, the turn of the date does not,
and walking to another computer does not. It is the shape a lan house sells, and
it is the only shape that is a limit on a *person* — a clock that started over
at the login would hand two free hours to anybody who logs out and back in,
which is the opposite of a limit. The counting, the `warnAt` marks, the `grace`
window and the action are the machinery that was already there, and what differs
is only whether the turn of the date empties the counter. `dailyMinutes` keeps
its name under either, because what it holds is the size of the allowance and
not the length of the day.

**`never` and a profile that names no account do not go together.** A pot on a
shared login empties once and is never refilled, so the second person to sit
down gets a spent clock and no midnight to rescue them — because `never` is what
took the midnight away. A machine carrying rules for whoever sits at it wants
`daily`.

`kind` is the one field a site budget added, and it is here rather than in a
second list because the ambiguity is real: `org.freedesktop.Platform` is a scope
id with dots in it and `youtube.com` is a domain with dots in it, and there is no
shape that tells them apart. The web *rules* of §11 get away without it by living
in their own list; `budgets` is one list, and a budget that guessed which
namespace it was in would eventually guess wrong about somebody's flatpak.

Everything else about a site budget is the machinery an app budget already had,
unbranched: the daily minutes, the grants, the `warnAt` marks, the `grace`
window, the notification, and the ledger remembering what has already been said.
What differs is the two ends — which observation spends it, and what happens when
it runs out — and both of those are one branch on `kind` in `evaluate`, put there
because `*` is a legitimate site selector and a shared loop would have a limit on
browsing closing every app on the machine. An `onExhausted` that cannot happen to
that kind is refused where the file is read, and not repaired into something that
would run.

### `logout` is two things

`loginctl terminate-user` ends a session in seconds, and on a machine with
autologin the user is back an instant later. Ending without preventing the
return is theatre. The block is one line of stock PAM, in
`/etc/pam.d/system-login`:

```
account required pam_listfile.so item=user sense=deny \
        file=/etc/omahouse/blocked onerr=succeed
```

`onerr=succeed` is not optional: a missing or unreadable file has to let
everybody in, or a machine locks itself out of itself. So `logout` is: write the
name into the file, `terminate-user`, and take the name out again.

Nothing has to remember to take it out. Every cycle works out from today's
ledger who should be refused right now and writes exactly that, so the turn of
the day, a `grant`, `profile enforce --off` and `profile remove` each let
somebody back in.

---

## 3. Architecture

Four qmake subdirectories, `ordered`.

| project | artefact | runs as |
|---|---|---|
| `src/core` | `libomahousecore.a` | — |
| `src/sys` | `libomahousesys.a` | — |
| `src/cli` | `omahouse` | the user, and root under `watch` |
| `src/studio` | `omahouse-studio` | the user, **never** root |

Two libraries and not one because the split is checkable. `core` has no clock,
no environment and no `/sys/fs/cgroup` in it, which is what lets a two hour
budget be proved in microseconds. Everything that reads or writes the machine is
in `sys`, on the other side of a line the gate can check.

**The daemon is a subcommand, not a fourth binary.** systemd runs
`omahouse watch`. No second process, no IPC, no second reader of the profiles.
The browser's native messaging host is a subcommand too — `omahouse meter`, §5.2
— and it keeps that promise rather than breaking it: it holds no state, reads no
profile and writes no ledger, so there is still exactly one thing accumulating a
day.

**The studio is never root.** It links both libraries to read — everything it
reads is world readable — and every write goes out as `pkexec omahouse <verb>`,
so the privileged half is the same CLI, with the same argument checking and the
same refusals. It refuses to start as root rather than working and letting
nobody find out.

---

## 4. State on disk

Two files, both JSON, both read and written by `core`.

Each file carries its **own** `schemaVersion`, and that is not decoration. It
was one number for four -- profiles, the day's ledger, `machine.json` and
`machines.json` -- which meant a change to any of them invalidated all the
others: a profile gaining a meaning would throw away the day a machine was in
the middle of, and nobody would connect the upgrade to the loss. The effect of
one number for four is that it is never raised, so the check that exists to
refuse a document this code would misread sat there never firing. Separately,
each is cheap and moves when it should.

What the number means is unchanged, and **it is about meaning and not only about
fields**. `profiles.json` went to 2 when a profile's `user` could be `*`: no
field was added, and an older reader parses such a file happily, binds an account
that cannot exist and enforces nothing. A reader that quietly ignores rules is
worse than one that stops.

`/etc/omahouse/profiles.json` (0644, root writes, everybody reads) holds the
profiles: `enabled`, `enforce`, `default`, `warnAt`, `grace`, `rules`,
`budgets`. `user` is the only required field.

A profile also carries `writtenBy` and `writtenAt` once something has changed
it. The file is written by root and read by everybody, and the honest answer to
*who put this here* used to be "somebody". `writtenBy` is whoever asked, and not
`root`: `pkexec` sets `PKEXEC_UID` and `sudo` sets `SUDO_UID`, and sudo makes
the real uid root as well as the effective one, so asking the process would
answer `root` for somebody who typed their own password a second ago. It is the
same answer a grant records. A profile pushed from a central omahouse arrives
through the node's service account and is stamped with that account, which is
what lets a merge tell a profile the household issued from one an administrator
typed on the machine. The stamp is set where the file is saved rather than by each verb —
there are a dozen places that change a profile, and a stamp a caller has to
remember is one that will be missing from whichever verb is added next — and
what is on disk is compared against what is about to be written, so exactly the
profiles that differ are stamped. A run that rewrites the file without changing
anybody leaves every stamp where it was, or the record of who last changed a
rule would be a record of who last ran a command.

- A permitted program with no budget of its own spends the day's total.
- `enforce: false` is **observing mode**: it counts and reports, and closes
  nothing.
- `warnAt` is the list of minutes-remaining marks at which the person is warned.
- `grace` is the seconds between the last warning and `cgroup.kill`.
- An absent `default` means `allow`, not `deny`. A half-written profile that
  counts without biting is recoverable; one that denies everything locks
  somebody out of their own machine. Same choice as `onerr=succeed` in §2.

`/var/lib/omahouse/<user>/<YYYY-MM-DD>.json` (0644) is the day's ledger: the
seconds spent per budget, the grants, and the events. One file per day, written
atomically (tmp + `rename`). The balance resets at the turn of the local date; a
session open across the turn stays open and starts debiting the new file.

The events are not only a record — they are the **memory of what has already
been said**. A `warn` carries the `warnAt` mark that fired, which is what stops
the same warning going out every two seconds; an `exhausted` carries the instant
that opens the `grace` window, read from disk rather than from a counter in
memory, so a daemon restarted mid-window resumes it instead of reopening it.

Since §5.1 the day also carries a `presence` object: seconds of the day spent in
each state of being in front of the machine, keyed by the state's name. Since
§5.2 it carries a `sites` object too: seconds spent with each registrable domain
in the front tab of the browser. Both sit **beside** `budgets` and never inside
it, and both are written only once there is something to say. Not for the sake
of ledgers that already exist — this file is rewritten every couple of seconds
for as long as the machine is on, and `"presence": {}` on a machine that cannot
read a screen is a claim about a measurement that never happened, made twice a
second forever.

That is a different rule from reading two spellings of one value, which
`profiles.json` did for a budget's `match` and no longer does. Writing a field
only when it means something is design and stays true whoever is running this.
Accepting a second spelling was compatibility with files that do not exist, and
it went — recorded here rather than quietly deleted, because it is the kind of
convenience somebody reintroduces believing they are being kind to whoever edits
the file.

A `kept` object joins them for the budgets whose `resets` is `never`: the
seconds spent against each of them. It sits **beside** the daily seconds, and
this second map is the load-bearing part of the whole idea. A counter that
survives the turn of the date is carried into the file of every day it touches,
and everything that adds days or machines together reads the daily map —
`report`, `omahouse house`, `consolidate`, `collect`. In `seconds`, a pot would
be counted once per day it crossed, and a fortnight's report would show it spent
fourteen times. Kept apart, all of them go on being right without knowing this
exists.

A `keptGranted` object joins it, for the other half of the same subtraction.
Handing over more is the only thing that refills a pot, and a grant lives in the
file of the day it was made — which is right for `report` and for the household,
both of which add days together out of `grants` and would count a carried grant
once per day it outlived. So the turn of the date **folds** the outgoing day's
grants for a pot into a running total, once, and reads them from there
afterwards. Without it the night took the operator's decision away and left the
spending it had paid for: a pot refilled at eleven came into the morning with
yesterday's hour against today's smaller allowance, which is worse than losing
both halves.

The events do **not** come with it, and that is deliberate. A pot that ran out
yesterday and is still out says so again this morning before it acts, which is
§6: nobody is cut off cold, and somebody meeting a spent pot at nine has been
told why before their window closes.

**The turn is applied by whoever reads the day, and not only by whoever was
running when it happened.** A ledger is named after its date, so the first look
at a new day meets no file at all — and a loop that read only today started
every pot from full each morning, which is the one thing `resets: never`
promises will not happen. `readDay` is the reader every verb about *now* goes
through: it reads today's file, and where there is none it finds the newest
ledger this person has from before today and applies the turn to that. The
newest and not yesterday's, because a machine that spent a week switched off has
a last day a week back, and there is no arithmetic on dates in it.

Two things in that directory are not days and both would win a sort. A name that
is not a date is skipped rather than read, because a stray file that fails to
parse would stop the loop counting that person at all. A file dated in the
future is skipped too, and that one parses: a clock that ran ahead once leaves
tomorrow's file behind, and a pot carried backwards out of it is a total nobody
spent. What is *not* skipped is a last day that is there and will not parse —
that is said out loud and the person is left uncounted for the cycle, because
starting a pot from zero on a file somebody could still repair hands over
everything it ever counted and writes the zero down where the evidence was.

Which of the two counters a budget spends is asked in one place, `spentSeconds`,
and that is not tidiness. It was asked in four, three of them right: `status`
and the window read the daily map for every budget alike, so a pot showed
nothing spent and its whole limit left, however much of it had gone.

`omahouse limit --resets never` is what writes it, on any of the verb's three
shapes, and `--resets daily` is what a budget means when it says nothing — so it
is never written into the file, a new budget that says nothing is daily, and a
budget that is already there keeps what it was, the way it keeps what it does
when it runs out. The rest of what a pot is across a household is written down
in [`design.md`](design.md).

A profile whose `user` is `*` applies to anybody who logs in without one of
their own — `Profile::anybody`. It is how a machine carries rules for whoever
sits at it rather than for forty people named one at a time. Four of its rules
are decided and none shows in the field: a profile of somebody's own wins and
the whole list is walked before this one is considered; an administrator is
never covered, and that is a live filter in the cycle rather than a fact about
the file, so being put in `wheel` takes effect on the next tick; the machine's
`Kind` does not come into it, because a profile exists by being written and a
second unwritten condition on it is the failure §8's pages keep making; and each
person is reported under their own name, so the commands printed beside them can
be run. A budget that never resets is refused on it where the file is read.

**It runs the other way round from every other profile.** `watch` goes from a
profile to its user to a uid, and this one has no user to start from, so `Proc`
answers which accounts have a session — read from the cgroup tree, because the
question is who is here and not who could be.

The same schema with `default: "allow"` and a `deny` rule per distraction is a
focus profile for an adult. Nothing in the engine changes.

---

### Publication records

Each `elsewhere/<machine>/<user>/published.json` records schema version 1,
`publishedAt`, and the exact central `profile` last successfully published and
confirmed. `profile.json` remains the machine's collected wire envelope, including
its own stamp. Publication compares rules without stamps.

A merge writes `resolved.json` with schema version 1, the exact observed profile
(or null for absence), and the chosen central rules without a stamp. This resolves
only that observation and that choice; changing either requires another decision.
It does not claim a successful publication. Keeping central rules can therefore
leave a machine behind even when those rules equal the last publication.

## 5. Enforcement

`omahouse watch`, as root, on a two second cycle:

1. Find which users with a profile have an active session (`/run/user/<uid>`).
2. List that user's **app scopes** under
   `/sys/fs/cgroup/user.slice/user-<uid>.slice/user@<uid>.service/app.slice/`.
3. Take each scope's identity and match it against the profile's rules, or fall
   through to the default verdict.
4. Debit two seconds from every budget with at least one live scope matching its
   selector — once per budget, not per process.
5. Decide: a `deny` verdict closes on the spot; an exhausted budget warns, waits
   out `grace`, and runs its `onExhausted`.
6. Persist the ledger.

**Closing** is `SIGTERM` to every process in the app's scope, then that scope's
`cgroup.kill` once `grace` has gone by. The polite half first, so an editor
writes its buffers; the write second, because it takes the whole cgroup at once
with no reaping order and no orphan. An app that leaves on its signal is never
written about.

**`session.slice` is never reached**, and that is structural rather than
careful. Only `app.slice` is walked, only a scope found in that walk can be
named, and the write is refused unless the path is inside that user's own
`app.slice`, is a `.scope`, has no `session.slice` in it, and lives in a real
cgroup tree. This is the failure that would end the product — an allowlist
taking Hyprland down two seconds after somebody logs in — so it is checked five
times over rather than implied once.

### The identity of an app is the cgroup, not the executable

The earlier design matched `/proc/<pid>/exe` against binary paths. Round 1 below
killed it. A binary path does not identify an app, does not group its process
tree, and does not separate an app from the session's plumbing.

Omarchy's `uwsm` already solves this. Every launched app goes into a scope of
its own under `app.slice`, and the session's plumbing stays in `session.slice`:

```
session.slice/wayland-wm@hyprland.desktop.service   Hyprland, quickshell
session.slice/pipewire.service                      pipewire
app.slice/app-graphical.slice/app-Hyprland-chromium-031bdc27.scope
app.slice/app-code-3579042.scope
```

Three consequences, all of them simplifications. There is no baseline list to
maintain, because the rule is structural. The app's process tree arrives grouped
for free. And closing becomes one write rather than a hunt for PIDs.

### A live scope is a live scope, named or not

A scope with processes in it is a person using the machine, and the `*` selector
matches all of them — including the ones the parser cannot name. Without that, an
afternoon inside a terminal debits nothing, which for a child's profile is the
obvious way out.

An id is required only where it is really needed — matching a *specific* budget or
rule. A scope with no id is never debited by a single-app budget and never judged
by a named rule: it falls through to the profile's **default verdict**. The
consequence is intentional: under `default: deny` with `enforce: true`, a scope
with no id is closed, which is the coherent reading of an allowlist. `status`
says both of those things out loud.

### 5.1 Presence, which is measured and does not act

A spike put a Chromium extension on real Omarchy and asked `chrome.idle` every
twenty seconds for thirty minutes with nobody at the keyboard
(the browser spike). It answered `active` every single time, including
the last twenty-five minutes with the monitor physically off. A browser does not
merely fail to notice idleness — it reports presence that is not there, and any
time-per-site meter built on it runs all night beside a sleeping child.

So presence is omahouse's own question, asked of the machine. Two facts, both of
them read every cycle by the daemon that is already root:

- **the screen** — `/sys/class/drm/<connector>/dpms`, for every connector whose
  `status` is `connected`. On if any of them is on. The kernel writes it, and
  Hyprland turning a monitor off is an atomic modeset that carries into it.
- **the seat** — `loginctl show-seat <seat> -p ActiveSession`, then that
  session's `User`. logind is root's own service and the daemon already ends
  sessions through `loginctl`, so this is not a new door.

**The cheapest candidate was measured dead, and it is worth writing down.**
`loginctl`'s own `IdleHint` would have cost nothing at all — the root daemon
already speaks to logind. Over **852 samples** on real Omarchy, `IdleHint=yes`
came back **0 times** and `LockedHint=yes` came back **0 times**, through both
the instant the shell locked the session and the instant the screen went dark.
The shell knows: it takes idleness from `ext-idle-notify-v1` and locks on its
own timer, and it tells logind none of it, because nothing in Omarchy calls
`SetIdleHint` or `SetLockedHint`. `/run/systemd/sessions/<id>` has no
`IDLE_HINT` and no `LOCKED_HINT` in it either, and its first line says it is
private data and not to be parsed. Dead, and not because it is expensive.

**What is deliberately not read is the fiscalised user's compositor.** Root
could connect to `$XDG_RUNTIME_DIR/hypr/$HIS/.socket.sock` and ask `hyprctl
monitors`; it works, and it costs 0.17 ms against the DRM attribute's 0.015 ms
and `loginctl show-seat`'s 4.3 ms. It is refused because that socket lives in a
directory the fiscalised user owns, and a child who kills her own Hyprland and
puts her own program on that path is a child feeding bytes to a JSON parser
running as root.

**And the refusal costs almost nothing, which is why it is affordable.** Over
those same 852 samples, Hyprland's own `dpmsStatus` and the kernel's `dpms`
**never once disagreed**: the expensive candidate knows nothing about the screen
that the cheap one does not. What it knows in addition is the lock — Omarchy
4.0.2 has no `hyprlock` process to find, the lock is an `ext-session-lock` held
by `quickshell`, and logind's `LockedHint` stays `no` right through it — so a
session locked with the screen still lit reads as `Using` for the eight seconds
until the display goes off. That is the whole price, it is named in the
vocabulary as `Locked`, and `SeatPresenceSource` never returns it. The seat has
to be read for a different reason: with the VT switched away, Hyprland went on
answering `dpmsStatus=True` while somebody else's session was on the screen, and
only logind saw it.

**It is measured and reported, and it acts on nothing.** What an app is billed
is its running time — the decision above, and published — and a screen going
dark does not change it. Presence appears in `status`, in the journal line, and
in the day's ledger beside the budgets. What uses it is §5.2, the time per site,
which is the one number in this program that a dark screen does change.

### 5.2 Time per site, and the budget on it

`design.md` proposed a Chromium extension that reports the site in
the front tab, and the browser spike measured its whole chain on real
Omarchy. This is what shipped of it. It counts the number, and — since §5.3 — a
number that runs out acts.

The counting came first and on its own, which was the observing stage `enforce:
false` is for the apps: a week of the number before anybody decided it was worth
teeth. §5.3 is the teeth, and it added one field to §2's `Budget` and one variant
to the decision, and nothing else.

Three parts, and the split is the point: **the extension is an eye, the engine is
the brain.**

```
extension/            MV3, permissions ["tabs","nativeMessaging"], no content
                      script, no host_permissions. Reports the registrable
                      domain of the active tab of the focused window, every
                      transition and every five seconds. Nothing else.
omahouse meter        the native messaging host. Runs as the child, appends
                      `<epoch> <site>` to /run/user/<uid>/omahouse/focus, and
                      stops. No ledger, no budget, no accumulation, no privilege.
omahouse watch        reads that file every cycle, crosses it with §5.1's
                      presence, and debits the tick to the site in the ledger.
```

**Why a file and not a socket.** `design.md` §8.1 called this "the
largest single piece of unplanned work": the host is spawned by the browser as
the child (the browser spike measured uid 1001, in her session, with
her bus in the environment) and the ledger is root's, so the argument there reached for
a socket in the daemon, with framing and a second writer's worth of validation —
against §3's "no second process, no IPC, no second reader of the profiles".

The file is cheaper because the host is made **stupid** rather than trusted. The
daemon is already root, already ticks every two seconds, and already knows
whether anybody is in front of the screen; it reads this the way it reads
`/sys/class/drm`. No endpoint, no protocol to version, nothing listening, and the
accumulation stays in exactly one place — which is what §3 was protecting.

**The crossing is what makes the number honest.** The browser spike
asked `chrome.idle` ninety-four times through thirty minutes of an empty room and
got `active` every time, including twenty-five minutes with the monitor
physically off. A browser is a reliable witness to *what* is on the screen and a
proven liar about *whether anybody is looking at it*. So the name comes from the
browser and the presence comes from §5.1 — the kernel's DRM attributes and root's
own logind — and **a second is billed only where the two agree**. A tab left on
YouTube overnight adds nothing, and `status` and the journal say `not counted` in
so many words when they disagree.

**The file is untrusted input, and that is the design and not a caveat.** The
child owns the directory. She can write anything into it, truncate it, delete it,
or kill the host. So the daemon opens both components with `O_NOFOLLOW`, opens
the file `O_NONBLOCK` so a fifo cannot hold the cycle, refuses anything that is
not a regular file owned by that uid, reads a bounded tail, and looks at **only
the last complete line** — not the last one that happens to parse, because
scanning backwards for something usable is how a file full of rubbish still bills
a site. Every way it can be wrong is one answer: nothing is billed this tick.

What that costs her is worth naming precisely, because it is why this is safe to
build at all: **she wins anonymity, not minutes.** The total time on the machine
is held by the cgroup walk of §5 and by the PAM line of §2, and neither is
reachable from that file. A child who kills the host has a short per-site table
and a session that ends at exactly the same minute.

**The privacy boundary is the registrable domain, and it is kept in the
browser.** The URL never crosses the wire, so there is no bug, no compromised
host and no readable file by which the page somebody was on can be read out of
omahouse. The reduction to `youtube.com` is a short table and not the public
suffix list, on both sides of the wire; being wrong about `bbc.co.uk` costs a row
with an ugly name and never a minute in the wrong place, and carrying the public
suffix list to fix one row is not a trade a household control should make.

**Where it shows.** `sites` in the day's ledger, beside `budgets` and never
inside it; `TIME PER SITE` in `status`, with what is in front right now and
whether it is being counted; `TIME PER SITE` in `report`, and `TOTAL PER SITE`
over a range. Written only when there is something to say, so a machine with no
extension on it writes exactly the file it always wrote.

A site that also has a budget appears twice, and the two rows are two different
things: `sites` is every domain that was ever in front, budget or no budget, and
`budgets` is what a limit has been spent. They cannot disagree, because one
function writes both in the same statement.

### 5.3 A budget on a site, which acts

The step §5.2 was the observing stage for. A site budget is §2's `Budget` with
`kind: "site"` on it, and the whole of what that changes is the two ends.

**What spends it** is `siteInFront`, the one thing `evaluate` is told about the
browser: a registrable domain, or nothing. The crossing of §5.2 happens in `sys`,
where a screen is a thing that exists, and what goes across the line into the
pure half is a name and never a state — so there is no budget anywhere that can
be spent by a screen being on. A tab left on YouTube overnight spends nothing,
which is the same sentence §5.2 already made true about the number and is now
also true about the limit.

**What happens when it runs out** is a fourth decision, `Block`, and it is its
own kind rather than a `Close` with a domain in it because a site is not a
cgroup. The domain goes into the `URLBlocklist` the web half of §11 already knows
how to write, composed with whatever the profiles' own web rules say — and the
clock has the last word in that composition: a profile that allows a site is
allowing it in general and not for the thirty-first minute, so a site that has
run out is blocked and is never also allowlisted.

**It comes back on its own, and that is the half worth having.** Nothing
remembers a blocked site. Every cycle works out from today's ledger which sites
are out of time right now — the same zero-tick question `blocked` is re-derived
by in §2 — and makes the policy file say exactly that. So the turn of the local
date lets the site open again, and so does a `grant`, and so does
`profile enforce --off`, and so does removing the profile, with none of those
verbs knowing the browser's policy file exists. The file is **removed** and not
emptied when nothing is out of time, for §11's reason: an empty managed policy
left behind is a machine that still looks managed.

**The warning, the marks and the grace come from the app half unchanged.**
`warnAt`, `grace`, the notification of §6 and the ledger's memory of what has
already been said are the same code paths; only the verb in the sentence differs,
because a site stops opening rather than closing. That sentence is the only place
anybody finds out *why*: §11 already says Chromium's block page names nobody and
explains nothing, and omahouse never learns the attempt happened.

**Two guards, and both are about `*`.** A site budget matching `*` is a limit on
browsing at all, which is a coherent thing to ask for — and `*` is also the
session budget's selector. So the debit is a branch on `kind` and not a shared
loop with a filter, and so is the action: without the first, a limit on browsing
would be spent by every app scope on the machine; without the second, it would
close every one of them when it ran out.

`omahouse limit <user> --site youtube.com=30m`, beside the `--budget` it mirrors.

### The signing key, and why there is not one

`design.md` §6.3 named the custody of the key as a new problem and did
not decide it. It is decided here: **there is no key to keep, because the key is
made on the machine during `pacman -U` and dies with `pacman -R`.**

The two ways to pay §6.3's bill were a release key held by whoever cuts releases,
with a `.crx` prebuilt inside the package — the ordinary answer, and the one
whose price is a single file whose leak lets a stranger sign an extension that
*every* installed policy force-installs, with no store and no review in the
way — or a key per machine. A Chromium extension's id is the first sixteen bytes
of the SHA-256 of its signing key's public half, so a key per machine is an id
per machine, and the policy can only be written *after* the key exists, naming
what that key produced. `packaging/omahouse-meter-pack` does exactly that, from
`post_install`, and is the whole of the mechanism.

**What it buys.** Nothing to hold, nothing to rotate, nothing to lose, nothing
whose leak reaches a second machine. The key that signs kid's meter cannot sign
anything her neighbour's Chromium would accept, and anybody who can read it is
already root on the only machine it means anything on.

**What it costs, and it is not nothing.**

*The id is not a constant any more.* Nothing may name the extension statically —
not the policy, not `allowed_origins` in the native messaging manifest, not a
support page. Everything that needs it reads `/etc/omahouse/meter/id`. A bug
report naming an extension id is now a bug report about one machine.

*A reinstall makes a different extension.* `post_remove` deletes the key, so
`pacman -R` then `pacman -U` gives a new key, a new id, and an extension Chromium
has never seen. It uninstalls the old one when the policy stops naming it and
installs the new one fresh. That is affordable here only because this extension
keeps no state at all — no `storage` permission, no options, nothing saved per
site — so a new id loses nothing. An extension with settings could not take this
trade, and this is the sentence to re-read if one ever grows them.

*An upgrade is not a reinstall.* `post_upgrade` runs the packer again, finds the
key, and re-signs the same id at the new version — which the browser takes
through the `file:` update channel as an update, not as a second extension.
The key surviving an upgrade and not surviving a removal is the whole design in
one line.

**The package cannot ship a `.crx`, and does not want a browser to make one.**
A `.crx` is a zip behind eleven bytes of magic, a protobuf header and an RSA
signature over both; `openssl` and `zip` are the entire toolchain, and
`omahouse-meter-pack` writes the header by hand from
`components/crx_file/crx_file.proto`. That matters beyond taste: this runs inside
a `pacman` transaction, and a scriptlet that starts a browser — profile, GPU
probe, sandbox — inside one is a scriptlet that will hang an upgrade for reasons
nobody can read. `vm/PKGBUILD` therefore ships three static files (the host shim,
the extension's source, the packer) and nothing that names an id; the archive,
the `updates.xml`, the force-install policy and the native messaging manifest are
all written on the machine.

**And `post_remove` takes all five off** — policy, archive, update manifest, host
manifest, key — whether or not the package put them there. Same argument §9 makes
about the PAM line: a force-install policy left behind is a browser installing an
extension whose host is gone, and a signing key left behind is the one artefact
this design exists to not have.

It takes them off because they are on the one declaration §9 describes, and not
because five `rm` lines were written by hand. That distinction is not a detail:
the host manifest is the artefact that was left out of the hand-written column
and survived a `pacman -R` in the VM, and it is the reason the column stopped
being hand-written.

### Where the logic lives

`Policy` and `Ledger` are pure functions: they are handed the scopes, the
profile, the ledger and `now`, and give back the decisions and the new ledger.
No `/proc`, no `kill()`, no clock inside. `Proc` is the adapter that really
reads `/sys/fs/cgroup` and `/proc`. `watch` is the loop that joins the two and
carries out the decisions.

No seccomp, no eBPF, no AppArmor: a `QTimer`, a read of `/sys/fs/cgroup`, and a
write.

---

## 6. Warnings, and ending a session

The daemon is root and the person is in another session. To speak to it:

```
systemd-run --uid=<uid> --setenv=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/<uid>/bus \
  notify-send "5 minutes left" "Minecraft closes at 19:35"
```

Two lines of `QProcess`, with no helper process inside the fiscalised session.
Nobody is cut off cold: the sequence is always warning → `warnAt` → `grace` →
end.

---

## 7. The command line

The verbs are declared in `omahouse.usage.kdl` and [`cli.md`](cli.md) is
generated from it; `mise run usage:check` refuses a commit where the two have
come apart.

Writing needs root, and the studio gets there through `pkexec`. `status`,
`report` and `profile list|show` are free — the fiscalised person runs
`omahouse status` and sees what is left of their own day.

`allow <user> <id> --limit 45m` is sugar: it writes the rule and the budget at
once, because that is how somebody thinks while configuring. `grant` is the one
verb beyond the agreed scope of four things; it is here because an operator who
cannot hand over ten minutes with the game open is not an operator.

`limit` is where a budget's shape is written, and so it is the one verb that
takes `--resets`. Not `allow --limit`: that is sugar for the common case, and a
pot is not it. The line the verb prints back tells the two apart — `2h a day`
and `2h in all` — and `profile show` and `status` carry a `RESETS` column on
every row rather than a mark on the odd one, because a column blank on most rows
reads as a column with no answer.

Every root the program reads moves by environment variable, which is how the end
to end suite runs as an ordinary user with nothing installed. Two of them also
keep a run pointed somewhere else from biting: a `close` is refused unless the
cgroup tree really is `/sys/fs/cgroup`, and a `terminate-user` is refused unless
the configuration really is `/etc/omahouse`. Both refusals name themselves in
the journal.

---

`profile publish <user> --to <machine>…` (or `--all`) runs only on the manager.
It reports per-machine standing and results; `--json` adds `user`, `unresolved`,
`changedOn`, `notChecked`, and `machines` rows with `machine`, `state`, `said`.
`profile list` adds a publication summary on a manager. `profile show` adds the
machine table, while its JSON envelope stays unchanged.

## 8. The studio

One Qt Quick window, two faces, chosen by whoever opened it. Whoever is in
`wheel` gets the operator's: the people under rules, the programs released to
each of them, the day's balance live, the sites that open and the minutes on
them, and the chips to change all four. Everybody else gets the subject's, which
is the same window with nothing to press.

**The sites are a view of their own and not rows inside the programs.** The same
call §11 makes on the command line, for the same reason: taking a program off
somebody's list and changing what every browser on the machine will open are
different enough acts that they should not be one word — and they cannot be one
list either, because not one command on the row is shared. `x` on a program
writes `deny`; the nearest thing on a site is `web allow`, which is not a
removal. What that view carries above its list is the two things that are about
the whole view and never about a row: the day's presence of §5.1, because a site
is counted only where the browser and the screen agree and that is why the number
can be smaller than the afternoon felt; and the reach of §11, once.

**It is not a generic rule editor.** The engine is generic; the interface talks
about programs and minutes, which is how people think. `default: deny` with a
list of `allow` presents itself as "the programs released", not as a form of
verdicts.

There is one table of commands in the window; the chips are drawn from it, the
keys are looked up in it, and `:` and `?` list it — so an action cannot exist on
only one of the two. `Theme.cpp` reads the omarchy theme and the corner rounding
from `looknfeel.lua`, live.

Every screen it draws is inventoried in [`screens.md`](screens.md).

---

On the manager, the people view also reports draft publication standing.
`u publish` opens a multi-select machine sheet and runs one privileged
`profile publish` call for the chosen destinations. Unresolved profiles disable
that action and identify the required merge. The sheet retains all CLI result
lines; dismissing it while the command runs only hides it. `f machines` is
available without a person and still lists machines for a profile without budgets.
The header exposes the computer's household role and names its managed machines.

## 9. Packaging

`packaging/` holds the unit, the polkit policy, the polkit rule, the desktop
entry, the icon and the `.install`. The install script creates `/etc/omahouse` and
`/var/lib/omahouse`, adds the PAM line of §2, enables `omahouse.service`, and
takes all of it out again on removal.

### Publishing a draft

The central is the source of truth. Editing a profile there makes a draft;
`omahouse profile publish kid --to "the kitchen laptop"` delivers it explicitly.
The CLI decides and the Battery carries it: there is no omahouse receptor or
network client. Install `omahouse.sync` and `omahouse.profile-publish` in the
manager's Battery workspace, and `omahouse.profile-send` and
`omahouse.profile-push` on each managed node.

Every publish checks all paired machines before pushing anywhere. A collected
change against the last publication makes the profile unresolved everywhere;
before the first publication, an empty remote envelope is an ordinary empty
machine. A failed check keeps the last collected copy standing. Use
`profile merge kid --take <machine>` to bring its rules here, or `--keep` to keep
the central draft. Keep still prints the wire document and also records the
explicit decision. Neither merge publishes.

Targets up to date are skipped. `--all` selects only reached, paired machines
behind or never published; unavailable machines remain visible as not checked.
Explicit unavailable targets fail. Each push carries the exact checked remote
profile as `supersedes`. Concurrent publications are serialized separately from
profile editing; an edit during publication stops remaining sends and asks for
another publication. Successful pushes are read back before recording completion,
so the next collected stamp is the receiver's real stamp. A lost confirmation is
reported as applied but unconfirmed, never as a remote refusal.

Publication and merge compare policy without its write stamp or `allocation`.
Allocation is runtime state bound to one machine: the central is `here`, each
client has its own fleet name, and scheduled cycles advance its balance and
revision. Those changes are not policy conflicts. Publishing and `merge --keep`
carry the target's allocation; `merge --take` retains the central's allocation.
The receiver preserves its current allocation while holding the profiles lock,
so a profile push never enrolls a machine or moves another machine's balance.
Only allocation verbs change that runtime state.

A supplied `supersedes` must match the receiver's whole current profile, even
when the last writer was the node account. A changed or removed observed copy
requires another check; a node's stamp cannot waive this condition. Explicit
merge resolutions also remain bound to the whole observation, so a scheduled
cycle between resolving and publishing can require another resolution.

Publishing is never scheduled. Gathering can keep the table fresh, but publishing
checks again and never treats a new conflict as permission to overwrite it.

### A profile travelling the other way, and one document shape for both

A profile an administrator wrote on a machine the manager could not reach is
the reason a merge exists, and it has to reach the manager before anybody can
decide about it. It travels the way a day travels and by no new road: a verb
that emits on standard output, a verb that takes in on standard input, and the
manager's cued script carrying the bytes.

```
ssh arch@192.168.1.20 omahouse profile show kid --json \
    | sudo omahouse profile collect 'the kitchen laptop' kid
```

It lands beside that machine's days, under
`/var/lib/omahouse/elsewhere/<machine>/<user>/profile.json`, and **nothing is
decided by taking it in** — the same posture a collected day keeps.

`profile show --json` therefore emits the document wrapped the way
`profiles.json` wraps, and not the bare profile. What that buys is that **the
bytes one machine emits are the bytes another takes in**: the same output goes
into `/etc/omahouse/staged` and `profile apply-staged` reads it unchanged. One
shape for a profile that travels, in either direction, and the `schemaVersion`
comes with it — so a machine newer than the one reading is refused rather than
misread.

It does mean that verb now does two jobs, showing a person their profile and
handing it to another computer. This project has split that pair before: `day`
is separate from `report` because the human view of a ledger is aggregated
differently. It is not split here because the only difference is the envelope,
and a whole verb for an envelope is more house than the tenant needs. **If the
two ever need to differ — a field one should carry and the other should not —
the answer is to split them then**, and never a condition inside one of them.

**Several profiles are reconciled by `profile collect` and a merge; one is
taken by `apply-staged`.** The two say so about each other, because the way to
get this wrong is to use the push in a loop.

### Deciding between two versions of one profile

The other half of the emergency exit. A push stands back from what somebody did
here, and `omahouse profile merge <user>` is where a person is finally asked
about it: what each computer says, against what the manager says.

**Three kinds of answer and not one.** *It differs* would put three different
decisions under one word — somebody asked to choose would read "conflict" where
there is nothing to conflict with:

- **new** — that machine has a profile for somebody this one has never heard of.
  An administrator created it with the manager unreachable, which is exactly
  what the exit is for. The decision is whether to take it in.
- **changed** — both have one and they are not the same. The only real conflict.
  Decided by the rules and never by the stamp: a pushed profile is restamped
  by the machine that takes it in, so two copies that agree on every rule
  differ in `writtenBy` and `writtenAt` as a matter of course, and a merge
  that compared whole documents listed every machine in the house as changed
  forever, with nothing to decide about any of them. The stamp is in the
  table beside the answer, which is where *who wrote each and when* belongs.
- **gone** — that machine says it has none and this one has one. Somebody
  removed it there.

And a fourth thing that is **not an answer**: a machine nothing has been
collected from. It is said out loud and never folded into *no differences*, for
the reason `house` names a computer that has not reported — silence and
agreement read the same and only one of them is a fact. That is also why a
collected document with no profile in it is kept rather than discarded: without
it, a removal and a machine that never answered are the same absence.

`--take` brings a machine's version here, and taking an *absence* takes the
removal. `--keep` prints the document that makes that machine take this one,
carrying what the manager believes is over there inside it as `supersedes`.
**What is applied is what was listed**, and that is mechanism rather than
discipline: a decision made against a version that has since changed lands
nowhere instead of on the wrong one.

### A profile that arrives without anybody at the keyboard

Every other write to `profiles.json` has a person behind it: a terminal with
`sudo`, or the window with `pkexec`, and polkit is the gate. A profile pushed
from a central omahouse has nobody, and no prompt stands in front of it — so
what authorises it is the sudoers line the pairing writes for the node's service
account, and nothing else.

The shape that takes: the manager cues a script that writes
`/etc/omahouse/staged/profile.json` **as that account**, into a directory the
pairing gave it, so putting a document there needs no privilege at all. Then it
runs `omahouse profile apply-staged`, which has root and does the reading.

**The verb takes no arguments**, and the sudoers entry names every one of them.
A rule ending in a wildcard lets the caller append what it likes to whatever the
pattern matched; a rule with nothing after the verb can only be that command or
a refusal. The price is that the path cannot be given on the command line, which
is why it is fixed. What it buys is that what the household may push becomes a
question somebody answers by reading a file.

It does **not** narrow the line that was already there. That one names the
binary and no verb, which was right while the only thing travelling was a day,
and whoever can act as that account still runs any verb as root. What changes is
that policy does not travel by widening it.

The document is shaped like `profiles.json` with one profile in it and is read
by the reader that file gets, so every refusal is the same refusal — the schema
version, an unknown verdict, an action that cannot happen to that kind of
budget, a budget that never resets on a profile for anybody, and a profile for
an administrator. A refusal that lived only in the verbs would have this as its
back door. A symbolic link in the way is turned down before anything is opened:
nothing is to be gained by one today, and that is exactly why nobody would look
at it later, when the wide line is narrower and this is the door.

`writtenBy` ends up being the node's account, because a push has no person. That
is what lets a central tell later what it issued from what an administrator
typed on the machine.

**And it is what stops a push overwriting a profile somebody made here.** The
tiebreak says the most recent written wins, and a manager pushes on a schedule —
so recency favours the central by construction. An administrator who fixes
something by hand at two o'clock would be overwritten by the routine push at five
past, with nobody deciding and nobody told. That defeats what installing omahouse
whole on every machine is *for*: a fix the next cycle erases was not a fix, it
was a loan. It would also destroy the evidence a merge exists to show, so the
merge would list nothing forever and never look wrong, because nothing is the
ordinary answer.

A document carrying `supersedes` is accepted only when that whole profile is
still present, regardless of its last writer. Without a supplied snapshot, a
new profile or a replacement of a node-written profile can be accepted; a
hand-made profile requires the matching snapshot. This is somebody on the
manager having looked at this machine's version and decided. It compares the
whole profile, because a stamp has a second's resolution and a machine edited
twice inside one second would otherwise read as unchanged. Runtime allocation
stays on the receiving machine in every case.

---

**Removal is half the job, not an appendix.** What omahouse puts on a machine
cannot be taken off it by a file list, and every one of those things is a
restriction: the PAM line, `/etc/omahouse/blocked`, the browser policy of §11,
the meter's four files and its key (§5.2), and the enabled service. A restriction
that outlives the program that made it is worse than one that never existed,
because nothing left on the disk knows how to lift it — a name still in `blocked`
locks somebody out of their own machine with no tool to let them back in, and a
policy still in `/etc/chromium` is a site that will not open and a browser that
says "managed by your organisation" with nothing to ask. The profiles and the
ledgers stay, by the convention for a package's data, and the removal message
names them.

### The removal is generated from one declaration, and a test proves it complete

Since §5.2 the package is **not describable by its own file list**: the `.crx`,
the update manifest, the force-install policy, the native messaging manifest and
the signing key are made by a scriptlet, so `pacman -Ql` does not list them and
`pacman -Qkk` does not verify them. What stood in for the file list was a column
of hand-written `rm` lines in `post_remove` — and the VM proved on the first
attempt that the column was already incomplete: the meter's native messaging
host survived `pacman -R`.

That failure mode is silent and on the wrong side, so it is closed structurally
rather than by writing the next list more carefully:

- **`_artifacts` in `packaging/omahouse.install` is the one place any of those
  paths is written.** Four verbs — `take`, `prune`, `borrow`, `keep` — and
  `post_remove` is a loop over the list rather than a column of commands. The
  removal message names the `keep` lines out of the same list, so the sentence
  and the behaviour cannot come apart.
- **The scriptlet has a prefix**, `$OMAHOUSE_PACK_ROOT`, which is the one
  `omahouse-meter-pack` already had. Empty on a real machine; under a root, the
  script writes only there, enables no unit and kills no process — the same
  discipline `mayTouchTheBrowserPolicy` keeps in §11.
- **`check_the_removal_covers_what_the_install_makes` in `tests/test_cli.py`
  runs the real thing.** It stages what `pacman -U` would, runs `post_install`
  (which signs a real key and a real `.crx`), asks two questions of the disk —
  did anything appear that the list does not name, and did anything the install
  made outlive `post_remove` — then takes pacman's own files away and runs
  `post_remove`. It is in `mise run verify`, so it answers on every commit
  rather than on the next VM night.
- **The VM case reads the same declaration.** `PUT_ON_THE_MACHINE` there used to
  be a third copy of the list; the removal half of that case now asks the machine
  about every `take` and `prune` line in the scriptlet.

The property is the one worth naming: **an artefact cannot be added to what the
installation creates without the removal knowing.** Add one to the packer and
leave the list alone, and the gate goes red on the undeclared path; put it on
the list under a verb that cannot remove it, and the gate goes red on the
survivor.

`Restart=always` matters: a stopped daemon is a rule switched off, and an
account without privilege cannot stop a system service.

---

## 10. What this is, and what it is not

**It is not a security boundary.**

The allowlist judges app scopes. A program started from inside a terminal
inherits the terminal's scope — so whoever has a terminal released runs whatever
they like, and all of it counts as terminal time. Do not release a terminal in a
profile that is meant to hold.

The force of a rule is a property of **who the operator is**, not of the engine.
A profile administered by somebody else holds for real. A profile somebody
imposes on themselves they undo whenever they like, and that is fine — it is
discipline, not a prison.

What the model does hold, because none of it depends on the goodwill of the
session: the clock (changing it needs polkit `auth_admin`), the `loginctl`
logout, the counting (the ledger is written by root), and the daemon
(`Restart=always`).

---

## 11. The sites, and a policy that is per machine

The web half of a profile is the app half with a domain where a scope id would
be. Same three parts, same order of reading, same file:

```json
"web": {
  "default": "allow",
  "incognito": "deny",
  "rules": [ { "match": "youtube.com", "verdict": "deny" } ]
}
```

`omahouse web block kid youtube.com` is `omahouse deny kid steam` written
about a site. `omahouse web kid --only-listed` is `profile default --deny`
said in the words §8 asks the studio to use. There is no second model here, and
that is the point: **site filtering is a `Rule` with a different selector**, and
`§2`'s three nouns carried it without a structural change, which is what
[`design.md`](design.md) and
[`design.md`](design.md) both predicted.

A `web` that says nothing is not written into the file at all. That keeps *never
had web rules* and *had them taken away* one state, which is what the whole of
the undoing below rests on.

**The operator's own sentence is not among what this delivers.** A blocked site
shows Chromium's page, which says an administrator blocked it and nothing about
who or why, and omahouse never learns the attempt happened at all: policy blocks
inside the browser and reports nothing out. So there is no message to carry, and
no count of tries for the day's report either. Both wait on the extension of
[`design.md`](design.md), and until it exists this half
blocks well and explains nothing.

### The mechanism is one Chromium managed policy

`chromiumPolicyFor` composes every profile into one document and
`/etc/chromium/policies/managed/omahouse.json` is where it lands: `URLBlocklist`,
`URLAllowlist`, `IncognitoModeAvailability`. Its own file name, beside whatever
else is in that directory, because Chromium merges every file it finds there and
Omarchy's `browser-policy.sh` already owns `policies.json`. Written atomically
and 0644 — the browser reads it as whoever started it, never as root.

The composition is pure and lives in `src/core/WebPolicy.cpp`; the writing is in
`src/sys/Chromium.cpp`. Same line as `Policy` and `Proc`, and for the same
reason: the interesting part is arithmetic over a list of profiles, and it is
proved in microseconds with no browser, no root and no disk.

**Blocking is all this file does.** The managed policy blocks and unblocks sites
and switches incognito off, and nothing else — it never learns that an attempt
happened. Time *per site* is measured by a separate mechanism with no file in
common with this one: §5.2, an extension that reports and a daemon that counts.

The two meet in exactly one place, and it is the composition. Since §5.3 a site
budget that runs out puts its domain in this file for the rest of the day, and
what `chromiumPolicyFor` is handed is the profiles **and** the list of domains
that are out of time right now. That list is not a rule and is never written into
a profile: it is today, worked out afresh every cycle from the ledger, and it
stops being true at midnight. Where the two disagree the clock wins — a site
somebody has spent their thirty minutes on is blocked even where a rule allows
it, and it is never also allowlisted, because Chromium gives the allowlist the
tie and that would turn the block into its opposite.

### The policy is per machine, and that was decided

Chromium's policy directory is a compile-time constant —
`design.md` §3.1 reads it out of `policy_paths.cc` — so there is no
per-account browser policy on Linux short of a managed cloud account, which is
exactly what this project is not. **One file therefore decides for every account
that opens Chromium on the machine, the operator's included.**

That was weighed and taken, not overlooked. `design.md` §3.2 offers
the way round it — the child on Chromium, the operator on Brave — and §3.4 the
heavier one, a `bwrap` bind mount per session, which is refused there for the
same reason §5 refuses eBPF. Neither is built. What is built says the truth once:
`omahouse web` prints it when it writes, `omahouse status` prints it under
`SITES`, the studio prints it on its sites view, and none of the three says it
twice. All three read it out of `webPolicyReach` in `src/core/WebPolicy.h`,
which is where the sentence lives now that there are two front ends that owe
it — a second copy in the window is exactly how the two would come to say it
differently. A tool that re-argues a settled decision every time it is used is a
tool people stop reading.

### Profiles that disagree compose to the most restrictive

A domain opens on this machine only if **every** profile with web rules allows
it. One profile blocking it blocks it for all of them; one profile's
`--only-listed` puts `*` in front of everybody.

The alternative was a precedence — first profile wins, most specific wins, the
child's beats the adult's — and it was refused. A precedence means a rule an
operator wrote, and can still read back in `profile show`, silently not
happening, with nothing in that file to tell them why. Being more restrictive
than one profile asked for is visible the moment somebody opens the site, and the
verb names the profile that overruled them. The surprise is put where it will be
noticed. Same choice as `onerr=succeed` in §2, made in the other direction and
for the same reason: §4.3 of `design.md` shows that the restriction
here cannot lock anybody out of anything, because an incognito window and a
blocked site both leave the session budget running exactly as it was.

A domain that is blocked is never also in the allowlist. Chromium gives the
allowlist the tie, so a domain in both lists is a domain that opens — which
would turn "the most restrictive wins" into its exact opposite in the one case
the rule exists for.

**An allowlist with nothing blocked beside it is not a policy**, and no file is
written for one. `URLAllowlist` is only ever an exception carved out of
`URLBlocklist`. That is not a guess: the browser spike measured the
same trap one policy over, where a `NativeMessagingAllowlist` with no blocklist
beside it let through exactly the host it was meant to keep out.

### Undoing it is the same size as doing it

A parental control that leaves a restriction behind after it is gone is worse
than one that never existed: there is no longer anything on the machine that
knows how to lift it. So there are two ways back and both of them are complete.

**Without uninstalling.** Taking the last block back — or removing the last
profile that had web rules, or the turn of the day on the last site budget that
had run out — **removes** the file rather than emptying it. An
empty managed policy left behind is a machine that still looks managed, and
`chrome://policy` would go on saying "managed by your organisation" over a
document that says nothing. Nothing has to remember to do this: every verb that
writes a profile goes through `saveProfiles`, and `saveProfiles` reconciles the
policy with what the profiles now say. Same discipline as `blocked` in §2, where
the name comes out on its own.

**By uninstalling.** `post_remove` in `packaging/omahouse.install` takes off
everything omahouse put on the machine that a file list cannot: the PAM line, the
`blocked` list it reads, this policy file, and the service. The profiles and the
ledgers stay, and the removal message names both paths — a report is evidence,
and neither of them does anything to the machine once the package is gone.

### The refusal that protects the developer's own browser

`/etc/chromium/policies/managed` is only written by a run whose configuration is
also `/etc/omahouse`. A run pointed at a tree of its own — the end to end suite,
somebody trying a profile out in `$TMPDIR` — writes its rule into its own
`profiles.json`, says the browser policy was left alone, and touches nothing.
`$OMAHOUSE_CHROMIUM_POLICY_DIR` is how the suite proves the file instead.

This is the third refusal of its kind, after §7's cgroup root and configuration
root, and it is the one with the shortest fuse: the suite runs on the
developer's laptop with the developer's Chromium open, and a bug here is
somebody's browser taken away in the middle of an afternoon.

### What it is not

Everything §10 says. Nothing in the browser stops a child opening a different
browser; what stops them is the app allowlist, with the same hole — a released
terminal launches anything. `design.md` §3.5 is the honest reading:
an account allowed to run two browsers is filtered in one of them, and the other
is a door with no lock on it.

---

## Measured, and what it cost

Eight rounds. Each one changed the design, or confirmed one against a machine,
and each is cited by name from the comments in `vm/`.

### Round 1 — one session, twelve seconds, no root

A script sampled a live Omarchy session of one user and wrote what it saw.

**57 distinct executables** in an ordinary session with a browser, an editor and
a terminal open. The design at the time assumed a hand-written baseline of half
a dozen names. Three cases showed why a binary path cannot be the selector:
`/usr/lib/chromium/chromium` under 21 processes, `/usr/share/code/code` under
13, and `/usr/bin/bash` under 26 — sometimes plumbing, sometimes the terminal
the person opened.

The same session had 10 scopes under `app.slice`, Chromium's 21 processes inside
one of them, and `cgroup.kill` present in every scope. Three binaries were
running out of `~/.local/`, which is what makes §10's hole concrete rather than
theoretical.

**Cost:** the selector changed from executable path to scope identity, and
`baseline.json` stopped existing.

### Round 2 — a VM with a real Hyprland on a real seat

An Arch VM with Hyprland 0.56.2, `uwsm` 0.26.7 and `mako`, autologin on tty1,
Hyprland on `vkms`. `loginctl` listed a real `seat0` session. The cgroup layout
matched the host's.

**`cgroup.kill` isolates.** One app scope went from 1 PID to 0 in a single
write; Hyprland kept the same PID before and after.

**Root can notify another user's session.** `systemd-run --uid=… --setenv=DBUS…
notify-send` arrived, and `makoctl list` inside the session showed it.

**The scope depends on how the app was launched.** Two identical `sleep 600`s:
`uwsm app --` produced `app.slice/…/app-Hyprland-sleep-….scope`;
`hyprctl dispatch exec` landed inside
`session.slice/wayland-wm@hyprland.desktop.service`. The second is invisible to
count and untouchable to close, because `cgroup.kill` would take the session
with it. Omarchy wraps its own launches in `uwsm-app --`, so its apps have
scopes; the blind spot is a raw `exec` written by hand in a keybinding, and
whatever is born inside a terminal.

**Autologin defeats `logout`.** `loginctl terminate-user` ended the sessions in
seconds, and the tty1 autologin brought the session straight back up.

**Cost:** `status` gained a number for what it cannot see, and `logout` stopped
being an action on its own.

### Round 3 — flatpak, and the way back in

`uwsm app -- flatpak run --command=sleep …` produced four processes: three with
`exe=/usr/bin/bwrap` and one with `/usr/bin/sleep`, all four in one scope named
`app-flatpak-org.freedesktop.Platform-….scope`. The scope carries the flatpak
app id and groups the tree. The identity model of §5 covers flatpak with no
special case.

The PAM line of §2 was then measured: with the name in
`/etc/omahouse/blocked` and after `terminate-user`, tty1 logged
`pam_listfile(login:account): Refused user kid for service login` and there
were no sessions 25 seconds later. Taking the name out brought the session back
on its own in 20 seconds.

**Cost:** none to the model — this round is what let `logout` ship.

### Round 4 — the launch shim erases the app's name

Found running the real `status` against a real session. Seven scopes on that
machine were called `app-Hyprland-gtk\x2dlaunch-*.scope`, and the unit's
`Description` also said only `gtk-launch` — but the processes inside were
`/usr/share/code/chrome_crashpad_handler`. It was VS Code. The terminal appeared
as `xdg-terminal-exec`.

It is round 3's `bwrap` problem one layer up: an app launched through a shim
inherits the shim's name, and every app launched that way collapses into one id.
Releasing `gtk-launch` in an allowlist releases an unknown and moving set of
programs.

The correcting signal is the **dominant `exe`** of the processes inside the
scope. The two signals fail in opposite situations: the id fails on the shim and
is right on the flatpak; the `exe` fails on the flatpak (`bwrap`) and is right on
the shim.

**Cost:** `evaluate` stays pure — it is handed the `exe` along with the id and
reads no machine of its own. The `exe` is still *configuration* information
first: it keeps the operator from releasing blind, and it is what `status` and
the studio's picker print under each id.

It also decides, in exactly one place and in one direction. A selector may match
a scope by the `exe` **only where the id is not the name of what is running**,
and it can only ever add a match, never take one away. That is what lets a rule
about `code` reach the program the menu opened as `gtk-launch`, and it is what
keeps `org.freedesktop.Platform` matching its own rule while `bwrap` is the
executable of every flatpak alike. A scope that has already said what it is
answers to that name and to no other — otherwise the path would be a selector of
its own, and an allowlist would be quietly wider than what is written in it.

The same round found a second, distinct blind spot: 23 scopes under `app.slice`
whose names the parser refuses (`tmux-spawn-<uuid>.scope`, 71 processes). Unlike
`session.slice`, omahouse sees these and can close them; it has no id to match a
rule with. `status` reports them separately.

### Round 5 — the meter, against what was on the screen

`omahouse-omarchy`, real Omarchy, `Chromium 152.0.7977.82`, 2026-09-04 between
21:26 and 21:28. kid's real session through the SDDM greeter, the meter
force-installed off-store by `ExtensionSettings` with a `file:` update URL, three
sites in a known order, and the screen turned off with the last one still in the
front tab. The whole chain crossed on the first attempt: the `.crx` installed,
the service worker opened the port, Chromium spawned `omahouse meter` as kid,
and the file appeared at `/run/user/1001/omahouse/focus`, `0600 kid:kid`.

| | the screen | the report |
|---|---|---|
| `example.com` | 22s | **22s** |
| `en.wikipedia.org` | 22s | **22s** — as `wikipedia.org` |
| `archlinux.org`, lit | 22s + 12s | **34s** |
| `archlinux.org`, screen off | 22s | **0s** |

```
2026-09-04T21:27:12 kid: 3 apps (chromium, org.chromium.Chromium, udiskie),
                    counting chromium, org.chromium.Chromium, session, using, archlinux.org
2026-09-04T21:27:32 kid: 3 apps (chromium, org.chromium.Chromium, udiskie),
                    counting chromium, org.chromium.Chromium, session, screen-off,
                    archlinux.org not counted
2026-09-04T21:27:54 kid: 3 apps (chromium, org.chromium.Chromium, udiskie),
                    counting chromium, org.chromium.Chromium, session, using, archlinux.org
```

The day, as written: `sites {archlinux.org: 34, example.com: 22, wikipedia.org:
22}`, `presence {using: 78, screen-off: 22}`, `budgets {chromium: 100,
org.chromium.Chromium: 100, session: 100}`. Every site is exact to the tick
against the wall clock; the twenty-two dark seconds are in `presence` and in no
site; and the app budgets gained the whole hundred seconds, screen or no screen,
which is §5 unchanged.

**Cost: none to the model, and one thing learned about the machine.**
`hyprctl dispatch dpms off` is dead on this Hyprland — the dispatcher argument is
Lua now and the working spelling is `hyprctl dispatch 'hl.dsp.dpms("off")'`. The
old form fails with a parse error and an exit code nobody was checking, which
would have turned the screen off in the log and not on the machine.

**What this round did not answer.** A suspend and resume
(`design.md` §9, question 9) was not exercised, and neither was an
incognito window nor a second browser profile. The keepalive over minutes of
silence was measured by the spike and not again here: the quick regime's windows
are seconds, which is what `mise run test:vm:long` exists for.

### Round 6 — the same run, driven by the harness rather than by hand

Round 5's comparison was hand-driven, and a measurement that only exists as prose
is what the old `testing.md` was. So the case was run: `vm/e2e.py --machine
omarchy --case sites`, quick pace, on the same machine. Every claim it makes held
the first time it was asked automatically — the `.crx` installed under policy, the
service worker opened the port, Chromium spawned `omahouse meter` as kid, three
sites came back as three rows within a tick of the wall clock, and the dark
window billed nothing.

**Cost: none to the model, and one bug in the case's own bookkeeping.** The lit
window after the screen came back was opened *before* the dispatch that turned it
on, so two ssh round trips and a poll of dark screen were counted as seconds the
site should have been billed for — and the shortfall read as the daemon losing
time. It is the failure a hand run cannot have, because a hand cannot be two
round trips early, and it is the reason the harness matters: an automated case
has clocks of its own, and they are the first thing to doubt.

### Round 7 — the teeth on the time per site

`omahouse-omarchy`, quick pace, `vm/e2e.py --case a_site_budget`. A one minute
budget on `example.com` with fifty seconds already spent, a real Chromium
browsing until it ran out, and then a `grant`.

| | |
|---|---|
| the domain reached `URLBlocklist` | 12s after the daemon started counting |
| the warning | `grace: Time is up — example.com stops opening in 3 seconds.`, delivered |
| Chromium stopped opening it | 10s after the policy file changed |
| the `grant`, and the file taken off the machine | on the next cycle |
| Chromium opened it again | 9s after that |
| the app budgets over the whole of it | `chromium: 36, session: 36` — untouched |

The window title is what proved the block, and it is the only thing on that
machine that could: §11 means the browser blocks and reports nothing out, so
omahouse never learns the attempt happened and cannot be asked. `Example Domain —
Chromium` before, `example.com — Chromium` while it was shut.

**Cost: none to the model.** What the round did name is the one number a quick
regime cannot promise anything about: Chromium picks a changed managed policy up
through its own file watcher, and the ten seconds above are that watcher and not
omahouse. Both browser-side assertions are waits with the regime's patience on
them, and the seconds they really took are printed on every run.

### Round 8 — the meter arriving in a package, and leaving in one

`omahouse-omarchy`, quick pace, `vm/e2e.py --machine omarchy --case package`,
2026-09-04 around 23:16. Rounds 5 to 7 all measured a machine somebody had
prepared: the harness signed the `.crx` here and mounted it, the `updates.xml`
and the policy over there. This round removes that step from existence. The
package is built from the tree, `pacman -U` installs it, and the scriptlet makes
the key, signs the archive and writes the three files inside that transaction.

**A key born during `pacman -U` is a key Chromium cannot tell from any other.**
That was the open question — the spike proved off-store force-install with a key
that already existed — and it is answered:

```
>>> omahouse: the browser meter is signed for this machine and forced
>>>           into Chromium. Its extension id here is ahdeiepgabnoenfnoebeomaoipdfgkpl
      the archive itself            written by the scriptlet, owned by no package
      the host Chromium spawned     kid  /usr/bin/omahouse meter \
                                    chrome-extension://ahdeiepgabnoenfnoebeomaoipdfgkpl/
      Chromium installed            ahdeiepgabnoenfnoebeomaoipdfgkpl 1.0.0_0
      example.com was in front for 12s and the report says 10s
```

The id in `/etc/omahouse/meter/id`, the id derived again from the key, the id in
the policy, the id in `allowed_origins`, the id in `updates.xml`, the id in the
argument Chromium passed the host and the name of the directory Chromium made in
kid's profile are one id, and no file in this repository contains it.

**Removal, which is the other half.** `pacman -R`: the nine things the package
and its scriptlet put on the machine are gone, `/etc/chromium` holds nothing at
all, the PAM line went from one to zero, `omahouse.service` is `not-found`, and
the extension directory left kid's profile the next time Chromium started.

**And one thing that is not a file, found by this round.** A native messaging
host lives as long as the pipe the browser gave it, so a `pacman -R` while a
window is open leaves `/usr/bin/omahouse meter` running as the child, holding a
binary that no longer has a name, appending sites she is visiting to the file
`post_remove` had just deleted — which comes back within five seconds, on a
machine with no omahouse on it and nothing left to explain either. `post_remove`
now ends it by exact command line, and the case measures that it was the removal
which did so: *`the host at ['3201'] died with the removal, Chromium still open`*.
Nothing else could have closed that pipe.

**The reinstall, priced rather than argued.** The case puts the machine back and
the id moves — `ahdeiepgabnoenfnoebeomaoipdfgkpl` →
`heaplkipdmjbgamhmhkgbgnmhphbpfjk` — because the key went with the removal, as it
must. **The upgrade path does the opposite, twice over:** each of the two runs
that followed began with `pacman -U` over an already installed package, and each
printed back the very id the run before it had left — `heaplkipdmjbgamhmhkgbgnmhphbpfjk`
again, from a key `post_upgrade` found rather than made. The key surviving an
upgrade and not surviving a removal is the whole design, and both halves are
measured.

**Nothing regressed.** The whole of `vm/run.sh --machine omarchy` — all three
browser cases, now against a package rather than a hand-mounted archive — is
3/3 in 227s: the site budget's teeth, three sites against the wall clock with the
dark window billing nothing, and this round.

**Cost: one bug in the harness's own eyes, and it was in two cases.** The check
for "a host is running" was `pgrep -f 'omahouse meter'`, asked over ssh — and
sshd runs the command inside a shell whose *own* command line holds those words.
It answered a pid every time, its own, whether or not a host existed. It is a
green light nobody earned, and `sites_are_counted_only_with_somebody_there` had
been carrying it since round 6; only the second assertion under it kept that case
honest. Both now ask `pgrep -u kid -x omahouse`, which the question cannot
satisfy. The failure that exposed it was the removal half asserting the *absence*
of a host and being told there was one: an assertion that cannot fail is
invisible until somebody writes down its negation.

---

## Tests and the gate

```bash
mise run verify
```

That is the whole local gate, and the versioned pre-commit hook execs
`.scripts/verify.sh` directly rather than going through `mise run`. Install it
once per checkout with `mise run hooks:install`. In order, it runs:

| step | what it refuses |
|---|---|
| `usage-check.sh` | `omahouse.usage.kdl` that does not lint, or a `docs/cli.md` that is not what it generates |
| `test.sh` | the C++ unit suite and `tests/test_cli.py`, the CLI end to end — and, since §9, `packaging/omahouse.install` put on a temporary root and taken off it again |
| `qml-check.sh` | any QML warning at all |
| `studio-check.sh` | the studio driven by keyboard and by mouse, offscreen |
| `shots-check.sh` | a `docs/img` that is not what the studio draws today |

Everything in the gate runs as an ordinary user with nothing installed. The
suite points every root at a temporary tree: no `/etc`, no `/var`, no session,
no root. `shots-check.sh` regenerates the pictures into a scratch directory and
compares byte for byte; one thing it cannot pin is which face `monospace`
resolves to, so a machine with a different monospace font finds the whole set
stale at once, and `mise run shots` is the answer.

### The VM suite

```bash
mise run test:vm            # every case, quick, then shut the machine down
mise run test:vm:long       # the same cases with the long windows
vm/run.sh --keep            # leave it running, for looking at
vm/run.sh --case grace      # one case, by a piece of its name
```

**Deliberately not in the gate.** It starts a libvirt domain, waits for a real
Hyprland session, installs the build and the packaging, then closes processes
and ends a login. It takes minutes, and the kind of regression it catches does
not arrive once an hour.

`vm/e2e.py` proves it is talking to that machine three ways before it imports a
single case: the domain is the one the manifest names and runs from the disk the
manifest names; the address it came up on is not one of this machine's; and
`uname -n` on the far side of the ssh answers with the domain's name. The third
is the one that matters, because it is about the machine that will run the
commands rather than the one that was asked to start. A missing prerequisite is
an explicit `blocked`, never a case quietly skipped.

The case that justifies the whole suite is `session_slice_untouched.py`: a
profile with `enforce: true` and an empty allowlist, asserting some minutes later
that Hyprland, `pipewire` and `systemd --user` are alive and the session is
still active in `loginctl`. §5 says that is structural; this is what proves the
structure is that on a real seat.

The budgets in `vm/manifest.toml` are seconds, not hours, and the clock is the
real one. The arithmetic of hours and of the turn of the day is proved by the
unit suite with an injected `now`; what the VM proves is that the mechanism
fires.

#### The two regimes

**Every duration this suite waits on is in `vm/manifest.toml`, under
`[pace.quick]` or `[pace.long]`, and `--pace` is the only knob that picks
between them.** Nothing in `vm/e2e.py` or `vm/cases/` holds a number of seconds
of its own. That is the whole of the mechanism, and it is one thing rather than
three so that the cost of a run can be read off one file.

| | quick | long |
|---|---|---|
| what it is for | iterating: the smallest window that still proves each case | publishing: the windows that catch what only shows with time |
| how to run it | `mise run test:vm` — the default | `mise run test:vm:long` — explicit, always |
| an app's budget | 8s, `grace` 3s | 45s, `grace` 20s — the one the demonstration VM is really provisioned with |
| the session's | 20s | 90s |
| the allowlist held | 20s | 3 minutes |
| the login refused for | 12s | 60s |
| a site in the browser | 10s each | 45s each |
| a site's budget | 10s, `grace` 3s | 45s, `grace` 20s |
| the screen going dark | by an explicit `dpms` dispatch | by the real idle cycle, 7 minutes of it |

**Which to run when.** Quick after every change, and it is what the default
gives you because it is the one somebody types forty times in an afternoon. Long
before publishing, and before believing a green quick run about anything to do
with the browser: the extension's service worker, the keepalive on the native
port, and Omarchy's own idle cycle are all things that only misbehave after
minutes, and a ten second window cannot tell a worker that lives from one that
was restarted between two ticks.

**One window buys its speed by giving up reach, and it is named rather than
hidden.** `session_slice_untouched.py` asks whether an empty allowlist
*eventually* reaches something it must not, and "eventually" is the question. At
20 seconds it is ten cycles of the daemon refusing every app scope, which is
enough to catch a rule that reaches the wrong tree at once; it is not enough to
catch one that only reaches it on the hundredth cycle. Nothing else here shrinks
in a way that changes what it proves — a budget of eight seconds and a budget of
forty-five prove the same mechanism firing, and the arithmetic that would differ
between them is the unit suite's job.

**No case runs against the developer's machine.** omahouse closes processes and
ends sessions; a run that confused the host with the guest would not be a red
test, it would be a logout in the middle of somebody's afternoon.

#### The second machine, and the one case on it

```bash
mise run test:vm:browser    # vm/run.sh --machine omarchy
```

`omahouse-poc` has no browser, no greeter and a `vkms` framebuffer nothing can
photograph. The meter of §5.2 needs all three of the opposite, so its cases run
on `omahouse-omarchy` — real Omarchy, SDDM, a `bochs` framebuffer, and a real
Chromium. There are three of them: `sites_are_counted_only_with_somebody_there`
proves the number against what was on the screen,
`a_site_budget_that_runs_out_stops_the_site` proves §5.3's teeth against what the
browser then refused to open, and `the_package_puts_the_meter_in_and_takes_it_out`
proves that a household gets any of it — `pacman -U`, a browser that had never
heard of the extension installing it, the site in the report, and then `pacman -R`
taking the archive, the policy, the host manifest, the key and the running host
back off, file by file.

**A run there installs a package and not a binary.** `deploy_on_omarchy` builds
`vm/PKGBUILD` from the working tree into a temporary directory — twenty seconds,
nothing landing in the tree and nothing installed on the developer's machine —
and `pacman -U`s it on the guest. That is what makes the browser half testable at
all: the `.crx`, the `updates.xml`, the force-install policy and the native
messaging manifest are written by the scriptlet inside that transaction, from a
key made inside it, and no line of the harness mounts any of them. Before this,
the harness signed the archive here and copied it over, which meant every green
run was a green run about a machine somebody had prepared.

**The package is the one thing a run there does not put back**, and that is a
better thing to leave than what came before. It is the artefact under test; what
a run must not do is replace it in silence, so the version and the hash of what
was found and of what is left are both printed — that silence is how the machine
came to be carrying an unpackaged build nothing on it could name. What is left
now answers to `pacman -Qo` and comes off with one command, the meter included.

**That machine is not disposable**, and the harness knows it. `reset()` refuses
to run there at all, because it empties `/etc/omahouse` and `/var/lib/omahouse`
and that is the owner's demonstration profile. Instead the run takes a copy of
`profiles.json`, the day's ledger and SDDM's `state.conf` before it touches
anything and puts them back byte for byte afterwards, removes the site block of
§11 — the one browser file no package writes, and the one a run could leave a
domain in — and takes off the `ydotool` it installed to type with. The meter's
four files are deliberately *not* on that list: they are the package's, and
stripping them by hand while leaving the package installed would hand back a
state that no install and no removal ever produces. Which cases exist on which
machine is not a convention either: a case declares `MACHINE` and is not imported
into a run pointed at the other one, so there is no path by which the cases that
end a login can reach the machine that is demonstrated from.

### Docker does not serve here

Without systemd as PID 1 there is no `logind`, no session, no
`loginctl terminate-user` and no system bus. Half of omahouse is exactly that
half.

---

## Out of scope

The v1 scope is four things: user profiles, a program allowlist, time per
program and time per user. The model of §2 carries all of the following without
a structural change when the time comes.

- **Hour windows** — "only between 15:00 and 20:00", with `pam_time.so`.
- **Presets** — `kids`, `focus`, `kiosk` as seeds under
  `/usr/share/omahouse/presets/`.
- **Ending on idle**, and wiping the account at logout (the kiosk case).
- **Focus time** instead of running time. Fairer, and it needs IPC from Hyprland
  inside the fiscalised session, which that session can kill.
- **Site filtering by DNS or proxy** — another problem, another program.
  [`design.md`](design.md) argues that way, per account
  through `nft meta skuid`. It is a **proposal**: none of it is built. What *is*
  built is §11, which blocks sites through the browser's own managed policy
  instead, and pays for it by being per machine rather than per account.
- **Credit that crosses days**, a time bank, time bought with a chore.


## 12. One household balance and scheduled coordination

The contract and operator setup are in
[the household scheduling guide](how-to-schedule-household.md). `Allocation` in
core plans and validates a **statement** per machine: three numbers per budget,
the household's `credit`, the seconds spent `elsewhere`, and how much of *this*
machine's own credit is already inside the first of them, `counted`. Each
machine's allowance is `credit - elsewhere`, plus whatever it has handed over
since `counted` — so what is left over there is the household's balance and an
hour is an hour wherever the person sits.

They are separate numbers and not one cap because they are different kinds of
fact and go stale differently — the same split §11's page draws between policy
and observation. `credit` is a decision with a correct current version and may
be lowered mid-day; `elsewhere` is consumption, only ever grows, and a statement
reporting less than the last one is refused for the reason `collect` refuses
it. A late report under-states `elsewhere`, so a machine allows a little too
much rather than too little.

`counted` is what makes `grant` work on an enrolled machine, and it had to
exist. The allowance was `credit - elsewhere` and a grant is in neither number,
so handing over ten minutes wrote the ledger, printed the same balance back and
changed nothing until the manager next planned — and on a machine that had lost
contact, never. Adding the grant on top was not available either: the manager
folds every grant it collects into `credit`, so the machine would count its own
twice the moment the next statement landed. So the statement says how much of
this machine's credit it has already folded in, taken from the very day the
credit was summed out of, and the machine adds the rest. Exactly once, by
subtraction and never by comparing a grant's clock against a statement's. It
moves in one direction like `elsewhere` and is refused for going backwards for
the mirror-image reason: a statement that unlearns what it counted hands this
machine every grant it has made a second time.

It is required in a document rather than defaulted, which is the difference
between a field and a hole: absent, it reads as *the household has counted none
of this machine's grants*, and every grant ever made here is handed over again.

**A day the household has said nothing about is not a day with no operator.**
The allowance there is exactly what somebody has handed over and no daily
number: the household's allowance stays the household's to give, and a machine
that has lost contact must not start issuing it. That cannot leak time, because
the only thing that raises it is an operator deciding to.

### A pot the whole household shares

One credit and several computers at once is the case this is for, and it is the
one that did not work. The plan was written for a thing that recharges. It
summed `secondsFor`, which is zero for every pot there has ever been, so
`elsewhere` came out zero on every machine and each was told the whole pot; and
it rebuilt `credit` out of today's grants alone, so every refill older than one
night was forgotten by everybody but the computer it was typed on. **Refills
crossed between computers and spending did not**, which is the direction that
gives time away — and it is worse than no household at all, because two machines
were each told the whole pot and every top-up of it.

Neither of a pot's numbers is a day. What it has spent is its running total,
`kept`; what it has been given is the fold beside it, `keptGranted`, which is
where the turn of the date puts a grant so that nothing counts it once per day
it outlived. A collected day is a whole ledger and brings both, so nothing new
had to travel — the plan had to ask. `spentSeconds` and `givenTo` are the two
questions, asked in one place each, because the plan builds `credit` out of one
of them, tells each machine how much of that credit came from itself out of the
same one, and the machine adds whatever it has handed over since.

`givenTo` is operator credit and never a `leave` adjustment. An adjustment is a
machine correcting its own balance and must not feed back into the household's,
and on a pot it cannot exist at all: `leave` answers *how much remains today*
and a pot has no today, so it is refused where it is typed. Left allowed it had
only two wrong answers — expire at the turn of the date and undo itself, or
survive it and recharge the pot every morning.

**And a pot does not go past the guard above.** That guard is about days: the
turn of the date is exactly when the household should have spoken again about a
daily budget, and a machine out of contact must not go on issuing an allowance
that is the household's to give. A pot's statement does not go stale that way.
`credit` is everything the pot has ever been given and `elsewhere` is everything
the other computers have ever spent of it, so a statement from last week is not
expired, it is merely behind on the second number — it under-states `elsewhere`,
and this machine allows a little too much rather than too little, which is the
trade already taken for a late report.

Sending a pot past that guard confused *the household has not spoken about
today* with *the credit is finished*, and the second cannot happen to a pot by
the passage of time. It did not merely take the balance away: with the spending
still carried and the allowance gone, an hour and a half left over came out as
an hour and a half **overdrawn**, so the line dropping at midnight did not stop
somebody spending a pot — it locked them out of one they had not finished.

`omahouse house` and the machines panel read `kept` too, and may: both are
handed **one day per machine**, so a pot's running total is read once. `report`
walks a range and must go on reading the daily map alone, because a pot is
carried into the file of every day it touches and would be counted once per day
there. Somebody who does not see that difference will think `report` is the one
that is inconsistent.

`omahouse day` is on the `status` side of that line and was not, until
2026-09-09. It is the document the household collects and plans from, and it
read today's file plainly: on a morning nobody had written yet it reported every
pot as untouched, the manager summed that into `elsewhere` for the other
machines, and their `apply` refused the statement for consumption moving
backwards — every cycle, until something was spent on the idle machine. Today
now goes through `readDay` like every other verb about now, and a carried pot is
a day with something in it to report; `--date` still reads a past day as it was
written.

**`omahouse limit <user> --session|--budget|--site … --resets never` is what
asks for a pot**, since 2026-09-09. Everything above is reachable from the
command line on the manager, and a pot pushed to a machine arrives the way every
profile does. The window shows a pot as one and edits its number without turning
it back into an allowance; asking for one from the window is `p` on the today
view.

**This is not exclusive.** Two computers told there are thirty minutes left can
both spend them, bounded by the reporting interval and nothing else. The
division into per-machine portions that came before did guarantee exclusivity
and paid for it with the thing the product is for: time reserved on one computer
was time another could not spend. The interval is therefore not a tuning knob;
it is what holds the sum together.

Profile allocation metadata changes enforcement and local balances; it does not
create ledger grants.
A `kind: adjustment` grant from `leave` affects standalone local balance, while
only genuine grants contribute to household credit. Untagged historical grants
remain credit because their original intent cannot be recovered.

The Battery calls `day`, `collect`, `allocation plan` and `allocation apply`.
Its optional manager-owned wrapper supplies Omakure's `Schedule`; there is no
new omahouse scheduler or network daemon. Console API results remain outside
the Health Plane. Readback must match the issued document. All enrolled
observations must be fresh before a new plan, including newly granted time.

Statements are fsynced and atomically replaced before delivery. Revision,
authority, date, account, budget IDs and frozen membership guard replay. A
machine holding a revision the manager has no record of is refused.
CLI profile mutations share a lock across their read/modify/write cycles;
`grant`, `leave` and the watcher share a per-day ledger lock. The Battery also
holds a workspace lock to exclude simultaneous manual and scheduled runs.

Studio reads the same files through core/sys and writes through the existing
privileged CLI boundary. The machines view shows observations, the household
credit and balance, and their age. It never turns an HTTP launch into proof of applied credit.

The host gate exercises arithmetic, refusals and keyboard/mouse interaction.
The Battery's `.scripts/test-sync.py` uses real CLI processes with disposable
file roots for retries, partial delivery, readback, offline peers and overlap.
The VM case `a_battery_schedule_shares_one_balance` exercises actual
Omakure scheduled history, paired HTTP transport, repeated runs, restart, an
offline machine and a subsequent grant in two disposable guests. `a_shared_pot`
does the same for a pot: asked for on the manager with `limit --resets never`,
pushed through the stage over the console API, the merge calling the two copies
the same under two stamps, 300s spent on one machine coming off the other's
balance in 2s and 7s on two runs with the credit unchanged, and a turn of the
date on the spending machine that refills nothing and loses nothing.
