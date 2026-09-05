# What was proposed and not built

Two paths were designed for controlling and measuring a child's browsing. One
was measured and refused; the other was measured and mostly shipped. This
directory is where the parts that **do not exist** are written down, so that no
page in this documentation can leave a reader thinking something is there when
it is not.

Everything that does exist is documented as a feature: the how-to pages say how
to use it, and [`design.md`](../design.md) says how it is built.

---

## The network path — refused

[**Network control per account**](network-control-per-account.md) proposed
per-UID `nftables` rules with a `dnsmasq` filling address sets, and it was
measured in two virtual machines on 2026-09-04. It is not built and will not be
built in that shape:

- **the browser never sends a packet to port 53** — `systemd-resolved` is
  reached over a unix socket by varlink, so there is nothing for `nft` to match,
  and pointing the machine's resolver at ours is a whole-machine decision that
  destroys the per-account property the whole design existed for (§3.1);
- **a set of addresses named `youtube.com` does not contain the site** — the
  video comes from `googlevideo.com`, and 4.5% of a playing video's packets
  landed in the set (§3.2);
- **an idle tab moves bytes anyway**, on 35% of two-second ticks, so counting by
  traffic overcharges the child who walked away and undercharges the one who is
  watching (§3.3).

**Still a candidate, and unmeasured:** reading the TLS `server_name` from the
ClientHello through `nfqueue` (§6), which would fix the naming problem
completely and which nobody has run. Its own §6.5 lists the eight questions that
would decide it, and §6.4 names what it still would not fix: **QUIC** — Chromium
prefers HTTP/3 on UDP 443 to Google, where there is no TLS record for a
TCP-shaped parser to find — and the hand-curation of which domains add up to one
site.

**Still a candidate, and independent of all of it:** `--lock-network` (§7.2), a
profile flag that would set the account's `output` chain policy to `drop` and
open only what is listed. It is the only idea on that page that needs no
resolver, no set and no matcher — and the only one that closes an HTTP proxy on
a port nobody thought to block. It costs a game that negotiates a random UDP
port, file sync, printer discovery and casting to a television, all failing with
no error that names the rule, which is why it would be a flag and would be off.

## The browser path — mostly built, and here is the rest

[`the-browser-half.md`](../the-browser-half.md) is the page the browser half was
decided from, and most of it shipped: the extension, the native messaging host,
time per site, the crossing with presence, blocking a site, incognito, and the
install and removal of all of it. That is
[`design.md` §5.2, §5.3 and §11](../design.md), and where the two disagree,
`design.md` wins.

These parts of it are **not built**:

- **A per-account browser policy of any kind.** Chromium's policy directory is a
  compile-time constant, so one file decides for every account on the machine
  (§3.1). Two ways round it are recorded and neither is built: the child on
  Chromium and the operator on a different browser family (§3.2), which is a
  household agreement rather than a mechanism; and a `bwrap` bind mount giving
  the fiscalised session its own view of the policy directory (§3.4), which is
  refused for the reason `design.md` §5 refuses eBPF — it is a different kind of
  component from anything in the tree, and a launch path the child can edit is a
  wrapper the child can skip.
- **A report line for browsing that no site budget can account for** (§3.5,
  §4.4). An account allowed to run two browsers is measured in one of them, and
  time in the other, or in an incognito window, or in the gap after a resume, is
  browsing that exists and cannot be attributed. The page's own rule is that an
  unseen thing gets reported rather than assumed away, and nothing prints that
  number today.
- **The policies that would close the ways round the meter** (§5.1, §5.5). What
  the install writes is one `ExtensionSettings` entry that force-installs the
  meter, and nothing else: not `NativeMessagingUserLevelHosts: false`, not
  `ExtensionDeveloperModeSettings`, not `DeveloperToolsAvailability`. So the
  hardening §5.5 calls load-bearing is not there — a child can put their own
  `com.omahouse.meter.json` in `~/.config/chromium/NativeMessagingHosts/` and
  the extension will report to it. The reason it was not written is measured
  rather than assumed: `NativeMessagingUserLevelHosts: false` blocks **every**
  per-user host on the machine, Omarchy's own included, and the documented
  escape does not escape — a `NativeMessagingAllowlist` with no blocklist beside
  it blocks nothing at all. What the gap costs is what §8.2 already says it
  costs: the person wins anonymity about which site, and not one minute of the
  day, because the session budget is held by the cgroup walk and the PAM line.
- **Anything about Firefox** (§7). A different extension API and a different
  policy path, and none of the source reading behind that page applies to it.
- **A count of how many times a blocked site was tried**, which is not
  unfinished work but an impossibility: a managed policy blocks inside the
  browser and reports nothing out, so omahouse never learns the attempt
  happened. The same absence is why a blocked site carries no message from the
  operator on it.

Two of its questions are also still unanswered by any measurement: whether the
extension is really dead in an incognito window (§4.2, §9 question 8), and what
a suspend and resume do to the reporting (§5.2, §9 question 9) — the round that
measured the meter end to end deliberately did not exercise either.

---

## Why a page like this exists at all

An earlier `testing.md` in this tree described a `systemd-nspawn` layer that had
never been built, and the claim reached the generated `docs/cli.md` before
anybody noticed. The repository is public, so the distinction between *measured*,
*built* and *argued* has to be visible from the file name inwards. That is what
this directory is for.
