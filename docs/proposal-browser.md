# Proposal — time per site through a Chromium extension · **the reporting half now ships**

> **Most of this page has now shipped, and the split is exact.**
>
> **Built, and described in [`design.md`](design.md) §11** — blocking and
> unblocking a site by managed policy, and switching incognito off. The policy
> file of §3, the `IncognitoModeAvailability` of §4.1, the per-machine reach of
> §3.1 and the composition of profiles that disagree. It has a field in
> `profiles.json` (`web`), four subcommands (`omahouse web block|allow`,
> `omahouse web <user> --all-but-listed|--only-listed`,
> `omahouse web incognito`), a line in `status`, and a `post_remove` that takes
> the file back off the machine. §11 is what to read for what it does; this page
> is the argument it was decided from, and where it disagrees with §11, §11 wins.
>
> **Built, and described in [`design.md`](design.md) §5.2** — the counting half,
> as far as counting. There is an extension (`extension/`), a signing key with
> its custody decided (§6.3 asked and did not answer; design.md's "The signing
> key" answers), a native messaging host (`omahouse meter`), and time per site in
> the ledger, in `status` and in `report`.
>
> **It shipped in a different shape from the one §2 and §8.1 propose, and that is
> the largest disagreement between this page and what exists.** §8.1 says the
> accumulation lives in the host and reaches the daemon through a socket, a
> protocol and a second writer's worth of validation, and calls that "the largest
> single piece of unplanned work on this page". There is no socket. The host is
> stupid: it appends `<epoch> <site>` to a file in the child's own runtime
> directory, and `omahouse watch` — already root, already ticking, already
> holding the presence signal — reads it as one more fact about the machine. The
> file is untrusted input and is read as such. Where this page and design.md §5.2
> disagree, §5.2 wins.
>
> **Still not built, and not promised:** a budget on a site, a warning, a block,
> a count of blocked attempts, and any screen in the studio. §5.2 is the
> observing stage, which is where `enforce: false` was for the apps.
>
> The measurement that decided the shape is `.temp/spike-extension.md`: a
> throwaway extension and a throwaway native messaging host driven on a real
> Omarchy VM. It contradicts this page in three places, which are marked
> **[measured]** where they occur.
>
> **This page was written with no measurement of its own mechanism.**
> [`proposal-network.md`](proposal-network.md) is written on top of a proof of
> concept that ran in two virtual machines; this one was written on top of a
> reading survey — Chromium source at `main`, the policy definition files, the
> signed store builds of two third-party extensions, and read-only checks of
> this machine. That is a weaker kind of evidence and the page is arranged so
> you can always tell which kind you are looking at: §1 to §2 are what the
> source says and what this machine answered, §3 onwards is trade-off and
> argument, and every claim that has not been observed says **not measured** in
> those words. Two rounds of measurement have since happened —
> `.temp/spike-extension.md` for the extension, and `design.md` §11's own VM
> case for the policy — and where they answer a **not measured** here, the
> paragraph says so where it stands.
>
> The precedent this box is here for: an earlier `testing.md` in this tree
> described a `systemd-nspawn` layer that had never been built, and the claim
> reached `docs/cli.md` before anybody noticed. The repository is public now, so
> the distinction has to be visible from the file name inwards.

This page is for the owner to say yes or no to. It is written against the model
of [`design.md`](design.md) — `Profile`, `Rule`, `Budget`, `onExhausted`, the
two second cycle — and where it does not fit that model, §8 says so rather than
inventing a second one.

It exists because the other route failed. `proposal-network.md` §3 measured a
per-account network path and found that it can **block** a site and cannot
**count** one: the child's browser emits no port 53 packet (§3.1), and the set
of addresses does not contain the site anyway (§3.2) — 4.5% of the bytes of a
playing video landed in the `youtube` set. The counting half of that proposal
has to come from somewhere else or not be shipped. This page is the somewhere
else: **ask the browser for the name, because the browser is the only thing on
the machine that knows it.**

It also contradicts the same line of `design.md` that the network proposal
contradicts — *"Site filtering, DNS, proxy — another problem, another program"*
— and it does so on a narrower front. This proposes no filtering at all. It
proposes a reporter.

---

## The problem that decides the design

Two facts pull in opposite directions, and everything below is the shape of the
compromise between them.

**The browser knows the name and nothing else does.** Not the compositor —
`hyprctl activewindow` gives a window title, and "(3) WhatsApp" is not
`web.whatsapp.com`, and a page can write whatever it likes there. Not the
network — measured, twice over, in `proposal-network.md` §3.1 and §3.2. Not the
DevTools protocol — dead since Chrome 136 for the default profile. The name of
the site in the active tab exists in exactly one process, and to get it out you
have to run code inside that process.

**Everything about the browser is per machine, and omahouse is per account.**
`policy::kPolicyPath` is a compile-time constant. There is no per-user policy
directory on Linux, and the code that would read one does not exist. So every
lever this page reaches for — force-install, incognito, developer mode, native
messaging — lands on every account on the machine that opens that browser.

`proposal-network.md` opens with the same seam and had `nft meta skuid` to close
it, at the packet, by UID. There is no `skuid` here. What there is instead is
**the extension deciding nothing**: it opens a port to the native host, the host
runs as whoever opened the browser and knows the `uid`, and for an account with
no profile the host answers "you are not measured" and the extension goes quiet
for the rest of the session. The seam is not closed. It is moved to the one side
of the wall where a user identity still exists.

There is a second consequence, and it runs the other way from
`proposal-network.md` §1. The `skuid` rule was the one part of omahouse that a
terminal does not defeat: a `curl`, a Flatpak, a binary out of `~/.local/` and a
compositor-started process all carry the same UID. **This mechanism has the
opposite property.** The whole per-site channel — the extension, the port, the
native host — runs inside the fiscalised session, as the fiscalised user, which
is precisely the thing `design.md` §1 says the design does not trust. §8.2 is
what that costs.

---

## 1. Where the evidence comes from, and what kind it is

The survey behind this page is `.temp/extension-survey.md`, 2026-09-04. Four
kinds of evidence, in descending order of confidence, and the page marks which
one it is using whenever it matters.

**Chromium source, branch `main`, read 2026-09-04.** The strongest thing here,
and the reason several of the trade-offs below are stated flatly. It also
contradicts the published documentation more than once — the service worker
lifecycle page is from 2023-05-02 and the self-hosting page for Linux is from
**2017** — so where the two disagree, this page follows the source and says it
is doing so.

**The policy definition files**
(`components/policy/resources/templates/policy_definitions/…`), which are the
text that becomes the published policy documentation.

**Read-only checks on this machine** — Omarchy, `Chromium 151.0.7922.173`,
`Hyprland 0.56.2`: `strings` on the Hyprland binary, `busctl --user list`,
`loginctl show-session`, and a read of `/usr/share/omarchy/default/chromium/`
and `/usr/share/omarchy/install/helpers/browser-policy.sh`.

**The signed store builds** of two third-party extensions, downloaded from
Google's update endpoint and read — not their repositories. §6.2 is the reason
that distinction is the whole point.

**What there is none of:** no extension was installed, no browser was launched
under policy, no VM was run for this page, and no line of omahouse was written
or changed. Where `proposal-network.md` can say "a machine did this", this page
can only say "the code says this". Every one of the four questions in §9 exists
because of that gap.

---

## 2. The shape being proposed

An extension that **only reports and only obeys**, with everything else in the
engine that already exists.

It reports the **registrable domain** — not the URL — of the active tab of the
focused window, and reports again when that changes. It obeys an order to close
a tab when the host sends one. It does not count time, does not know what a
profile is, does not know what a budget or a rule is, decides nothing, knows
nothing about idleness, and does not touch the content of any page.

```json
{
  "manifest_version": 3,
  "permissions": ["tabs", "nativeMessaging"],
  "host_permissions": [],
  "background": { "service_worker": "background.js" },
  "key": "…"
}
```

Two permissions, and nothing else. No `<all_urls>`, no `scripting`, no
`webRequest`, no content script, no `host_permissions`, no `idle` (§5.6), no
`storage` (§5.2). The `key` field fixes the extension id across repackagings,
the way Omarchy's own extensions already do.

The accumulation of time lives in the **native host**, which is a subcommand of
`omahouse` in the same pattern as `watch`. The extension holds a segment in
memory at most; if its service worker dies the host closes the segment on
`onDisconnect`. The reasons are §5.2 (a suspend kills the worker), the fact that
`Date.now()` is a wall clock that can jump, and the fact that there already is
exactly one place that holds a day's balance and nobody wants a second.

Where the files would live:

```
/usr/share/omahouse/chromium/omahouse-meter.crx
/usr/share/omahouse/chromium/updates.xml
/etc/chromium/policies/managed/omahouse.json            root:root 0644
/etc/chromium/native-messaging-hosts/com.omahouse.meter.json
```

The protocol, sketched and not designed: the extension opens the port on install
and keeps it open; it sends a message on every **transition** — active tab
changed, URL changed in the active tab, window focus gained or lost — carrying
`{host, t}`; it sends a heartbeat on the two second cycle so the daemon can tell
"the browser closed" from "nobody changed tabs"; and the host answers the first
message with `{measured: false}` for an account with no profile, after which the
extension says nothing for the rest of the session.

Worth stealing from `drmowinckels/cairn`, the one prior art in the survey that
uses native messaging at all: **the host revalidates and discards** every field
that is not on its short list, so a compromised extension cannot leak a full URL
through it. That is `design.md`'s own posture — the privileged half distrusts
the unprivileged half — applied to a wire the project would be adding.

---

## 3. Trade-offs — the reach of policy

### 3.1 There is no per-user browser policy, and there is no way to make one

`components/policy/core/common/policy_paths.cc`:

```cpp
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
const char kPolicyPath[] = "/etc/opt/chrome/policies";
#elif BUILDFLAG(GOOGLE_CHROME_FOR_TESTING_BRANDING)
const char kPolicyPath[] = "/etc/opt/chrome_for_testing/policies";
#else
const char kPolicyPath[] = "/etc/chromium/policies";
#endif
```

A compile-time constant. The `per_profile: true` that appears in some policies'
`features:` is about a *browser* profile, and on Linux the source that fills
that profile is the same file in `/etc`. Per-user policy exists only through the
cloud path — a managed Google account, Workspace — which is exactly what this
project is not.

**No upside.** This is a constraint, not a decision, and every trade-off in this
section is a consequence of it.

### 3.2 The way out is to split by browser family, not by user

Omarchy's `browser-policy.sh` already owns four Chromium-family managed
directories — `/etc/chromium`, `/etc/opt/chrome`, `/etc/opt/edge`, `/etc/brave`
— hardens their parents, purges anything in them not owned by root, and installs
`policies.json`. The same loop serves. So: **the child's browser is Chromium and
the operator's is Brave**, we write to `/etc/chromium` only, and the operator's
browser never sees the extension, the incognito switch or anything else on this
page.

**What it buys.** A clean answer to §3.1 that costs no new machinery at all —
the policy writer already exists and is already privileged, and the split is one
directory versus another. It also gives the operator a browser that is genuinely
untouched, rather than one that is touched and then told to ignore it.

**What it costs.** It is a demand on the household, not on the software: the
operator has to actually keep to a different browser, and the day they open
Chromium they are force-installed too. It also only works for as many families
as there are — two households on one machine with three children is not a
problem this shape has an answer for.

*One caveat on the evidence.* `policy_paths.cc` names the Chromium, Chrome and
Chrome-for-testing paths. That Brave reads `/etc/brave/policies/managed` is
taken from the Omarchy helper's behaviour, not from a source read. **Not
verified.**

### 3.3 It is not a security boundary, and the thing that holds it is the app list

Nothing in the browser stops the child opening the other browser. What stops
them is the app allowlist of `design.md` §5 — the same allowlist, with the same
guarantee, including the same hole: a released terminal launches anything, and
`design.md` §10 already says not to release one in a profile that is meant to
hold.

**What it buys.** No new class of enforcement to build, argue about or get
wrong. The browser path inherits exactly the strength the product already has
and claims exactly the same thing about itself.

**What it costs.** The claim is honest and it is also weak. A child who gets a
terminal released, or who finds a raw `exec` in a keybinding (`design.md` round
2), opens Brave and is unmeasured and unblocked. This is not a defect this page
can fix; it is the product's standing position, restated so nobody reads
"force-installed by policy" as "sealed".

### 3.4 The heavier alternative: mount the policy directory per namespace

If per-family separation is judged too soft, the stronger version is to give the
fiscalised session its own view of `/etc/chromium/policies` — a `bwrap` bind
mount, applied at launch — so that the policy is genuinely per user and the
operator's browser is genuinely untouched even if it is the same binary.

**What it buys.** The one thing §3.2 cannot give: separation by account rather
than by product choice. It would also survive the household changing its mind
about which browser is whose.

**What it costs, and why it is recorded rather than proposed.** Three things.
`design.md` §5 ends with *"No seccomp, no eBPF, no AppArmor: a `QTimer`, a read
of `/sys/fs/cgroup`, and a write"* — a namespace wrapper on every browser launch
is a different kind of component from anything in the tree, and the same
argument that `proposal-network.md` §8.6 makes against an `nfqueue` matcher
applies here. It has to be imposed at launch by something the child does not
control, which means it lands in the launch path — `uwsm`, the desktop entry,
the keybinding — and a launch path the child can edit is a wrapper the child can
skip. And it changes what a scope under `app.slice` looks like, which is the
identity `design.md` §5 counts with.

**Not in the survey, and not measured.** This paragraph is argument only.

### 3.5 Only the fiscalised browser is measured, and `status` has to say so

An account allowed to run two browsers is measured in one of them. The other is
a door with no clock on it. The same is true of a browser started in a way that
escapes the policy directory, and of any browsing done before the extension's
worker comes back after a resume (§5.2).

**What it buys.** Nothing. This is a hole.

**What it costs, and what the project already does about it.** The rule in this
tree is that an unseen thing gets reported rather than assumed away. `design.md`
round 2 gave `status` a number for what it cannot see; its §5 says out loud
that a scope with no id falls through to the default; `proposal-network.md` §4.3
demands `status` print how many addresses a set holds, because an empty set is a
defect and not a state. The same obligation here: **`status` prints, per
profile, that browser time exists which no site budget can account for, and how
much.** If the number is not printed the feature is not shipped, because a
per-site report that silently omits a second browser is worse than no per-site
report.

---

## 4. Trade-offs — incognito

### 4.1 Turning it off is a per-machine act

`IncognitoModeAvailability: 1` disables incognito. Like everything else in §3.1,
it disables it for every account on the machine that opens that browser family.
Under §3.2 that is contained — the operator's Brave keeps its incognito window —
and the containment is exactly as strong as the household's willingness to keep
to two browsers.

**What it buys.** One line, no code, and it closes §4.2 completely.

**What it costs.** An adult who shares the child's browser loses a feature for a
reason that has nothing to do with them, and there is no policy that would give
it back to them alone.

### 4.2 The extension does not run in incognito, and the fix is ChromeOS-only

An extension is not enabled in incognito windows unless the user allows it, and
the user here is the child. The policy that would settle it —
`MandatoryExtensionsForIncognitoNavigation` — has `supported_on: chrome_os:114-`
and is `future_on` for Linux. It cannot be used today.

**Not observed.** The survey read this from the policy note ("This policy
doesn't apply to Incognito mode") and from the `supported_on` field. Nobody
opened an incognito window and watched the extension fail to report. §9 asks for
it.

### 4.3 Incognito is not a hole in the total, only in the attribution

This is the trade-off that decides how seriously to take the rest of this
section, and it comes from `design.md` §5 rather than from the browser. omahouse
counts a **cgroup scope**. An incognito window is a window of the same browser,
in the same `app.slice` scope, under the same profile. The session budget and
the browser's own app budget keep debiting exactly as they did.

So an incognito window does not buy a child one extra minute of screen time. It
buys anonymity of *which site*, inside a total that keeps running.

**What it buys the design.** It makes §4.5 a defensible default rather than a
grudging one, and it means a failure of this whole mechanism degrades to the
product omahouse already is, rather than to nothing.

**What it costs.** Nothing directly — but it is the reason to be suspicious of
any future feature that treats a site budget and an app budget as the same kind
of number. `proposal-network.md` §8.5 raises exactly that objection about the
network path's 35% error, and it applies here for a different reason.

### 4.4 If incognito is allowed, the report has to grow a line for it

A browser in focus with no site reported is browsing time that exists and cannot
be attributed. It has to appear in the report under its own name — unattributed
browsing — rather than being dropped or, worse, spread across the sites that
were reported before and after.

**What it buys.** The same thing §3.5 buys: a report that is honest about its
own blind spot. It also makes the hole visible to the operator, which is a
better enforcement mechanism than most policies — a child whose report shows two
hours of unattributed browsing has told the operator something.

**What it costs.** A concept in the report that has no counterpart in the app
side, and a second thing the studio has to explain.

### 4.5 The proposed default is denied, with an opt-in in the profile

Ship `IncognitoModeAvailability: 1` for the fiscalised family, and let the
profile turn it back on for a household that would rather have the hole than the
restriction.

**What it buys.** The recoverable mistake is the one to make by accident — the
same reasoning as `onerr=succeed` in `design.md` §2, as the absent-`default`
choice in §4, and as `sitesDefault: allow` in `proposal-network.md` §7.2. Here
the recoverable direction happens to be the restrictive one, because §4.3 means
the restriction cannot lock anybody out of anything.

**What it costs.** It is a default that takes something away from an adult who
never asked, on a machine they may also use. §3.2 is the reason that is
acceptable, and §3.2 is a household agreement rather than a mechanism.

---

## 5. Trade-offs — distribution and policy

### 5.1 Off-store install works on Linux, and needs no server, store or account

The restriction that requires a managed machine is real, and the code applies it
**only on Windows and macOS**.
`chrome/browser/extensions/extension_management.cc`, branch `main`:

```cpp
bool ExtensionManagement::ShouldBlockForceInstalledOffstoreExtension(
    const Extension& extension) {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
  …
  return GetHigherManagementAuthorityTrustworthinessForPolicyLoading(profile_) <
         policy::ManagementAuthorityTrustworthiness::TRUSTED;
#else
  return false;
#endif
}
```

`IsForceInstalledInLowTrustEnvironment()` and
`IsGreylistedForceInstalledInLowTrustEnvironment()` sit behind the same `#if`.
On Linux all three return `false`, always. And the update URL may use `file:` —
the policy text says so and `ExtensionDownloader::GetURLLoaderFactoryToUse` has
the branch, binding a `CreateFileURLLoaderFactory` when `url.SchemeIsFile()`. A
`.crx` and an XML under `/usr/share/omahouse/` are enough.

**What it buys.** No developer account, no store review, no web server, no
network at install time, no third party with a veto over shipping a fix. For a
parental-control tool that is not a convenience, it is the difference between
owning the artefact and renting it.

**What it costs, and it is the first thing to test.** **Not verified
empirically.** Documentation and source say it works; nobody ran it. There are
reports on the internet of `file://` force-install failing, almost all of them
on Windows and macOS where off-store force-install is blocked anyway — which
explains the reports without contradicting the code, and is exactly the sort of
explanation that turns out to be wrong. §9 puts this first because it can end
the page on its own.

It is worth being precise about what force-install actually promises, in the
policy's own words: extensions *"which users can't uninstall or turn off through
the Google Chrome interface"*, and then, two paragraphs later, *"some operating
systems make it impossible for Google Chrome to defend robustly against
extensions being modified externally, so this prevention is best efforts"*. It
is a lock on the UI, not on the disk. Which is §3.3 again.

The holes and what closes each, all read from the policy definitions:
`DeveloperToolsAvailability` defaults to 0, which already disallows DevTools on
policy-installed extensions; `ExtensionDeveloperModeSettings: 1` blocks
developer mode and unpacked loading; `NativeMessagingUserLevelHosts: false`
blocks a fake host planted in the user's home (§5.5). And Omarchy's own
`browser_policy_purge_dir` deletes anything in those directories not owned by
root, which helps rather than hinders: our file is root's and survives, a file
the child plants is not.

### 5.2 A native port is a strong keepalive, but a suspend still kills the worker

The known problem with counting time in MV3 is that the service worker dies
after 30 seconds of inactivity, and since Chrome 114 opening a port no longer
resets that timer. For a native messaging channel it does.
`extensions/browser/api/messaging/message_service.cc`:

```cpp
  // Keep the opener alive until the channel is closed.
  channel->opener->set_should_have_strong_keepalive(true);
  channel->opener->IncrementLazyKeepaliveCount(Activity::MESSAGE_PORT);
```

Unconditional — no policy, no allowlist. And `extension_message_port.cc` says
what "strong" means:

```cpp
        should_have_strong_keepalive()
            ? content::ServiceWorkerExternalRequestTimeoutType::kDoesNotTimeout
            : content::ServiceWorkerExternalRequestTimeoutType::kDefault,
```

`kDoesNotTimeout`. Neither the 30 second idle nor the 5 minute ceiling. This is
also documented behaviour since Chrome 105, not a reverse-engineering find.

**What it buys.** The hardest part of writing a time-tracking extension under
MV3 is solved by the same thing the architecture needed anyway. No offscreen
document trick (the `Reason` enum has no value that means "I want a clock", and
using `TESTING` in production would be a false declaration), no `chrome.alarms`
scaffolding, no timestamp bookkeeping in `chrome.storage`.

**What it costs.** Three things. It was read on `main`, not on the 151 installed
here, and it was not run — §9. If the native host dies the port closes and the
worker dies on the normal timers, so reconnecting on `port.onDisconnect` is
mandatory rather than tidy. And **suspending the machine kills the worker
anyway**: crbug 40273015, open and unresolved, says that after a suspend of more
than 30 seconds the worker is dead on wake and does not come back on its own.
Any partial that lives only in the extension's memory is gone. That is the
direct reason the accumulation lives in the host (§2), and it leaves a residual
hole that host-side accumulation does not close: after a resume, nothing reports
until something wakes the worker, so per-site attribution can be blind while the
app budget keeps debiting. §4.4's unattributed line is where that time lands.

Keeping a worker alive indefinitely is also against the intent of MV3, and the
migration guide says the Chrome team *"reserves the right to take action against
those extensions"* outside enterprise and education use. Here that is not a
threat with teeth — publishing to the Web Store was never the plan (§5.1) — but
it is one more reason it never can be.

### 5.3 `tabs` shows the user "Read your browsing history", and `activeTab` will not do

`tabs` is what unlocks reading `url`, `pendingUrl`, `title` and `favIconUrl`.
Its permission warning is **"Read your browsing history"**. `nativeMessaging`
shows **"Communicate with cooperating native applications"**.

`activeTab` is not an option: it grants access only *after a user gesture*. It
is the right permission for Omarchy's `copy-url`, which is fired by a shortcut.
It is the wrong one for measuring continuously, because there is no gesture.

**What it buys.** The two warnings are honest descriptions of what the product
does, and they are much better than the alternative: `host_permissions:
["*://*/*"]` would give the same access to the URL with the far worse warning
"read and change all your data on all websites", plus an injection capability we
would not use.

**What it costs.** Every account on the machine that gets this force-installed —
including the operator's, under §3.2's failure mode — sees a browser telling
them that something reads their browsing history. That is a true sentence about
a program whose whole privacy claim is that it does not keep the URL, and there
is no way to make the browser say the narrower thing. **There is no permission
that grants only the domain**: `url`, `pendingUrl`, `title` and `favIconUrl` all
pass the same gate. The narrowing in §5.4 is our discipline, and the browser has
no way to vouch for it.

### 5.4 The privacy boundary is the registrable domain, and it is ours to keep

The extension converts to a hostname inside the service worker and never stores
or sends a path. The host then discards every field outside its short list, the
way `cairn` does. Nothing in the wire format, the ledger or the report has room
for a URL.

**What it buys.** The report an operator reads says `youtube.com`, not
`youtube.com/watch?v=…`. For a tool a household points at a child, the
difference between "how long" and "what exactly" is most of the difference
between supervision and surveillance, and building the narrow one first means
the wide one is never one small change away.

**What it costs, and one detail the survey does not resolve.** Chrome 153
(2026-08-25) added `browser.publicSuffix`, which collapses a hostname to eTLD+1
correctly. **This machine has Chromium 151.** So either the extension ships
without it and sends a hostname, and the host does eTLD+1 with its own table, or
the product carries a minimum browser version. The first is better for reasons
that have nothing to do with the version — it puts the parsing on the privileged
side that already distrusts the unprivileged one — but it means omahouse
acquires a public suffix list, which is a data file with an update cadence.
**Not in the survey as a trade-off; noticed writing this page.**

### 5.5 The native host manifest goes in `/etc`, and Omarchy's do not

Ours goes to `/etc/chromium/native-messaging-hosts/com.omahouse.meter.json`,
root-owned, with `NativeMessagingUserLevelHosts: false` over the top — otherwise
the child writes their own `com.omahouse.meter.json` into
`~/.config/chromium/NativeMessagingHosts/`, pointing at a script of their own,
and the extension dutifully reports to it.

**Omarchy installs its own native messaging host manifests in
`~/.config/chromium/NativeMessagingHosts/`** — the user's directory, writable by
the user. That is fine for what they do and would be a hole for what we do,
which is the whole point of writing it down.

**What it costs, and this is a collision the enunciation did not name.**
`NativeMessagingUserLevelHosts: false` is a policy, so it is per machine, so
setting it **disables Omarchy's own native hosts** — for the child and for
anybody else on that browser family. Something that works today would stop
working, silently, as a side effect of installing omahouse. There is a
documented escape (`NativeMessagingAllowlist` / `NativeMessagingBlocklist` were
among the policy files read), and there is a cruder one (move Omarchy's
manifests to `/etc` too, which is not our file to move). Neither has been
tested. §9 asks for it, because "we broke the desktop's own extensions" is the
kind of regression that gets a tool uninstalled.

**What it buys.** Without it, the entire chain is decorative: force-install,
DevTools lockout and developer-mode lockout all fall to a five-line JSON file in
the child's own home directory.

### 5.6 `chrome.idle` is useless here, and idleness has to come from the compositor

The path, top to bottom: `idle_api.cc` → `ui/base/idle/idle.cc` →
`idle_linux.cc` → `WaylandScreen::CalculateIdleTime()`, which tries
`org_kde_kwin_idle` (KWin), then the `org.gnome.Mutter.IdleMonitor` D-Bus
service, and then:

```cpp
  NOTIMPLEMENTED_LOG_ONCE();
  // No providers.  Return 0 which means the system never gets idle.
  return base::Seconds(0);
```

On this machine:

```
$ strings /usr/bin/Hyprland | grep -i idle
ext_idle_notification_v1
ext_idle_notifier_v1
get_idle_notification
get_input_idle_notification
```

Only `ext-idle-notify-v1`, which Chromium does not speak — the Wayland host
directory in `main` contains `org_kde_kwin_idle.*`,
`org_gnome_mutter_idle_monitor.*` and `zwp_idle_inhibit_manager.*` and nothing
else. `org_kde_kwin_idle` landed in Chromium in 2021 and wlroots dropped that
protocol for `ext-idle-notify-v1` afterwards, so this cut applies to **Hyprland,
sway, and every wlroots compositor**. The lock state is no better: nothing owns
`org.freedesktop.ScreenSaver` on this bus, so `IsScreenSaverActive()` is also
`NOTIMPLEMENTED_LOG_ONCE(); return false`. And Omarchy's `chromium-flags.conf`
sets `--ozone-platform=wayland`, so there is no XWayland escape without changing
a system default.

**What it costs.** The hope that `chrome.idle` would solve the idle-tab problem
for free is gone, and with it the `idle` permission.

**What it buys, and it is more than it costs.** The division this forces is the
one the design wanted anyway: **the extension says which site is in the active
tab of the focused window; omahouse decides whether that counts.** omahouse is a
native process that already runs beside the compositor, already reads cgroups,
and can speak `ext-idle-notify-v1` directly — a thirty-line client. Putting
idleness in the daemon also means one idleness for the whole product rather than
one for the browser and another for everything else, and it keeps the extension
free of a content script, which is what `time-tracker-4-browser` needs to solve
the same problem inside the browser.

**Partly inferred.** The `strings` output and the source reads are facts. That a
running Chromium therefore answers `"active"` forever has not been observed. §9
asks for it, because it is a five-minute check.

### 5.7 `windows.onFocusChanged` lies on Linux, and the lie has to be damped

The `chrome.windows` reference, updated 2026-05-14:

> On some Linux window managers, `WINDOW_ID_NONE` is always sent immediately
> preceding a switch from one Chrome window to another.

So moving between two Chromium windows produces a spurious "the browser lost
focus". Untreated, the measurement records gaps that did not happen, and — worse
for a design where the host closes segments — closes and reopens a segment on
every window switch.

**What it costs.** A debounce: wait some tens of milliseconds and re-query
`windows.getLastFocused()` before believing the loss. That is state, and state
in a service worker is state that a restart loses.

**What it buys.** `windows.onFocusChanged` needs **no permission at all**, and
it gives the browser's own answer to "am I in front", which is the one thing the
compositor route (§5.6) would otherwise have to infer from a window title.
Having both signals — the compositor's and the browser's — is a cross-check
neither one gives alone.

**Not verified on Hyprland.** The documentation warns about "some Linux window
managers"; which ones is not stated and this one was not tested.

---

## 6. Trade-offs — the ecosystem

### 6.1 Nine extensions were examined and none of them serves

The survey has the reason for each. In short: nobody uses native messaging
except a 7-star v0.1.0 project; the two that can report to a local process do it
over loopback HTTP with the address in a text field on an options page that
belongs to the person being measured; the best of them ships its own limits and
its own blocking screen, which is the second competing budget the brief already
ruled out; and one of them counts per blocking *set* rather than per domain and
its public repository is the Firefox build.

**What it buys.** Confidence that writing one is not a failure of research.
`time-tracker-4-browser` (MIT, 980 stars, published in the store in September
2026, store build checked against the repository and clean) is a **better
extension than the one this page proposes**, in everything except the three
things omahouse actually needs: report over a channel the child cannot switch
off, report on the engine's clock rather than on a five-minute file backup, and
bring no budget of its own.

**What it costs.** Everything a third party would have maintained is now ours:
the manifest, the state machine, the packaging, the signing key, and the
compatibility with whatever Chromium does next.

### 6.2 Two of the popular ones are disqualified by conduct, read from the shipped build

Both findings come from the **signed build published in the store**, not from
the repository, which is where the difference lives.

**StayFocusd** (v4.6.12, id `laankejkbhbdhmipfmgcngdelahlfoji`) contains
`stayfocusd.st-panel-api.com` with `/v1/web/upload`, the strings `page_views`
and `chatbot_chats`, and, minified, `uploadWebUsage:!0` — **usage upload on by
default**. Three content scripts match `*://*/*`: `ad-finder.js`,
`ai-link-modifier.js` and `gen-ai-collector.js`, with references to
`chatgpt.com`, `claude.ai`, `gemini.google.com`, `perplexity.ai`, `grok.com`,
`copilot.microsoft.com` and `chat.deepseek.com`. It also embeds Google Analytics
and Bugsnag. On a child's machine that is the opposite of the product. *(Who
owns the listing today: **not determined**. The telemetry is from the binary;
the corporate attribution was not checked.)*

**Web Activity Time Tracker** had its **listing sold**. The original
repository's README says so: *"the Chrome extension has been sold. So I am not
responsible for any changes to the functionality"*. The published build (v2.5.0,
id `hhfnghjdeddcfegfekjeihfmbjenlomm`) contains `track.mediaintelliview.com` and
`chrome-watt.mediaintelliview.com` — third-party infrastructure **absent from
the MIT repository**. The survey records these as third-party tracking endpoints
and does **not** characterise them as an ad network; that is as far as the
evidence goes.

