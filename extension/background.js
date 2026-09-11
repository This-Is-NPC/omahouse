// The eye. It reports the site in the front tab, and it does nothing else.
//
// docs/design.md §5.2 is the whole of the argument for why this file is as small
// as it is. The browser is the only thing on the machine that knows the name of
// the site somebody is looking at -- not the compositor, whose window title is
// whatever the page decided to write there, and not the network, which
// `docs/design.md` §3.2 measured putting 4.5% of
// a playing video's bytes in the set named after it. So the name has to come
// from in here. Every
// other question is answered outside, by the daemon that is already root:
//
//   what a profile is                    the daemon reads /etc/omahouse
//   whether anybody is in front          the daemon reads the kernel's DRM
//                                        attributes and root's own logind
//   how much of the day has gone         the daemon writes the ledger
//   whether any of it is allowed         nothing here; see below
//
// Nothing in this file acts, and that is not because nothing acts. A site
// budget that runs out does shut the site -- by the daemon rewriting Chromium's
// managed policy, which the browser enforces itself. So the teeth exist and
// none of them are here: this file is never told a verdict, never asked for
// one, and could not carry one out if it were. It reports a name and stops,
// which is what keeps it small enough to read in one sitting and worth
// force-installing into somebody else's browser.
//
// -- what is deliberately not in the manifest ---------------------------------
//
// Two permissions, `tabs` and `nativeMessaging`, and nothing else. No
// `host_permissions`, no content script, no `scripting`, no `webRequest`. A
// content script would be code running inside every page somebody opens, and
// this needs to know a name rather than to read a page.
//
// No `idle`. The browser spike asked `chrome.idle` every twenty
// seconds through thirty minutes of an empty room and got `active` ninety-four
// times out of ninety-four, including the last twenty-five minutes with the
// monitor physically off. A browser on Omarchy does not merely fail to notice
// idleness -- it reports presence that is not there. So the permission is not
// asked for, because the answer would be a lie the daemon would then have to
// know to ignore.
//
// No `alarms` and no `storage`. The same spike measured the native port holding
// this service worker alive for thirty minutes of silence with ninety-one
// heartbeats and no gap and no `onDisconnect`: `connectNative` is a strong
// keepalive, and with it the structural MV3 problem is not this file's to solve.

"use strict";

/// The name in `/etc/chromium/native-messaging-hosts/`. A host at that path is
/// root's to write; a host of the same name in the child's own home directory
/// is hers, which is why the policy that installs this one exists.
const HOST_NAME = "com.omahouse.meter";

/// How often the same answer is sent again with nothing having changed.
///
/// The daemon samples rather than sums -- it asks "what is in front right now"
/// on its own two second cycle -- so this is what tells "nobody has changed tab
/// for ten minutes" from "the browser is gone". Five seconds against the
/// daemon's freshness window of fifteen leaves room for two beats to go missing
/// before a site stops being counted.
const HEARTBEAT_MS = 5000;

/// What is sent when there is no site to name: no focused window, a tab that is
/// not on the web, an incognito window this extension cannot see into, or the
/// compositor having taken keyboard focus away because the screen went dark.
///
/// Sent rather than withheld, and that matters: silence would be
/// indistinguishable from a browser that had crashed, and the daemon would go
/// on billing the last site for as long as its freshness window lasted.
const NOTHING = "-";

/// How long to wait before opening the port again after it closed.
const RECONNECT_MS = 5000;

// -- the registrable domain ---------------------------------------------------
//
// The privacy boundary, and it is kept here rather than at the far end.
// `docs/design.md` §5.4: the URL never leaves the browser process, so
// there is no path -- no bug, no compromised host, no file left readable -- by
// which the page somebody was on can be read out of omahouse. What crosses the
// wire is `youtube.com`, and the difference between that and
// `youtube.com/watch?v=...` is the difference between a meter and a log of a
// child's afternoon.
//
// Chromium does not hand the public suffix list to an extension, so this is a
// reduction and not a lookup. It is right for the shape of name a household
// meets and wrong for the rest of the list, and the daemon reduces again with
// the same table on the other side -- `registrableDomain` in `src/core/Focus.cpp`,
// where it is a pure function with a test per case. Being wrong here costs
// attribution and never minutes: `bbc.co.uk` read as `co.uk` is a row with an
// ugly name in it, and the total of the day is unchanged.

/// Suffixes that take three labels rather than two.
///
/// The shortest list that covers what a Brazilian household on Omarchy is
/// actually going to open. It is not the public suffix list and does not claim
/// to be; anything not in it is reduced to its last two labels.
const THREE_LABEL_SUFFIXES = new Set([
    "com.br", "net.br", "org.br", "gov.br", "edu.br", "art.br", "blog.br",
    "co.uk", "org.uk", "ac.uk", "gov.uk", "me.uk",
    "com.au", "net.au", "org.au", "edu.au",
    "co.jp", "ne.jp", "or.jp", "ac.jp",
    "com.ar", "com.mx", "com.pt", "com.es", "co.in", "co.nz", "co.za",
]);

