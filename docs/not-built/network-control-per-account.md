# Network control per account — measured, and not taken

> **Nothing on this page is built, and the route it proposed is closed.**
>
> This is a record of a path that was designed, measured in two virtual machines
> on 2026-09-04, and abandoned — kept so that the next person who reaches for
> `nftables` and DNS to control a child's browsing spends an afternoon reading
> instead of a week measuring. There is no code, no verb, no flag, no field in
> `profiles.json` and no screen in the window for any of it.
>
> **What killed it, in two sentences.** The child's browser never emits a packet
> to port 53, so a `dnat` by UID catches nothing (§3.1); and a set of addresses
> resolved from `youtube.com` does not contain the site — 4.5% of the bytes of a
> playing video landed in it (§3.2). Both failures are invisible from outside:
> the ruleset loads, the daemon runs, the set has elements in it, and nothing
> works.
>
> **What is still a candidate, and is honest about being unmeasured.** Reading
> the TLS `server_name` out of the ClientHello through `nfqueue` (§6) would fix
> the naming problem completely and has **never been run** — §6.5 lists the eight
> questions that would decide it, and §6.4 names the two it cannot fix (QUIC, and
> the hand-curation of which domains are a site). `--lock-network` (§7.2), a
> profile flag that would set the account's `output` policy to `drop`, is the one
> idea here that survives the death of the mechanism entirely: it needs no
> resolver, no set and no matcher.
>
> **What replaced the parts that mattered.** Blocking sites shipped, through
> Chromium's own managed policy — [`design.md` §11](../design.md), per machine
> rather than per account, which is the price. Counting time per site shipped,
> through a browser extension that reports the name — [`design.md`
> §5.2 and §5.3](../design.md). Neither is what this page proposed, and both
> exist because this page failed.
>
> §1 to §5 are what a machine did. §6 onwards is argument.

The measurement is the reason to keep this page. §1 to §5 hold numbers that were
paid for with two VMs and a day, and several of them are true of **any** design
that ends in a set of addresses, not only of this one — §4 is the list.

It was written against the model of [`design.md`](../design.md) — `Profile`,
`Rule`, `Budget`, `onExhausted`, the two second cycle — and §8 is where it did
not fit, which is worth reading before proposing anything else that adds a
component to this program.

It also contradicts one line of that page. `design.md`'s own out of scope list
says **"Site filtering, DNS, proxy — another problem, another program."** The
reversal was the first thing to accept or refuse, and the measurement made it a
weaker case than it was: the argument used to be that the bridge was one `nft`
expression and the counting reused the loop that already exists. Half of that
survived. The other half did not.

## The problem that decides the design

Browser policy is per **machine**. `/etc/chromium/policies/managed/` applies to
every account that opens Chromium. A `dnsmasq` on `127.0.0.1` answers every
process on the box. `/etc/hosts` is one file for everybody.

omahouse is per **account**. A profile is one login name, and the whole product
is that a household shares a machine and does not share rules.

The bridge is `nft meta skuid`: the only place in the stack that knows, at the
packet, which user's socket it came out of. It is also the one thing in this
proposal that was measured and paid in full (§2.1). Everything that hangs off it
without going through DNS is still standing; everything that needed the child's
own resolver query is not (§3.1).

There is a second consequence worth stating early, because it runs the other way
from `design.md` §5's blind spot. The app allowlist judges cgroup scopes, so a
program started by a raw `exec` in a keybinding lands in `session.slice` and is
invisible to it, and a program started from inside a released terminal wears the
terminal's identity. **`skuid` has neither hole.** A `curl` in a terminal, a
Flatpak, a binary out of `~/.local/`, and a process the compositor started all
carry the same UID, and the same rules apply to all of them. Network control
would be the one part of omahouse that is not defeated by a terminal.

It is still not a security boundary. §7.3 says what it does not catch.

---

## 1. What was measured, and where

Two machines, both on 2026-09-04. None of it is a measurement of omahouse; all
of it is a measurement of the tools omahouse would stand on.

**The proof of concept, in two virtual machines.** `omahouse-poc`: Arch,
`nftables 1.1.6`, `dnsmasq 2.93`, `bind 9.20.27`, `ufw 0.36.2`, kernel
`7.2.2-arch1-1`, with two real accounts — `arch` (uid 1000, the operator) and
`julia` (uid 1001, the subject). `omahouse-omarchy`: a real Omarchy install with
`chromium 152.0.7977.82`, a real graphical session and a real seat, used for
every question about what a browser and a child actually do. Both machines were
restored and shut down afterwards.

**The development machine**, for two facts about the tools that needed no VM:
§2.5.

The screenshots quoted in §5 are frames from `virsh screenshot` of the
`omahouse-omarchy` session. They live in the run's working notes and are not in
this repository; what they show is described here rather than linked, and the
numbers beside them come from `nft` counters read on the same machine at the
same time.