**What this buys the argument.** It is the exemplary case, and it generalises:
**a free licence on the source code does not protect the distribution channel.**
An extension can be MIT, audited, and replaced next Tuesday by whoever bought
the listing, on every machine that has it, silently. For a parental-control
product that is not a risk to be weighed against convenience — it is a category
error.

**What it costs.** It rules out the whole ecosystem, including the good one,
which is §6.1.

### 6.3 The signing key becomes our problem

Self-hosting means we produce and sign the `.crx`, and the `key` in the manifest
fixes the id that the policy names.

**What it buys.** No third party can revoke, replace or review the artefact
(§5.1), and the id is stable across rebuilds.

**What it costs.** A private key that has to exist somewhere, be reproducible
enough that a rebuilt package keeps the same id, and not end up in the
repository. Losing it means a new id, which means every installed policy file
names an extension that no longer exists. Leaking it means somebody else can
sign something the policy will force-install. **Not in the survey** — the survey
notes the key fixes the id and does not discuss custody. This is argument.

**Decided, and not the way this section assumed.** Both paragraphs above take for
granted that the id is stable across machines and that somebody therefore holds a
key. `design.md` §5.2, "The signing key, and why there is not one", takes the
other branch: the key is made during `post_install`, on the machine, so the id is
per machine and there is nothing to hold, revoke or leak beyond the one machine
it belongs to. The `key` in the manifest is gone, no file in the tree names an
extension id, and `post_remove` deletes the key — which is what makes a reinstall
a new extension, the one real cost, affordable only because this extension keeps
no state.

