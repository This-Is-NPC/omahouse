# Proposal — network control per profile · **nothing here is built**

> **This page is a proposal, not a description of omahouse.**
>
> None of it exists. There is no code, no verb, no flag, no field in
> `profiles.json`, no screen in the studio, and no measurement of omahouse doing
> any of this. Every sentence below about what omahouse would do is a sentence
> about something that has never run.
>
> What *has* been measured is listed in §2, and it is measurements of the
> **tools** — the kernel's refusal, `nft`, `dnsmasq`, Omarchy's own installer —
> on the development machine on 2026-09-04. That the parts exist is not evidence
> that the thing works.
>
> The precedent this box is here for: an earlier `testing.md` in this tree
> described a `systemd-nspawn` layer that had never been built, and the claim
> reached `docs/cli.md` before anybody noticed. The repository is public now, so
> the distinction has to be visible from the file name inwards.

This page is for the owner to say yes or no to. It is written against the model
of [`design.md`](design.md) — `Profile`, `Rule`, `Budget`, `onExhausted`, the
two second cycle — and where it does not fit that model, §8 says so rather than
inventing a second one.

It also contradicts one line of that page. `design.md`'s own out of scope list
says **"Site filtering, DNS, proxy — another problem, another program."** The
argument for reopening it is §1 and §5: the bridge is one `nft` expression, and
the counting reuses the loop that is already there. That reversal is the first
thing to accept or refuse.

---

## 1. The problem that decides the design

Browser policy is per **machine**. `/etc/chromium/policies/managed/` applies to
every account that opens Chromium. A `dnsmasq` on `127.0.0.1` answers every
process on the box. `/etc/hosts` is one file for everybody.

omahouse is per **account**. A profile is one login name, and the whole product
is that a household shares a machine and does not share rules.

The bridge is `nft meta skuid`: the only place in the stack that knows, at the
packet, which user's socket it came out of. Everything else in this proposal
hangs off that one expression.

There is a second consequence worth stating early, because it runs the other way
from `design.md` §5's blind spot. The app allowlist judges cgroup scopes, so a
program started by a raw `exec` in a keybinding lands in `session.slice` and is
invisible to it, and a program started from inside a released terminal wears the
terminal's identity. **`skuid` has neither hole.** A `curl` in a terminal, a
Flatpak, a binary out of `~/.local/`, and a process the compositor started all
carry the same UID, and the same rules apply to all of them. Network control
would be the one part of omahouse that is not defeated by a terminal.

It is still not a security boundary. §7 says what it does not catch.

---

## 2. What was measured, and what it is evidence of

Four things, on this development machine, 2026-09-04. None of them is a
measurement of omahouse.

### 2.1 An account without privilege cannot create a network interface

```
$ id -u
1000
$ ip tuntap add mode tun name omatest0
ioctl(TUNSETIFF): Operation not permitted
```

`capsh --print` on the same shell: `Current: cap_wake_alarm=i`, ambient set
empty.

This is what takes a `tun` VPN out of the picture **before** any firewall rule,
and it is why the whole design is allowed to assume the fiscalised account is not
an administrator. It does not close a userspace proxy that needs no interface at
all — see §7.

### 2.2 `nft` matches by UID

```
$ nft --version
nftables v1.1.6 (Commodore Bullmoose #7)
```

`man nft`, META EXPRESSIONS, the table of meta keys:

```
skuid   UID associated with originating socket   uid
```

Documented, not inferred. Nothing here has been run against a live ruleset;
doing that is stage 0 of §9.

### 2.3 `dnsmasq` feeds an nftables set

```
$ dnsmasq --version
Dnsmasq version 2.93
Compile time options: … conntrack ipset nftset auth DNSSEC loop-detect inotify dumpfile
```

`nftset` is compiled in. `man dnsmasq`:

```
--nftset=/<domain>[/<domain>...]/[(6|4)#[<family>#]<table>#<set>[,…]
    Similar to the --ipset option, but accepts one or more nftables sets to add
    IP addresses into. These sets must already exist.
```

"These sets must already exist" is a sequencing constraint on whoever writes the
ruleset: the `nft` table comes up before `dnsmasq` starts.

### 2.4 Omarchy already manages browser policy, with a privileged write

`/usr/share/omarchy/install/helpers/browser-policy.sh`, 4451 bytes,
`root:root`, sourcing `as-root.sh`. It owns four Chromium-family managed
directories:

```
/etc/chromium/policies/managed
/etc/opt/chrome/policies/managed
/etc/opt/edge/policies/managed
/etc/brave/policies/managed
```