---

## 2. What held

### 2.1 `meta skuid` separates two accounts, without a reservation

One rule in the `output` chain, both users probing **at the same instant**
(`probe arch & probe julia & wait`), over TCP, UDP and ICMP:

```
=== rule: meta skuid 1001 (julia) ip daddr 1.1.1.1 drop ===
  [arch]  tcp http://1.1.1.1 -> 301   udp dig -> answered   icmp -> 0% loss
  [julia] tcp http://1.1.1.1 -> 000   udp dig -> timed out  icmp -> 100% loss
  counter: meta skuid 1001 … counter packets 9 bytes 608 drop

=== rule flipped to skuid 1000 (arch) ===
  [julia] tcp -> 301   udp -> answered   icmp -> 0% loss
  [arch]  tcp -> 000   udp -> timed out  icmp -> 100% loss
```

Both directions, the same instant, all three protocols. The other user's counter
stayed at `0 0` for the whole of the counting run of §3.3.

**It also holds on an already established connection, and the cut is
immediate.** A download in flight, with a `drop` inserted at second 5:

```
   t | file bytes  | d bytes/s   | d out-packets
   5 |   110702592 |    31244288 | 12275
  ---- DROP inserted ----
   6 |   125476864 |    14774272 | 12289
   7 |   125476864 |           0 | 12289   … motionless to the end
  CURL: exit=28 size=125476876 time=40.001655s
```

Egress freezes inside one second. **But the connection does not die — it
hangs.** `curl` stayed up until its own `--max-time`. `drop` is not a refusal,
it is a silence, and §5 is what that silence looks like to a child.

**Cost to the design: none.** This is the piece the proposal bet on, and it
pays.

### 2.2 One `dnsmasq` can feed several profiles' sets — with commas, not with repeated options

The old page proposed one `dnsmasq` per profile on a loopback port of its own.
One process for the whole machine is simpler, and it works — but **not in the
spelling the old page used**.

Two `--nftset` options naming the same domain: only the **first** one fills. The
second is not an error and not a warning. It disappears.

```
--nftset=/youtube.com/…#kid1_youtube   --nftset=/youtube.com/…#kid2_youtube
  kid1_youtube_v4 = 0
  kid2_youtube_v4 = 1
  (one log line, one set)
```

One `--nftset` per domain, with the sets comma separated, fills all of them:

```
--nftset='/youtube.com/4#inet#omahouse#kid1_youtube_v4,4#inet#omahouse#kid2_youtube_v4,6#inet#omahouse#kid1_youtube_v6'
  kid1_youtube_v4 = 1   kid2_youtube_v4 = 1
```

End to end, with that one resolver serving the whole machine:

```
  julia curl https://www.youtube.com -> 200
    kid1_youtube_v4 = 8   counter kid1_youtube = 129 pkts ; arch_youtube = 0
  arch  curl https://www.youtube.com -> 200
    kid1_youtube_v4 = 8 (filled by the operator's query)
    counter kid1_youtube = 0 ; arch_youtube = 122
  block julia only, against that set:  julia -> 000 ; arch -> 200
```

Two properties worth keeping. The DNS stops blocking and only **names**; the
blocking and the counting are done by the per-UID rule. And the set fills even
when the person who resolved was the operator, so a child who never typed the
name is still blocked, because the address is already there.

**Cost to the design:** the `dnsmasq` command line grows as profiles × domains,
and changing one profile's rules means rewriting the option and restarting the
process — which empties the cache and opens the hole of §4.1. Whether `SIGHUP`
re-reads `--nftset` was **not measured**.

### 2.3 The set is full before the answer leaves

Four rounds, cache emptied with `SIGHUP` before each, polling the set in a tight
loop:

```
  youtube.com:        dig=44ms  visible after 1 extra poll / 3ms
  www.youtube.com:    dig=46ms  visible after 1 extra poll / 4ms
  music.youtube.com:  dig=47ms  visible after 1 extra poll / 3ms
  studio.youtube.com: dig=46ms  visible after 1 extra poll / 3ms
```

The 3 ms is the cost of `nft list set`, not a wait. There is no race between
"resolved" and "connected". Subdomains are caught; a CNAME chain is caught at
its end, because what enters the set is the A record of the answer whatever the
name in the middle was. Elements age out exactly as the set's `timeout` says.

### 2.4 `ufw` is untouched, and untouching

`ufw` installed and enabled the way
`/usr/share/omarchy/install/config/firewall.sh` does it, on the
`iptables-nft` backend, through the whole cycle:

```
                       table  rules  set  counter  nat  julia's DNS
  before ufw             1      3     8     121     1   ok
  ufw enable             1      3     8     121     1   ok
  ufw reload             1      3     8     121     1   ok
  ufw disable            1      3     8     121     1   ok
  ufw enable again       1      3     8     121     1   ok
  systemctl restart ufw  1      3     8     121     1   ok
```