### 6.4 Manifest V2 is over, and it was not our choice

`ExtensionManifestV2Availability.yaml` is `deprecated: true` with `supported_on:
chrome.*:110-138`. Chrome 138 turned MV2 off for good in July 2025, the policy
that re-enabled it was removed in 139, and the store purged the remainder in
August 2026. This machine runs 151.

**What it costs.** Nothing that could have been had otherwise. It is recorded
because it removes an entire class of "but what about" — any candidate extension
still on MV2 is dead regardless of how good it looked, and any design that leans
on a persistent background page is not available.

**What it buys.** It makes §5.2 load-bearing rather than optional, which is
worth knowing before somebody proposes a design that assumes a background page.

---

## 7. Trade-off — scope

**Chromium only, for now.** Firefox is a different extension API and a different
policy path — Omarchy's helper already owns two Firefox-family *distribution*
directories, which are not the same thing as `/etc/chromium/policies/managed`,
and none of the source reading behind this page applies to it.

**What it buys.** One browser family to get right, and the family Omarchy ships
as the default.

**What it costs.** A household on Firefox gets nothing from this page, and
§3.5's unmeasured-door disclosure covers them by default. It also means the §3.2
split — child on Chromium, operator on Brave — is a policy the product can
express, while "child on Firefox" is not, so the browser choice stops being free
for the fiscalised account. Whether the Firefox work is a second document or a
second half of this one is not decided here.

