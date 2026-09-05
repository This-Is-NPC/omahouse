# How it is built

This page is for contributors. What the commands do belongs in
[the guide](guide.md) and [the command line](cli.md); this one is the model, the
measurements the model was changed by, and the gate that protects it.

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
Budget { id, match, dailyMinutes, onExhausted }

  onExhausted:  close    closes the processes matching the selector
                logout   ends the user's session
                warn     says something, and records it
```

The generalisation that makes three nouns enough: **the session is the budget
whose selector is `*`**. There is no separate concept of user time and app time.
In the code there is no special case for the session.

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

`/etc/omahouse/profiles.json` (0644, root writes, everybody reads) holds the
profiles: `enabled`, `enforce`, `default`, `warnAt`, `grace`, `rules`,
`budgets`. `user` is the only required field.

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
it, and both are written only once there is something to say — a machine with
neither a screen it can read nor a browser extension on it goes on writing
exactly the file it has always written.

The same schema with `default: "allow"` and a `deny` rule per distraction is a
focus profile for an adult. Nothing in the engine changes.

---

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
(`.temp/spike-extension.md` §5). It answered `active` every single time, including
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

**What is deliberately not read is the fiscalised user's compositor.** Root
could connect to `$XDG_RUNTIME_DIR/hypr/$HIS/.socket.sock` and ask `hyprctl
monitors`; it works, and it costs 0.17 ms. It is refused because that socket
lives in a directory the fiscalised user owns, and a child who kills her own
Hyprland and puts her own program on that path is a child feeding bytes to a
JSON parser running as root. The measurement of what that refusal costs, and of
every candidate that lost, is `.temp/poc-presence.md`.

**It is measured and reported, and it acts on nothing.** What an app is billed
is its running time — the decision above, and published — and a screen going
dark does not change it. Presence appears in `status`, in the journal line, and
in the day's ledger beside the budgets. What uses it is §5.2, the time per site,
which is the one number in this program that a dark screen does change.

### 5.2 Time per site, which is measured and does not act

`proposal-browser.md` proposed a Chromium extension that reports the site in
the front tab, and `.temp/spike-extension.md` measured its whole chain on real
Omarchy. This is what shipped of it, and it is the **observing stage** — exactly
what `enforce: false` is for the apps. It counts and it shows the number. There
is no site budget, no warning and no block, and there will not be one until
somebody has looked at a week of the number and decided it is worth having.

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

**Why a file and not a socket.** `proposal-browser.md` §8.1 called this "the
largest single piece of unplanned work": the host is spawned by the browser as
the child (`.temp/spike-extension.md` §1 measured uid 1001, in her session, with
her bus in the environment) and the ledger is root's, so the proposal reached for
a socket in the daemon, with framing and a second writer's worth of validation —
against §3's "no second process, no IPC, no second reader of the profiles".

The file is cheaper because the host is made **stupid** rather than trusted. The
daemon is already root, already ticks every two seconds, and already knows
whether anybody is in front of the screen; it reads this the way it reads
`/sys/class/drm`. No endpoint, no protocol to version, nothing listening, and the
accumulation stays in exactly one place — which is what §3 was protecting.

**The crossing is what makes the number honest.** `.temp/spike-extension.md` §5
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

### The signing key

`proposal-browser.md` §6.3 named the custody of the key as a new problem and did
not decide it. It is decided here.

**The public half is committed**, in `extension/manifest.json` under `key`. That
is what fixes the extension id — `ghiofeehkpcmfcpogcpjlpnfgnfaidjn` — across
rebuilds, so `packaging/omahouse-meter-policy.json` can name it statically and a
rebuilt package installs over the one before it rather than beside it under a new
name. A public key is not a secret.

**In development the private half lives at
`~/.config/omahouse/omahouse-meter.pem`**, outside the repository, 0600, made by
`extension/pack.sh` on first use and moved by `$OMAHOUSE_EXTENSION_KEY`.
`.gitignore` refuses `*.pem` and `*.crx` as a second line of defence, because the
failure is silent: a key committed once is a key somebody else can sign a
force-installed extension with, on every machine that has the policy.

**In production it would not be on a developer's machine at all** — the same
shape as any release signing key, held by whoever cuts releases, passphrase
protected or on a token, reachable by the release job and nothing else. Two
properties matter: losing it means every installed policy names an extension that
no longer exists, and leaking it means somebody else can sign something those
policies will force-install. The development key is explicitly not it.

**The package does not build the `.crx`.** Signing needs a browser and a private
key, and a `makedepends` on chromium for a parental control, with a signing key
reachable by a build, is a bad trade twice over. So `vm/PKGBUILD` ships the shim,
the native messaging manifest and the extension's source; `extension/pack.sh`
makes the archive, its `updates.xml` and nothing else; and `post_remove` takes
the policy, the archive and the manifest off the machine whether or not the
package put them there — the same argument §9 makes about the PAM line, because a
force-install policy left behind is a browser installing an extension whose host
is gone.

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

Every root the program reads moves by environment variable, which is how the end
to end suite runs as an ordinary user with nothing installed. Two of them also
keep a run pointed somewhere else from biting: a `close` is refused unless the
cgroup tree really is `/sys/fs/cgroup`, and a `terminate-user` is refused unless
the configuration really is `/etc/omahouse`. Both refusals name themselves in
the journal.

---

## 8. The studio

One Qt Quick window, two faces, chosen by whoever opened it. Whoever is in
`wheel` gets the operator's: the people under rules, the programs released to
each of them, the day's balance live, and the chips to change all three.
Everybody else gets the subject's, which is the same window with nothing to
press.

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

## 9. Packaging

`packaging/` holds the unit, the polkit policy, the desktop entry, the icon and
the `.install`. The install script creates `/etc/omahouse` and
`/var/lib/omahouse`, adds the PAM line of §2, enables `omahouse.service`, and
takes all of it out again on removal.

**Removal is half the job, not an appendix.** Four things omahouse puts on a
machine cannot be taken off it by a file list, and every one of them is a
restriction: the PAM line, `/etc/omahouse/blocked`, the browser policy of §11,
and the enabled service. A restriction that outlives the program that made it is
worse than one that never existed, because nothing left on the disk knows how to
lift it — a name still in `blocked` locks somebody out of their own machine with
no tool to let them back in, and a policy still in `/etc/chromium` is a site that
will not open and a browser that says "managed by your organisation" with nothing
to ask. `post_remove` takes all four. The profiles and the ledgers stay, by the
convention for a package's data, and the removal message names both paths.

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
[`proposal-network.md`](proposal-network.md) and
[`proposal-browser.md`](proposal-browser.md) both predicted.

A `web` that says nothing is not written into the file at all. That keeps *never
had web rules* and *had them taken away* one state, which is what the whole of
the undoing below rests on.

**The operator's own sentence is not among what this delivers.** A blocked site
shows Chromium's page, which says an administrator blocked it and nothing about
who or why, and omahouse never learns the attempt happened at all: policy blocks
inside the browser and reports nothing out. So there is no message to carry, and
no count of tries for the day's report either. Both wait on the extension of
[`proposal-browser.md`](proposal-browser.md), and until it exists this half
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
happened. Time *per site* is a separate mechanism with no file in common with
this one: §5.2, an extension that reports and a daemon that counts, with no
budget attached to what it counts. Neither half knows about the other, and that
is deliberate — the day this grows a site budget it will be one `Rule` and one
`Budget`, and until then the counting is allowed to be honest about a site the
policy does not block.

### The policy is per machine, and that was decided

Chromium's policy directory is a compile-time constant —
`proposal-browser.md` §3.1 reads it out of `policy_paths.cc` — so there is no
per-account browser policy on Linux short of a managed cloud account, which is
exactly what this project is not. **One file therefore decides for every account
that opens Chromium on the machine, the operator's included.**

That was weighed and taken, not overlooked. `proposal-browser.md` §3.2 offers
the way round it — the child on Chromium, the operator on Brave — and §3.4 the
heavier one, a `bwrap` bind mount per session, which is refused there for the
same reason §5 refuses eBPF. Neither is built. What is built says the truth once:
`omahouse web` prints it when it writes, `omahouse status` prints it under
`SITES`, and neither says it twice. A tool that re-argues a settled decision
every time it is used is a tool people stop reading.

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
for the same reason: §4.3 of `proposal-browser.md` shows that the restriction
here cannot lock anybody out of anything, because an incognito window and a
blocked site both leave the session budget running exactly as it was.

A domain that is blocked is never also in the allowlist. Chromium gives the
allowlist the tie, so a domain in both lists is a domain that opens — which
would turn "the most restrictive wins" into its exact opposite in the one case
the rule exists for.

**An allowlist with nothing blocked beside it is not a policy**, and no file is
written for one. `URLAllowlist` is only ever an exception carved out of
`URLBlocklist`. That is not a guess: `.temp/spike-extension.md` §7 measured the
same trap one policy over, where a `NativeMessagingAllowlist` with no blocklist
beside it let through exactly the host it was meant to keep out.

### Undoing it is the same size as doing it

A parental control that leaves a restriction behind after it is gone is worse
than one that never existed: there is no longer anything on the machine that
knows how to lift it. So there are two ways back and both of them are complete.

**Without uninstalling.** Taking the last block back — or removing the last
profile that had web rules — **removes** the file rather than emptying it. An
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
terminal launches anything. `proposal-browser.md` §3.5 is the honest reading:
an account allowed to run two browsers is filtered in one of them, and the other
is a door with no lock on it.

---

## Measured, and what it cost

Four rounds. Each one changed the design, and each is cited by name from the
comments in `vm/`.

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
`pam_listfile(login:account): Refused user julia for service login` and there
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

**Cost:** `evaluate` still matches by id and stays pure. The dominant `exe` is
*configuration* information — it keeps the operator from releasing blind, and it
is what `status` and the studio's picker print under each id. It is not a
decision criterion.

The same round found a second, distinct blind spot: 23 scopes under `app.slice`
whose names the parser refuses (`tmux-spawn-<uuid>.scope`, 71 processes). Unlike
`session.slice`, omahouse sees these and can close them; it has no id to match a
rule with. `status` reports them separately.

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
| `test.sh` | the C++ unit suite and `tests/test_cli.py`, the CLI end to end |
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
  [`proposal-network.md`](proposal-network.md) argues that way, per account
  through `nft meta skuid`. It is a **proposal**: none of it is built. What *is*
  built is §11, which blocks sites through the browser's own managed policy
  instead, and pays for it by being per machine rather than per account.
- **A budget on a site.** §5.2 counts time per site and §11 blocks sites, and
  nothing joins them: there is no site limit, no warning and no block on a
  number running out. That is the observing stage on purpose, and it is where
  `enforce: false` was for the apps. Joining them is one `Rule` with a domain
  selector and one `Budget`, which is the work
  [`proposal-network.md`](proposal-network.md) §8.1 to §8.3 already costed —
  and it is not done until somebody has read a week of the number.
- **Several machines on a network.** The model carries it; v1 is one machine.
- **Credit that crosses days**, a time bank, time bought with a chore.