Neither the set's elements nor the counter's value moved, and the per-UID block
kept working with `ufw` active. `ufw` lives in `ip filter` and `ip6 filter`; we
would live in `inet omahouse`. `nft` tables are independent and neither tool
does `flush ruleset`.

**Cost to the design: none.** Worth recording that a third party's
`nft flush ruleset` would take both tables at once, and the sets would come back
empty (§4.1).

### 2.5 Two things that were already true, on the development machine

**An account without privilege cannot create a network interface.**

```
$ id -u
1000
$ ip tuntap add mode tun name omatest0
ioctl(TUNSETIFF): Operation not permitted
```

`capsh --print` on the same shell: `Current: cap_wake_alarm=i`, ambient set
empty. This takes a `tun` VPN out of the picture **before** any firewall rule,
and it is why the design may assume the fiscalised account is not an
administrator. It does not close a userspace proxy that needs no interface at
all — §7.3.

**Omarchy already manages browser policy, with a privileged write.**
`/usr/share/omarchy/install/helpers/browser-policy.sh`, `root:root`, sourcing
`as-root.sh`, owns four Chromium-family managed directories
(`/etc/chromium`, `/etc/opt/chrome`, `/etc/opt/edge`, `/etc/brave`) and two
Firefox-family distribution directories. It hardens the parents, purges anything
in them not owned by root, and installs `policies.json`. So a policy writer does
not have to be invented. And **nothing in Omarchy sets `DnsOverHttpsMode`
today** — `grep -rn DnsOverHttps` over `/usr/share/omarchy`, `/etc/chromium` and
`/etc/brave` returns nothing — so that key is a thing to add, not a thing to
reuse.

---

## 3. What did not hold

Three findings. The first two end the DNS route; the third is a sentence the old
page asserted and the measurement contradicts. They are written out at length
because the point of writing them down is to stop the next person walking the
same way.

### 3.1 The browser never sends a packet to port 53

The old design's centrepiece was a `dnat` by UID:

```
meta skuid <uid> udp dport 53 dnat to 127.0.0.1:5353
meta skuid <uid> tcp dport 53 dnat to 127.0.0.1:5353
```

It catches whoever speaks port 53. **On an Omarchy, almost nobody does.**

```
  hosts: mymachines mdns_minimal [NOTFOUND=return] resolve files myhostname dns
  systemd-resolved: active
  srw-rw-rw- … /run/systemd/resolve/io.systemd.Resolve

  --- curl (glibc getaddrinfo) ---              julia's packets to port 53: 0
  --- chromium, headless, same account ---      julia's packets to port 53: 0
  --- chromium --disable-features=AsyncDns ---  julia's packets to port 53: 0
```

**Zero.** The NSS `resolve` module speaks **varlink over a unix socket** to
`systemd-resolved`. No IP packet, no port, nothing for `nft` to match. And
`systemd-resolved` then raises the query under its own UID, not the child's, so
`skuid` does not catch it either.

The `dnat` is not broken. It works exactly as documented — an explicit
`dig @8.8.8.8` is caught over UDP and over TCP, the operator's own query is not
touched, and `drop` on 853 does stop a DoT client
(`meta skuid 1001 tcp dport 853 counter packets 7 bytes 420 drop`). It is simply
aimed at traffic a browser does not emit. A resolver on a non-standard port
escapes it too, because the rule matches `dport 53` and nothing else.

**The fix was measured, and it is the problem.** Take `resolve` out of
`/etc/nsswitch.conf` and point `/etc/resolv.conf` at our `dnsmasq`, and it
works:

```
  hosts: files myhostname dns ; resolv.conf -> nameserver 127.0.0.1
  julia curl https://youtube.com -> 301 ; kid1_youtube_v4 = 1
  nat counters: dport 53 counter packets 3
```

But it only works if our `dnsmasq` is the **machine's** resolver. With the
resolver on a high port and only the child under `dnat`, the operator loses DNS
entirely (`arch curl https://www.youtube.com -> 000`). So the child's browser
reaches our resolver only when our resolver is everybody's resolver.

**What it costs the design.** Replacing the owner's `systemd-resolved` is a
whole-machine decision, not a per-profile one, and it is a much larger thing to
ask than "install omahouse". It also erases exactly the property that the
opening section uses to justify the whole design: DNS is per machine, and this
was supposed to be the part that was per account. The old §8.4 said browser
policy has that seam; **DNS has it too**, and the old page did not say so.

### 3.2 The set does not contain the site

Set `kid_youtube_v4`, 57 addresses, `drop` inserted, then a navigation to
`youtube.com/feed/trending` on the real Omarchy machine. Forty seconds later:

```
  meta skuid 1001 ip daddr @kid_youtube_v4 counter packets 163 bytes 55456 drop
  counter kid_all { packets 3882 bytes 472760 }

  julia's established connections, and whether they are in the set:
    172.217.114.4   no    142.251.150.119 no    172.217.172.33  no
    172.217.115.4   no    142.250.79.206  no    142.250.79.195  no
    172.217.29.74   no
  set size: 57
```

Seven live connections to Google, **none of them in the set**. The `drop` caught
163 packets; 3882 went past. The video was still playing, and the picture had
changed between frames.

The reason is in the names. `youtube.com` resolves to page addresses; the video
comes from `googlevideo.com`, the thumbnails from `ytimg.com`, the avatars from
`ggpht.com`, and the rest from `gstatic.com`, `googleapis.com` and `google.com`.
Resolved through the same `dnsmasq`, none of them lands in a `youtube` set:

```
  googlevideo.com -> 172.217.30.100    kid1_youtube_v4: 0 elements
  i.ytimg.com     -> 16 addresses      kid1_youtube_v4: 0 elements
  yt3.ggpht.com   -> 16 addresses      kid1_youtube_v4: 0 elements
```

*An honest reservation about this measurement.* On the Omarchy machine the set
was filled by hand with `getent`, not by `dnsmasq` from Chromium's own queries —
because of §3.1 those queries never reach `dnsmasq` at all. Had they reached it,
the addresses would have matched **for the names on the list**, by construction.
What does not change is `googlevideo.com`: that domain is in no `--nftset` for
`youtube.com` and never would be.

**Address ambiguity is not the problem.** Four `youtube` names and two `google`
names through the same resolver gave 25 and 9 addresses with exactly **one** in
common, because Google hands out a distinct address per name inside the same
/24 (`…150.4` for youtube, `…150.119` for google). Precision does not die of
shared IPs.

**It dies of coverage.** During a playing video the share of julia's bytes that
landed in the `youtube` set swung between 18% and 100% per tick, and over the
whole run above it was 175 packets in the set against 3882 in total — **4.5%**.

**What it costs the design.** The two uses split, and they split badly.

- To **block**, this is survivable: cover the whole cluster of domains and the
  site stops. Blocking does not need the accounting to be exact, only complete.
- To **count time**, it is not. Making the set complete means putting
  `gstatic.com` and `googleapis.com` in it, and those serve Google Search, Gmail
  and half the web besides — so the `youtube` set and the `google` set stop being
  distinguishable. "30 minutes of YouTube" would be measuring the page's API
  traffic and not the video, or it would be measuring the whole of Google.

Either way, the per-site domain list becomes permanent hand curation — which is
precisely what the old page claimed `--nftset` would spare anybody.

### 3.3 An idle tab does move bytes

The old page claimed: *"A tab open and idle in the background moves no bytes, so
time per site comes out fairer than time per app."* **That is false as
measured.**

On the clean VM, with no browser, the noise floor is exactly zero — 14 seconds
of nobody touching anything, `0 packets 0 bytes` every tick, and an established
but silent TLS socket likewise. The counter's resolution is not the problem
either: a sustained download moves 5000-plus packets per two second tick on ACKs
alone.

With a real Chromium, an open `youtube.com` tab and nobody at the keyboard, over
40 seconds:

```
  tick | d packets | d bytes | what a 2 s tick would call it
     2 |         1 |      52 | LIVE (2 s debited)
     7 |        20 |   11983 | LIVE
     8 |         2 |     104 | LIVE
     9 |         1 |      52 | LIVE
    10 |         1 |      52 | LIVE
    13 |        11 |    7220 | LIVE
    19 |        17 |   15251 | LIVE
  (the other 13 ticks: 0 / 0)
```

**7 of 20 ticks — 35% — moved.** Forty seconds of nobody doing anything would be
billed as 14 seconds of YouTube. A floor of 1000 bytes per tick would leave 3 of
20, which is 15%: better, and still not zero.

And the error runs the other way too. With the video actually playing, 13 of 15
ticks moved — but **2 did not**, with the picture moving on screen, because a
buffered video plays without the network.

**What it costs the design.** The claim has to go. Time per site is **fairer
than time per app** — 35% against 100% for a minimised browser — but it is not
fair, and the number shown to a parent is wrong in both directions: it
overcharges the child who walked away from an open tab and undercharges the one
who is watching. That error belongs to the browser, not to DNS, so **no change
of mechanism fixes it**. §6 does not fix it either.

### What the three findings mean together

The route through DNS is closed for counting and expensive for blocking. It is
not that a detail was wrong: the child's browser does not emit the query
(§3.1), and the query would not name the traffic anyway (§3.2). Anybody
reaching for `dnsmasq --nftset` plus `dnat` by UID again should read those two
sections before writing a line, because both failures are invisible from the
outside — the ruleset loads, the daemon runs, the set has elements in it, and
nothing works.