---

## 8. Where this does not fit the model, honestly

Five points of friction. The first two are new since the brief and are the two
that could stop the page.

**8.1 The native host runs as the child, and the ledger is written by root.**
`design.md` §4 puts the day's ledger in `/var/lib/omahouse/<user>/<date>.json`,
0644, written by root; §10 lists "the counting (the ledger is written by root)"
among the things that hold precisely because they do not depend on the goodwill
of the session. But a native messaging host is **spawned by the browser**, as
the user who opened the browser. So the subcommand that §2 puts the accumulation
in cannot write the ledger. It has to hand its segments to `omahouse watch`,
which means a socket, a protocol and a second writer's worth of validation — and
`design.md` §3 says, as a feature, *"No second process, no IPC, no second reader
of the profiles."* This is not fatal and it is not free: it is the same class of
cost as `proposal-network.md` §8.6's matcher, arriving for a different reason.
The alternative — the extension talks to a root-owned socket directly, with no
native host at all — throws away the `connectNative` keepalive of §5.2, which is
the thing that made MV3 tractable.

**8.2 The whole channel lives inside the untrusted half.** The extension is the
child's browser and the host is the child's process. A child who kills the host
closes the port; the worker dies on the normal timers; site reporting stops.
This is detectable — the daemon sees the browser's scope alive with no channel
open, and §3.5 already requires it be said out loud — and it is not defeatable.
Compare `proposal-network.md` §1, where `nft meta skuid` matches at the packet
and a terminal does not help: this mechanism has the terminal problem and the
network one did not. §4.3 is the reason it is survivable: what a child wins by
killing the host is anonymity, not minutes.