and two Firefox-family distribution directories,
`/usr/lib/firefox/distribution` and `/opt/zen-browser/distribution`. It hardens
the parents (0755, `root`, not a symlink), purges anything in them not owned by
root, and installs `color.json` and `policies.json`. On this machine
`/etc/chromium/policies/managed/color.json` is present.

Two things follow. The mechanism omahouse would need already exists and is
already privileged, so this proposal does not invent a policy writer. And
**nothing in Omarchy sets `DnsOverHttpsMode` today** — `grep -rn DnsOverHttps`
over `/usr/share/omarchy`, `/etc/chromium` and `/etc/brave` returns nothing — so
that key is a thing to add, not a thing to reuse.

---

## 3. The four pieces

### 3.1 `nft` rules, per UID

A sketch. It has not been loaded, and `<uid>` is the fiscalised account's:

```
table inet omahouse {
    set kid_youtube_v4 { type ipv4_addr; timeout 1h; }
    counter kid_youtube { }

    chain output {
        type filter hook output priority filter; policy accept;
        meta skuid != <uid> return
        tcp dport 853 drop
        udp dport 853 drop
        ip daddr @kid_youtube_v4 counter name "kid_youtube"
    }
}

table ip omahouse_nat {
    chain output {
        type nat hook output priority dstnat;
        meta skuid <uid> udp dport 53 dnat to 127.0.0.1:5353
        meta skuid <uid> tcp dport 53 dnat to 127.0.0.1:5353
    }
}
```

The `dnat` on **every** port 53, not a block on the ones we do not like: a
child who types a different resolver into the network settings gets sent to the
profile's own `dnsmasq` anyway. The `drop` on 853 closes DNS over TLS, which is
a port of its own and so is cheap to close. DNS over HTTPS is on 443 with
everything else and is §7's problem.

`meta skuid != <uid> return` first, so the operator's own traffic is untouched
by the rest of the chain.

### 3.2 One `dnsmasq` per profile

An instance per fiscalised account, on a loopback port of its own, holding that
profile's domain rules: `--address=/<domain>/` for what is blocked,
`--nftset=/<domain>/inet#omahouse#<set>` for what is on a clock.

The set membership is the point. `--nftset` puts into the set **every address
that domain resolves to**, so a CDN that answers with a different IP every ten
minutes stays accounted for without anybody maintaining a list of addresses.

The instances are per profile and not per machine because the rules are per
profile. Whether that is one `dnsmasq@.service` template or one process with
several `--listen-address` is an implementation question, not a design one.

### 3.3 Browser policy, for the two things DNS cannot do

`DnsOverHttpsMode: off` (and the Firefox equivalent) so the browser asks the
system resolver rather than talking to a DoH endpoint on 443. Without it the
whole of §3.1 and §3.2 is bypassed by a browser default.

`URLBlocklist` for the blocked domains, so a blocked site is a page that says it
is blocked rather than a connection error with no explanation. This is a second
copy of the same list, written for the browsers only; the DNS answer is what
holds for everything else on the machine.

Written through the helper of §2.4, or through something shaped like it. It is
already root, already hardened, and already covers the five browsers.

### 3.4 The notification

The channel `design.md` §6 already uses, and round 2 already measured arriving:
`systemd-run --uid=<uid> --setenv=DBUS_SESSION_BUS_ADDRESS=… notify-send`. A
site that runs out of time, or a site that was refused, is said in the same
place and the same way a program that runs out of time is said. Nothing new.

---

## 4. The shape in the profile, and on the command line

Mirroring what is there: a rule is `(match, verdict)`, and what matches no rule
falls to a default verdict.

```json
{
  "user": "kid",
  "default": "deny",
  "sitesDefault": "allow",
  "lockNetwork": false,
  "blockedMessage": "Ask me and we can look at it together.",
  "rules": [
    { "kind": "app",  "match": "chromium",   "verdict": "allow" },
    { "kind": "site", "match": "tiktok.com", "verdict": "deny"  }
  ],
  "budgets": [
    { "kind": "site", "id": "youtube", "match": "youtube.com",
      "dailyMinutes": 30, "onExhausted": "block" }
  ]
}
```

`kind` defaults to `"app"`, so every profile already on disk reads unchanged.
It exists because a domain and an app id are not distinguishable by looking at
them: `org.freedesktop.Platform` has dots in it and is a Flatpak.

`blockedMessage` is the operator's sentence, not the program's. omahouse writes
"tiktok.com is blocked"; whether the next line is *"Ask me and we can look at it
together"* or *"Not before your homework"* is a thing only the person who set
the rule can write. Absent, the notification is the first line alone.