The counting half was picked up by [`proposal-browser.md`](../proposal-browser.md),
which leaves the network entirely and asks the browser for the name. **That one
shipped** — the extension, the native host and the crossing with presence are
[`design.md` §5.2](../design.md), and the budget with teeth on it is §5.3.

---

## 4. The smaller defects, which any set-of-addresses design inherits

These were found on the way and are recorded because they survive a change of
mechanism, as long as the mechanism ends in an nftables set of addresses.

### 4.1 A cached answer does not touch the set

```
  1st query (fresh):   kid1_google_v4 = 1
  --- set emptied by hand ---
  2nd query (cached):  kid1_google_v4 = 0
  log:  query[A] … / forwarded … / nftset add … / reply …
        query[A] … / cached google.com is 172.217.172.174   <-- no nftset line
```

So the ruleset **cannot be reloaded from scratch**. Recreating the table blinds
both the blocking and the counting for as long as the TTL the child already
holds — measured at 26 seconds in one run with 21 seconds of TTL left, and the
TTLs `dnsmasq` hands out from a cold cache were 249 s for `youtube.com` and
196 s for `google.com`. Nothing announces it. Either the sets survive a reload,
or a profile change sends `SIGHUP` to `dnsmasq` as well — and even then the hole
only closes when the child asks again.

### 4.2 IPv6 needs a set of its own, and forgetting it fails silently

A `--nftset` without the `4#`/`6#` prefix sends A and AAAA to the same set, and
every AAAA is one error line:

```
  nftset inet omahouse probe_v4 Error: Could not resolve hostname:
      Address family for hostname not supported
```

With the `6#` prefix it fills normally. And an `ip` rule does not see an `ip6`
packet — measured on loopback, the v4 rule blocked and the v6 path connected,
with `meta skuid` matching in both families inside an `inet` table. So every
domain of every profile needs **two** sets and **two** rules.

If IPv6 is forgotten in a house that has IPv6 — which is the ordinary house —
`getaddrinfo` prefers AAAA, the browser connects over v6, and neither the block
nor the counter sees anything: **a block that does not block and a clock that
does not run, without one line of error.** It is the worst failure mode on this
page and it costs nothing to avoid.

*Not measured:* real IPv6 traffic. The libvirt network had no IPv6 and the VM
had only link-local. What is proved is the structure, not the behaviour in a
house with a real v6 route.

### 4.3 A missing set does not stop `dnsmasq` — it only fills the log

With the set absent, `dnsmasq` starts, stays `active`, answers the client
normally, and writes one line per matching query — **even without
`--log-queries`**:

```
  dnsmasq: nftset inet omahouse kid1_youtube Error: No such file or directory
  dnsmasq: reply youtube.com is 142.250.219.206
```

No exit code, no degraded unit, no way for omahouse to know except by reading
the log. A missing set is an always-empty set, is a never-moving counter, is a
child with unlimited time and a block that never blocks — all of it silent. If
this design is ever built, `omahouse status` has to print, per site budget,
**how many addresses the set holds**; an empty set for a site the child has been
using is a defect, not a state.

### 4.4 Rule order decides which lie you get

If the exhaustion `drop` sits **before** the counting rule, the counter freezes
with it (measured: stuck at `12289`). If it sits after, the browser keeps
retrying and the counter keeps billing a site that is already blocked. There is
no order that is simply correct; it has to be chosen and written down.

---

## 5. What the child saw

Chromium in julia's real graphical session, on the Omarchy machine.

**With the `youtube` set dropped, YouTube did not stop.** Forty seconds after
the `drop` went in and the page was loaded, the live webcam was still playing
and the image had changed between frames. The numbers are §3.2's.

**With a clean block — both of `example.com`'s addresses in the set, `drop` in
place, a fresh navigation** — this is the whole of what happened:

| t | what was on screen |
|---|---|
| +20 s | the address bar says `example.com`; **the previous page is still fully drawn**, and its video is still playing; only the tab spinner turns |
| +130 s | the same — the old page's webcam still moving, still spinning |
| +160 s | the error page |

Giving up fell between 130 s and 160 s, consistent with
`/proc/sys/net/ipv4/tcp_syn_retries = 6` (~127 s to `ETIMEDOUT`). The `drop`
counter closed at `packets 150 bytes 9000` — retransmitted SYNs. The message is
Chromium's own:

> **Não é possível acessar esse site**
> **example.com** demorou muito para responder.
> Tente: Verificar a conexão · Verificar o proxy e o firewall
> `ERR_CONNECTION_TIMED_OUT`

**What it costs the design.** Two and a half minutes of spinner is not
acceptable for a child and is indistinguishable from a broken internet
connection. The old page wondered whether the operator's sentence could be put
on the block page; the practical answer comes before that one — **`drop`
produces no block page at all in any useful time**. Worse, the child is not even
looking at a blocked page: they are looking at the page they were on a moment
ago, which keeps playing.