**8.3 `evaluate` and `Budget` need the same changes the network proposal needs,
for the same reason.** A site budget's selector is a domain and not an app scope
id, so `verdictFor` needs to know which kind it is looking at, `Decision::Kind`
needs something that is not `Close`, and the profile needs a second default.
`proposal-network.md` §8.1 to §8.3 works all of this through and this page
proposes nothing different — which is worth saying explicitly, because it means
**the two proposals share a core change and should not be costed twice**. If
either ships, the model work is done for both.

**8.4 The number is still not the number.** `proposal-network.md` §3.3 measured
that an idle tab moves bytes 35% of the time and concluded that time per site is
fairer than time per app and still wrong in both directions. This mechanism
fixes the *naming* problem completely — the extension says `youtube.com` because
the tab says `youtube.com`, with no address, no set, no coverage question and no
QUIC — but the presence problem is the same problem: a tab in the front with
nobody at the keyboard reads as live. §5.6 moves the answer to the daemon rather
than solving it, and the daemon's answer is compositor idleness, which is
coarser than "this person is reading this page". So `proposal-network.md` §8.5
stands word for word: either a site minute means something looser than an app
minute and the studio says which, or the counting is not shipped.

**8.5 It adds a component in a language the project does not have.** `src/core`,
`src/sys`, `src/cli`, `src/studio` — four qmake subdirectories of C++ and QML,
one gate, one test suite. This adds JavaScript, a packaging step that produces a
signed archive, and a policy file, none of which `mise run verify` knows how to
check. Whatever `usage-check.sh` and `shots-check.sh` are to the CLI and the
studio, there is currently no equivalent for "the extension still reports what
the host expects", and an untested reporter feeding a tested engine is a tested
engine reporting confidently wrong numbers.