/// A host with no label to spare, or an address. Both are reported whole.
function isAddress(host) {
    // An IPv4 literal, and the bracketed form an IPv6 URL host takes. Neither
    // has a registrable domain to find, and neither is a site with a name.
    return /^\d+(\.\d+){3}$/.test(host) || host.startsWith("[");
}

function registrableDomain(host) {
    const lower = String(host || "").toLowerCase().replace(/\.+$/, "");
    if (!lower || isAddress(lower)) {
        return lower;
    }
    const labels = lower.split(".");
    if (labels.length <= 2) {
        return lower;
    }
    const lastTwo = labels.slice(-2).join(".");
    if (THREE_LABEL_SUFFIXES.has(lastTwo) && labels.length >= 3) {
        return labels.slice(-3).join(".");
    }
    return lastTwo;
}

/// The site of one tab, or `NOTHING`.
///
/// Only `http` and `https`. `chrome://`, `file://`, `about:blank` and an
/// extension's own pages are not sites somebody is spending time on in any sense
/// a household would recognise, and reporting them would put `newtab` at the top
/// of the day's table.
function siteOf(tab) {
    if (!tab || !tab.url) {
        return NOTHING;
    }
    let parsed;
    try {
        parsed = new URL(tab.url);
    } catch (ignored) {
        return NOTHING;
    }
    if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
        return NOTHING;
    }
    const domain = registrableDomain(parsed.hostname);
    return domain || NOTHING;
}

// -- the port -----------------------------------------------------------------

let port = null;
let heartbeat = null;

function open() {
    if (port) {
        return;
    }
    try {
        port = chrome.runtime.connectNative(HOST_NAME);
    } catch (ignored) {
        // No host manifest, or the policy refused it. There is nothing useful to
        // do about it from in here and nothing to say to anybody: the daemon's
        // own answer to a channel that never opened is a day with no sites in
        // it, which is the honest reading.
        port = null;
        return;
    }
    port.onDisconnect.addListener(() => {
        port = null;
        // Not measured: the browser spike held the port for thirty
        // minutes without one of these, and a suspend and resume was never
        // exercised (`docs/design.md` §9, question 9). So the
        // reconnection here is written for a case nobody has seen rather than
        // for one somebody has, and it is deliberately the dullest possible
        // shape: wait, open again, report again.
        setTimeout(open, RECONNECT_MS);
    });
    report("connected");
}

function send(site) {
    if (!port) {
        open();
    }
    if (!port) {
        return;
    }
    try {
        // The site and nothing else. No time: the host stamps its own, because a
        // clock inside the browser is a clock the person being measured can move,
        // and the far end is going to distrust the stamp either way.
        port.postMessage({ site: site });
    } catch (ignored) {
        port = null;
    }
}

// -- what is in front ---------------------------------------------------------

async function report(why) {
    let site = NOTHING;
    try {
        const window_ = await chrome.windows.getLastFocused();
        // `focused` and not merely "the last one to have been": a Chromium that
        // is open behind somebody's editor is not a site anybody is looking at.
        if (window_ && window_.focused) {
            const tabs = await chrome.tabs.query({ active: true, windowId: window_.id });
            site = siteOf(tabs && tabs[0]);
        }
    } catch (ignored) {
        site = NOTHING;
    }
    // Sent whether or not it changed, and there is no debounce on the way out.
    // `docs/design.md` §5.7 warned that `windows.onFocusChanged` sends
    // a spurious `WINDOW_ID_NONE` before every window-to-window switch on some
    // Linux window managers and that the lie would have to be damped.
    // the browser spike measured it on this compositor: twenty real
    // alt-tabs, twenty clean events, zero spurious `NONE`. Damping a lie nobody
    // has observed would be machinery with no measurement behind it, and this
    // file is small on purpose.
    void why;
    send(site);
}

chrome.windows.onFocusChanged.addListener(() => report("focus"));
chrome.tabs.onActivated.addListener(() => report("tab"));
chrome.tabs.onUpdated.addListener((id, change) => {
    // Only a change of address. A tab that finished loading an image it already
    // had is not somebody moving to another site.
    if (change && change.url) {
        report("url");
    }
});
chrome.runtime.onStartup.addListener(open);
chrome.runtime.onInstalled.addListener(open);

// And on every load of the worker, because neither of the two events above fires
// when Chromium revives a worker it had stopped.
open();

heartbeat = setInterval(() => report("heartbeat"), HEARTBEAT_MS);