`reject` — with `tcp reset`, or ICMP admin-prohibited — should give
`ERR_CONNECTION_REFUSED` immediately. **It was not measured**, and it should be
before anything else is decided, because it changes the whole of this section.
`URLBlocklist` does give an immediate page, and it is per machine, not per
account (§8.4) — **and that is the one that shipped**, with the per-machine
reach paid for and said out loud rather than worked around
([`design.md` §11](../design.md)).

---

## 6. The candidate path: read the SNI — **and it has not been measured**

Everything from here down is argument. The measurement stops at §5.

### 6.1 What it is

Every TLS connection carries the hostname it is asking for, in the clear, in the
`server_name` extension of the ClientHello — the first thing the client sends,
before any certificate. An `nft` rule in the `output` hook sends the opening
packets of a new connection to a userspace queue:

```
meta skuid <uid> tcp dport 443 ct state new queue num 0 bypass
```

and a matcher on the other side of `nfqueue` reads the name, decides, and hands
back a verdict.

### 6.2 What it would fix, measured item by measured item

| the finding | what SNI does to it |
|---|---|
| §3.1 the browser emits no port 53 packet | irrelevant — no DNS is involved anywhere, `systemd-resolved` can stay exactly as it is, and nothing about the machine's resolver changes |
| §3.2 the set does not contain the site | the connection to `googlevideo.com` **says so**, by name, at the moment it opens; there is no gap between the name and the address |
| §4.1 cache blindness | gone: there is nothing to cache |
| §4.2 IPv6 | one rule per family still, but the name is the same and there is no address family in it to get wrong |
| §4.3 a missing set is silent | gone: no set to be missing |
| attribution | per connection, not per address — the ambiguity of a shared IP cannot arise |
| §5 no block page | the matcher can inject a reset instead of dropping, which should be an immediate `ERR_CONNECTION_REFUSED` — still not a page with the operator's sentence on it |

It also removes the whole-machine decision of §3.1, which was the worst thing
about the DNS route: nothing outside `inet omahouse` is touched, and the rule
stays per UID all the way down.

### 6.3 What it costs

A userspace matcher in the packet path. That is a different kind of component
from anything omahouse has today — `design.md` §5 ends with *"No seccomp, no
eBPF, no AppArmor: a `QTimer`, a read of `/sys/fs/cgroup`, and a write"*, and
this is not that. See §8.6.

### 6.4 What it does **not** fix

Four things, and the first two are the ones that matter.

**It does not fix §3.3.** The idle tab still moves bytes, because that is the
browser's behaviour and not DNS's. Whatever the mechanism, 35% of the ticks of
an untouched tab look live. Time per site stays fairer than time per app and
stays wrong in both directions.

**It does not fix the curation.** SNI names `googlevideo.com` exactly, which DNS
could not — but somebody still has to write down that `googlevideo.com`,
`ytimg.com` and `ggpht.com` are YouTube, and `gstatic.com` and `googleapis.com`
are still shared with the rest of Google and still cannot be attributed to one
site. SNI fixes **recognition**. It does not fix **coverage**, and §3.2 said
coverage was what killed the number.

**QUIC.** Chromium reaches Google over HTTP/3 on UDP 443 by preference, and a
QUIC handshake has no TLS record for a TCP-shaped parser to find. The
ClientHello is inside a CRYPTO frame in the QUIC Initial packet, encrypted under
the well-known initial secrets derived from the Destination Connection ID —
readable in principle, and a second parser with its own reassembly problem in
practice. This is not a corner: it is very likely the transport carrying exactly
the `googlevideo.com` traffic §3.2 found uncounted — and §3.2's own evidence is
a list of *established connections*, with no note of which transport, so it may
have been reading a minority of the traffic without knowing. A cheap answer
exists — `meta skuid <uid> udp dport
443 drop` forces the fallback to TCP — and it is cheap in code and not cheap in
video quality. **Neither the problem nor the fallback was measured.**

**Encrypted ClientHello.** ECH puts the `server_name` inside an encrypted
extension, and where it is deployed the visible name is the provider's public
name rather than the site's. It is a browser policy key to turn off — and a
browser policy key is per machine, not per account, which is §8.4 again.

### 6.5 What has to be measured before this is a plan

In the spirit of the stage 0 that produced §1 to §5, and with the same rule: the
answer changes the page or ends it.

1. **Does an `nfqueue` verdict in the `output` hook keep `meta skuid`?** The
   whole design rests on the packet still being attributable to a UID when the
   matcher sees it.
2. **What fraction of a real YouTube session is QUIC?** Count UDP 443 against
   TCP 443 for the account, on the Omarchy machine, with a video playing. If it
   is most of it, §6.4's QUIC paragraph is not a footnote, it is the design.
3. **Does `udp dport 443 drop` make Chromium fall back to TCP cleanly, and what
   does it cost the video?** Measured, not assumed.
4. **What does a reset injected from the matcher look like on screen?** §5's
   question, answered against a real browser: how long until the page, and what
   the page says.