---

## 9. What has to be measured before a line is written

The survey lists four. They are the right four, they are all cheap, and they are
all VM work. Confirmed, in its order, with what each one decides:

1. **Does `.crx` + `updates.xml` + a `file:` update URL in the policy actually
   install, and resist uninstalling?** §5.1 is read from source and
   documentation and has never been run. If this fails there is no distribution
   story and the page ends.
2. **Does the native port hold the service worker alive for ten minutes?** §5.2
   was read on `main`, not on the 151 that is installed. If this fails the
   extension needs alarms, storage and a reconnection design, and the cost
   estimate in §2 is wrong.
3. **Does `chrome.idle` answer `"active"` forever on Hyprland, as §5.6
   predicts?** Five minutes of work to turn a strong inference into an
   observation.
4. **Does `windows.onFocusChanged` lie when switching between two Chromium
   windows on this compositor?** §5.7. It decides whether the extension needs a
   debounce and how much state it carries.

The first two decide the path; the last two decide the shape.

Five more that this page adds, and why each earns its place:

5. **Where does the native host actually run, and how does it reach root?** §8.1
   is the largest single piece of unplanned work on this page, and part of it is
   measurable rather than arguable: what user the browser spawns the host as,
   with what environment, what working directory and what `stdin`. Cheap, and it
   should go **before** 3 and 4, because it can change the architecture rather
   than the shape.