The verbs, mirroring `allow`, `deny`, `limit` and `profile default`:

```bash
sudo omahouse site block kid tiktok.com
sudo omahouse site allow kid youtube.com --limit 30m
sudo omahouse site limit kid youtube.com --limit 45m
sudo omahouse profile sites kid --block          # allowlist: only the listed
sudo omahouse profile sites kid --allow          # denylist: all but the listed
sudo omahouse profile message kid "Ask me and we can look at it together."
sudo omahouse profile network kid --lock-network
```

`site allow --limit` is the same sugar `allow --limit` is, and for the same
reason: the rule and the clock are one thought at the moment somebody is
configuring. The spellings above are not settled; the shape is.

In the studio, a fourth view beside the people, the programs and the day: `4` ·
**the sites**, with the same chips-and-keys table the other three are drawn
from, so no action exists as only a key or only a chip.

---

## 5. Time per site

This is the part that matters most and reuses the most.

1. `dnsmasq --nftset` puts every address `youtube.com` resolves to into the set
   `kid_youtube_v4`.
2. One `nft` rule counts that UID's traffic to that set into the named counter
   `kid_youtube`.
3. The two second loop that already exists reads the counter each tick. If it
   has moved since the previous tick, the site is live, and the budget is
   debited two seconds.

That is the same shape as "a budget with at least one live scope matching its
selector" — `design.md` §5, step 4. A site that is being used is a counter that
is moving, in exactly the way an app that is being used is a scope with
processes in it.

**A counter that moves is a better signal than a process that exists.** A tab
open and idle in the background moves no bytes, so time per site comes out
fairer than time per app, where a minimised browser burns budget for as long as
it is running. It is a nice property and it is not free: a page left playing
audio still counts, which is correct, and a page that polls in the background
counts too, which is arguable.

What it costs the code:

| | |
|---|---|
| one new adapter | `Net`, in `src/sys` beside `Proc` — reads the counters, holds the previous tick's values, and presents the moving ones |
| one new action | `block` on `onExhausted`, beside `close`, `logout` and `warn` |
| one new field | `kind` on a rule and on a budget, defaulting to `"app"` |

The arithmetic does not change. The debit is still once per budget per tick, the
`warnAt` marks, the grace window, the ledger, the turn of the day and the grants
are all untouched, and none of them learns what a domain is. If the adapter
presents a live site as an entry shaped like an `AppScope` — id the domain,
alive when the counter moved — then `anyLiveScopeMatches` reads it with no
change at all. §8 is where that claim stops being true.

The previous tick's counter values live in the daemon's memory. A restart costs
one tick of one site, which is two seconds, and is not worth a file.

---

## 6. Two deliberate decisions, with the price beside them

### 6.1 Denying egress by default is optional, not the default

`--lock-network` would set the profile's `output` chain policy to `drop` and
open only what is listed — DNS to the profile's own resolver, 80, 443, and
whatever else is named.

**What it buys.** `ssh -D` is dead, and so is every SOCKS or HTTP proxy on a
port nobody thought to block. It is the only thing in this proposal that closes
§7's first hole.

**What it costs.** A game that negotiates a random UDP port does not connect.
Steam does not connect. File sync, printer discovery, casting to a television
and anything else that talks on a port the operator did not predict stop
working, with no error message that names the rule.

That price is too high to charge everybody, and too useful to leave out. It is
a flag, and it is off.

### 6.2 Blocking is the default; allowlisting is for kiosks

An allowlist of domains is the stronger rule and the wrong default. One ordinary
page pulls from ten to twenty domains — fonts, a CDN, an analytics host, an
image host, a video host — and an allowlist built by hand breaks pages in ways
that look like the network being broken rather than the rules working.

So `sitesDefault` is `allow` on a new profile: the listed sites are blocked,
everything else runs. `--block` turns it into an allowlist, and that mode is for
a machine meant to reach four addresses and nothing else.

Same reasoning as `design.md` §4's fail-open default, and the same reasoning as
`onerr=succeed` on the PAM line: the recoverable mistake is the one to make by
accident.

---

## 7. What this does not catch

Stated as limitations, because they are limitations.

**An HTTPS proxy, or DoH on 443.** A proxy the child finds on a web page, or a
DoH endpoint the browser policy did not cover, is TCP to port 443 like
everything else. Closing it means reading the SNI from the ClientHello and
matching the name there, which is a different mechanism with a different cost —
and one that TLS Encrypted Client Hello is in the business of removing. Later
work, if at all. `--lock-network` closes the non-443 variants of this and
nothing else.

**Another device.** A phone on the same wifi is not this machine, and nothing on
this machine can see it.