5. **How does the matcher fail?** `queue flags bypass` means packets pass when
   nothing is listening — fail open, consistent with `onerr=succeed` and with
   `design.md` §4's absent-default. Without it, a dead matcher takes the
   machine's network down. The behaviour has to be chosen, measured, and made
   visible in `status`.
6. **How is a connection counted after its ClientHello?** The matcher sees the
   opening and then the connection is just a 5-tuple. Either every packet keeps
   going to userspace, which is a cost nobody has measured, or the verdict is
   carried forward in `ct mark` and counted by `nft` as before. The second is
   almost certainly right and is entirely unmeasured.
7. **`reject` versus `drop`,** still open from §5, and cheap to answer.
8. **`DnsOverHttpsMode: off`, `EncryptedClientHelloEnabled: false`, and
   `--lock-network`** — three questions the old page's stage 0 asked and this run
   did not reach.

Until at least 1, 2 and 6 are answered, this section is a hypothesis with a
plausible shape, and it should be read with exactly the suspicion §3 has earned
for its predecessor.

---

## 7. What does not depend on the mechanism

Three things that a decision between DNS, SNI and nothing at all does not touch.

> **Most of §7.1 arrived by another road, and it is worth reading for what did
> not.** The shape below — a rule that is `(match, verdict)`, a site default of
> its own, a budget on a site, and a fourth view in the window — is what
> [`design.md` §11](../design.md) and §5.3 shipped, spelled `omahouse web block`
> and `omahouse limit --site` rather than `omahouse site block`, and holding the
> web rules in a `web` object of their own rather than through a `kind` field on
> every rule. Two things in §7.1 are **still not built**: `blockedMessage`, the
> operator's own sentence on a refusal, which cannot be delivered at all through
> a managed policy because the browser never tells omahouse the attempt happened;
> and `lockNetwork`, which is §7.2.

### 7.1 The shape in the profile, and on the command line

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

`kind` defaults to `"app"`, so every profile already on disk reads unchanged. It
exists because a domain and an app id are not distinguishable by looking at
them: `org.freedesktop.Platform` has dots in it and is a Flatpak.

`blockedMessage` is the operator's sentence, not the program's. omahouse writes
"tiktok.com is blocked"; whether the next line is *"Ask me and we can look at it
together"* or *"Not before your homework"* is a thing only the person who set the
rule can write. Absent, the notification is the first line alone. It goes out on
the channel `design.md` §6 already uses and round 2 already measured arriving:
`systemd-run --uid=<uid> --setenv=DBUS_SESSION_BUS_ADDRESS=… notify-send`.
Nothing new.

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

In the studio, a fourth view beside the people, the programs and the day: `4` ·
**the sites**, with the same chips-and-keys table the other three are drawn
from, so no action exists as only a key or only a chip.

One thing §3.2 does change here. A site is not one domain, it is a cluster of
them, so `match` is either a list or a name in a shipped table that maps
`youtube` to its cluster. Either way, somebody maintains that table, and the
proposal should say whose job it is before it says anything else about the
studio.

### 7.2 Two deliberate decisions, with the price beside them

**Denying egress by default is optional, not the default.** `--lock-network`
would set the profile's `output` chain policy to `drop` and open only what is
listed. It buys the death of `ssh -D` and of every SOCKS or HTTP proxy on a port
nobody thought to block — it is the only thing here that closes §7.3's first
hole. It costs a game that negotiates a random UDP port, Steam, file sync,
printer discovery and casting to a television, all failing with no error that
names the rule. Too high a price to charge everybody, too useful to leave out.
It is a flag, and it is off.

**Blocking is the default; allowlisting is for kiosks.** One ordinary page pulls
from ten to twenty domains — §3.2 is the measured proof of that, not a guess —
and an allowlist built by hand breaks pages in ways that look like the network
being broken rather than the rules working. So `sitesDefault` is `allow` on a
new profile, and `--block` turns it into an allowlist for a machine meant to
reach four addresses and nothing else. Same reasoning as `design.md` §4's
fail-open default and as `onerr=succeed` on the PAM line: the recoverable
mistake is the one to make by accident.

### 7.3 What this does not catch

**An HTTPS proxy the child finds on a web page.** TCP to port 443 like
everything else. §6 is the mechanism that would see it by name; `--lock-network`
closes the non-443 variants and nothing else.

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

Six points of friction now. They are the reason this is a proposal and not a
plan, and the last two are new since the measurement.

**8.1 `evaluate` does change, by a little.** Site entries can pass through as
`AppScope`-shaped values for the *debit*, and not for the two other passes. Step
1 of `evaluate` judges every live scope by `verdictFor(scope.id)` and emits a
`Close` for a `deny`, which would name something that is not a cgroup. Step 3's
exhaustion loop emits `Close` for every live scope the budget matches, and for a
site budget the action is `block`, not `Close`. So `evaluate` gains a condition
in each pass, and `Decision::Kind` gains `Block`. Two small changes, not zero.
Saying zero would be the same kind of claim this page opens by warning about.