6. **Does `NativeMessagingUserLevelHosts: false` break Omarchy's own native
   messaging hosts?** §5.5. They live in the user's home directory, which is
   exactly what the policy exists to forbid. If it breaks them, the answer is
   `NativeMessagingAllowlist`, and that has not been tested either.
7. **Can the child escape the policy with a second browser profile?** The survey
   marks this open: policy in `/etc` is read by every profile of the same
   binary, but `BrowserAddPersonEnabled` and `ProfilePickerAvailability` were
   not checked. A one-command answer that could quietly undo §5.1.
8. **Is the extension really dead in an incognito window?** §4.2 was read from a
   policy note, not observed. It decides whether §4.5's default is a real
   protection or a superstition.
9. **What does a suspend and resume do to the reporting?** §5.2's residual hole.
   Specifically: after wake, does anything bring the worker back, how long is
   the gap, and does the host see a clean `onDisconnect` or a hung port. This is
   the one that decides whether §4.4's unattributed line is an edge case or a
   daily occurrence.

Until at least 1, 2 and 5 are answered, everything on this page is a proposal
with a plausible shape, and it should be read with exactly the suspicion that
`proposal-network.md` §3 earned for its own predecessor: the documentation was
current, the source was read correctly, and the mechanism still did not do what
the page said it would.