**A bootable USB stick.** A different operating system on the same hardware.

**Learning the administrator password.** Every rule here is written by root, and
whoever is root writes their own rules.

The last three are not software problems and there is no version of omahouse
that solves them. `design.md` §10 already says the force of a rule is a property
of who the operator is; this changes nothing about that.

---

## 8. Where this does not fit the model, honestly

Five points of friction. They are the reason this is a proposal and not a plan.

**8.1 `evaluate` does change, by a little.** §5 claims the site entries can pass
through as `AppScope`-shaped values with nothing in the core moving. That is
true of the debit, and false of the two other passes:

- Step 1 of `evaluate` judges *every* live scope by `verdictFor(scope.id)` and
  emits a `Close` for a `deny`. A site entry falling through to the app default
  verdict would produce a `Close` naming something that is not a cgroup.
- Step 3's exhaustion loop emits `Close` for every live scope the budget
  matches. For a site budget the action is `block`, not `Close`.

So `evaluate` gains a condition in each pass, and `Decision::Kind` gains
`Block`. Two small changes, not zero. Saying zero would be the same kind of
claim this page opens by warning about.

**8.2 `verdictFor` gains an argument.** A site rule must not judge an app and an
app rule must not judge a domain, so the lookup needs the `kind` as well as the
match. The alternative is a separate `sites` array with its own default and its
own budgets, which leaves `Rule`, `Budget` and `evaluate` untouched and
duplicates the whole of the machinery instead. The `kind` field is the smaller
of the two; it is not obviously the right one.

**8.3 A second default verdict.** `design.md` §2 has one default per profile.
This proposal adds `sitesDefault`, because "only the listed programs run" and
"only the listed sites open" are two decisions an operator makes separately —
an app allowlist with an accidental site allowlist attached to it would be a
machine that reaches nothing. Two defaults is one more concept than the model
has today.

**8.4 Browser policy is per machine, and this design is per account.** §1 is the
whole argument for `skuid`, and §3.3 then reaches for a mechanism that has no
notion of a user. `DnsOverHttpsMode: off` lands on the operator's browser too,
and so does `URLBlocklist` unless the blocklist is kept empty and the blocking
left to DNS. This is the seam in the design. It is survivable — turning DoH off
for every account on a family machine is a defensible thing to do, and the
per-account part is still DNS and `nft` — but it should be accepted knowingly.

**8.5 `blockedMessage` may have nowhere to land in the browser.** The
notification channel of §3.4 can carry it, and that is measured. Whether any
Chromium or Firefox policy key puts the operator's own sentence on the block
page has **not** been checked, and this page does not claim it does. Until
somebody measures it, the block page is the browser's own wording and the
operator's sentence goes out as a notification.

---

## 9. The order to build it in

In the spirit of the stages this project already used, and with the same rule:
the measurement comes before the code it would justify.

**Stage 0 — a PoC in a VM, before a line of C++.** Nothing else starts until
this has run and been written down the way `design.md`'s four rounds were. It
answers, on a real machine with two real accounts:

1. Does `meta skuid` in the `output` hook actually separate two accounts'
   traffic, for TCP and for UDP?
2. Does the `dnat` of port 53 survive a user editing their own resolver, and
   does the `drop` on 853 stop a DoT client?
3. Does `dnsmasq --nftset` populate the set as addresses change, and do the
   elements age out with the set's `timeout`?
4. Does a named counter move for the account's traffic and stay still for the
   operator's, at a resolution a two second tick can read?
5. What does `DnsOverHttpsMode: off` do to a browser that was already using
   DoH, and what does the block page say?
6. What breaks under `--lock-network`, measured rather than guessed.

Any of the six answering badly changes this page or ends it.

**Stage 1** — the ruleset and the resolver as a shell script the operator runs
by hand, with the profile read from `profiles.json`. Still no C++.

**Stage 2** — `kind`, `sitesDefault`, `blockedMessage` and `lockNetwork` in
`core`, with the unit suite proving the arithmetic against an injected clock, as
every other budget is proved.

**Stage 3** — the `Net` adapter in `sys`, and `block` in the daemon.

**Stage 4** — the verbs, and `docs/cli.md` regenerated from
`omahouse.usage.kdl`.

**Stage 5** — the fourth view in the studio, and its screens added to
[`screens.md`](screens.md).

**Stage 6** — VM cases, beside the ones in `vm/`. The case that would justify
the suite is the mirror of `session_slice_untouched.py`: the operator's own
account keeps its network while the fiscalised account loses a site.

Until stage 0 has run, this document is a proposal and every claim in it about
omahouse is a claim about something that does not exist.