**8.2 `verdictFor` gains an argument.** A site rule must not judge an app and an
app rule must not judge a domain, so the lookup needs the `kind` as well as the
match. The alternative is a separate `sites` array with its own default and its
own budgets, which leaves `Rule`, `Budget` and `evaluate` untouched and
duplicates the whole of the machinery instead. The `kind` field is the smaller
of the two; it is not obviously the right one.

**8.3 A second default verdict.** `design.md` §2 has one default per profile.
This adds `sitesDefault`, because "only the listed programs run" and "only the
listed sites open" are two decisions an operator makes separately — an app
allowlist with an accidental site allowlist attached to it would be a machine
that reaches nothing. Two defaults is one more concept than the model has today.

**8.4 Browser policy is per machine, and this design is per account.** The
opening section is the whole argument for `skuid`, and then §5 and §6.4 reach for
policy keys that have no notion of a user. `DnsOverHttpsMode: off` lands on the
operator's browser too, and so does `EncryptedClientHelloEnabled: false`, and so
does `URLBlocklist`. **The measurement widened this seam rather than narrowing
it:** §3.1 showed that under the DNS route the *resolver itself* is per machine,
so the seam ran through the middle of the design and not along its edge. §6 is
the version that closes it, which is the strongest argument for §6 and is not
by itself a measurement of anything.

**8.5 The number shown to a parent is wrong, and the model has no way to say
so.** §3.3 measured 35% false-live on an idle tab and a buffered video counted
as idle while playing. `design.md`'s ledger records seconds, `report` prints
seconds, and neither has a notion of a figure with an error bar on it. Either a
site budget's minutes mean something looser than an app budget's minutes — and
the studio says which — or the counting is not shipped. Presenting the two side
by side as if they were the same measurement would be dishonest in the interface
rather than in a document, which is worse.

**8.6 The daemon would stop being allowed to be late.** Today `omahouse watch`
is a two second poller: if a tick is slow, nothing breaks, because
`/sys/fs/cgroup` is still there when it arrives. An `nfqueue` matcher (§6) sits
in the path of every new connection the account opens, and a matcher that stalls
is a network that stalls or a rule that stops applying, depending on `bypass`.
That is a different reliability contract from anything in `design.md` §5, and it
would be the first part of omahouse where being slow is a fault. It is a good
reason to keep the matcher out of the `omahouse` binary and behind its own
socket, and a good reason to be sure §6 is worth it before starting.

---

## 9. The order it would be built in — and none of it has started

**Stage 0b has not run, and nothing after it has been begun.** The order below
is kept as written, because it is the shape of the work if anybody ever picks
this up, and because the first stage is the one that decides whether there is
any work at all.

Stage 0 has run once. It is what §1 to §5 are, and it did what a stage 0 is for:
it ended a design before any code was written for it. The same rule applies to
what replaces it.

**Stage 0b — measure the SNI path, in the same two VMs.** The eight questions of
§6.5, written up the way this run was. Nothing else starts. Questions 1, 2 and 6
can each end this page on their own, so they go first.

**Stage 0c — the smaller open questions**, cheap and worth closing whatever
happens: `reject` versus `drop` (§5), whether `SIGHUP` re-reads `--nftset`
(§2.2), and real IPv6 traffic in a house that has it (§4.2).

**Stage 1** — the ruleset and the matcher as something the operator starts by
hand, with the profile read from `profiles.json`. Still no C++ in `core`.

**Stage 2** — `kind`, `sitesDefault`, `blockedMessage` and `lockNetwork` in
`core`, with the unit suite proving the arithmetic against an injected clock, as
every other budget is proved.

**Stage 3** — the `Net` adapter in `sys` and `block` in the daemon, with the
error of §3.3 named in whatever `status` prints.

**Stage 4** — the verbs, and `docs/cli.md` regenerated from
`omahouse.usage.kdl`.

**Stage 5** — the fourth view in the studio, and its screens added to
[`screens.md`](../screens.md).

**Stage 6** — VM cases, beside the ones in `vm/`. The case that would justify the
suite is the mirror of `session_slice_untouched.py`: the operator's own account
keeps its network while the fiscalised account loses a site. §2.1 is the evidence
that such a case can pass.

A note for whoever writes stage 0b's cases: the run behind this page had to undo
changes to `/etc/nsswitch.conf`, `/etc/resolv.conf`, `systemd-resolved` and
`ufw` by hand, and to delete the `inet omahouse` and `ip omahouse_nat` tables,
because `vm/e2e.py`'s `reset()` knows about none of them.

Until stage 0b has run, §6 is a proposal, and every claim on this page about
omahouse is a claim about something that does not exist.
