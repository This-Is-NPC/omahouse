#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace omahouse {

// The site in the front tab, read out of a file the person being measured can
// write -- docs/design.md §5.2.
//
// The whole of the mechanism is three parts and this is the middle one:
//
//   the extension   reports the registrable domain of the active tab of the
//                   focused window, over native messaging, and nothing else
//   the host        runs as the child, and appends `<epoch> <site>` to a file
//                   under her own runtime directory. It cannot write the ledger:
//                   The browser spike measured it running as uid
//                   1001 with /var/lib/omahouse root's, and there is no path
//                   from one to the other
//   this            is what `omahouse watch` reads that file with, and it is
//                   pure so that every way the file can be wrong is a line in a
//                   test rather than a browser somebody has to open
//
// -- the file is untrusted input, and that is the design and not a caveat ------
//
// The child owns the directory. She can write anything into that file, she can
// truncate it, she can delete it, and she can stop the host that writes it by
// killing it. Every one of those is treated as "there is nothing to bill right
// now", and none of them is treated as an error worth stopping the cycle for.
//
// What that costs her is worth naming precisely, because it is the reason this
// is safe to build at all: **she wins anonymity, not minutes.** The total time
// on the machine is held by the cgroup walk of §5 and by the PAM line of §2,
// neither of which this can reach. A child who kills the host has a day whose
// per-site table is short and whose session budget runs out at exactly the same
// minute it would have.
//
// So the reading below is strict rather than forgiving. Only the last complete
// line is ever looked at -- not the last one that happens to parse -- because
// scanning backwards for something usable is how a file full of rubbish still
// bills a site. A stamp in the future is refused, a stamp too old is refused,
// and a name that is not a plausible domain is refused. Every refusal comes out
// the same way: nothing is billed for this tick.

/// How old the last line may be and still be billed, in seconds.
///
/// The extension repeats itself every five seconds whether or not anything
/// changed, so this is what tells "nobody has changed tab for ten minutes" from
/// "the browser is gone" -- and fifteen leaves room for two beats to go missing
/// before a site stops being counted. It is short on purpose: a browser that
/// closed at 19:00 must not go on being billed until 19:15.
constexpr int kFocusFreshnessSeconds = 15;

/// How far ahead of `now` a stamp may be before it is refused.
///
/// Not zero, because the host stamps its line between two of the daemon's ticks
/// and the two are not synchronised. Small, because the only other thing a stamp
/// in the future can be is somebody buying themselves a quarter of an hour of a
/// site that will not be counted.
constexpr int kFocusSkewSeconds = 3;

/// The longest line that will be looked at, in bytes.
///
/// A domain is 253 bytes at the outside and a stamp is ten. Anything longer is
/// not a line this wrote.
constexpr int kFocusLongestLine = 300;

/// One line of the focus file, read.
struct FocusLine {
    /// Whether the line was a line at all. False for rubbish, and false is not
    /// the same as `site` being empty.
    bool read = false;
    /// When the host stamped it. Only meaningful with `read`.
    QDateTime at;
    /// The registrable domain, or empty for `-` -- the browser saying "there is
    /// no site in front", which is a thing it says rather than a thing it
    /// withholds. A window that is not focused, a tab that is not on the web, an
    /// incognito window the extension cannot see into, and a screen the
    /// compositor took keyboard focus away from all arrive here.
    QString site;
};

/// The registrable domain of a host name, by reduction rather than by lookup.
///
/// The public suffix list is not in this program and is not going to be. This is
/// the same short table the extension carries in `extension/background.js`, on
/// the other side of the wire, and it is right for the shape of name a household
/// meets and wrong for the rest of the list.
///
/// Being wrong costs attribution and never minutes. `bbc.co.uk` read as `co.uk`
/// is an ugly row in the day's table; the total of the day is what it was. That
/// trade is taken deliberately -- carrying a copy of the public suffix list, and
/// the job of keeping it current, to make one row read better is not a trade a
/// household control should make.
QString registrableDomain(const QString &host);

/// Whether a name is shaped like a domain at all: labels of letters, digits and
/// hyphens, none of them empty, none of them starting or ending in a hyphen, at
/// most 253 bytes in all, and at least one dot.
///
/// The gate on what may become a key in somebody's day. Without it the child
/// chooses what the rows of the report are called, and a row can be a sentence.
bool isPlausibleDomain(const QString &candidate);

/// One line, as `<epoch seconds> <site>`. `read` is false for anything else.
FocusLine parseFocusLine(const QByteArray &line);

/// The last **complete** line of a blob, parsed.
///
/// Complete means terminated by a newline: the tail of a file may begin in the
/// middle of one, and the host may be in the middle of writing the next.
/// Deliberately the last one and not the last one that parses -- see the head of
/// this file.
FocusLine lastFocusLine(const QByteArray &blob);

/// What to bill right now, or an empty string for nothing.
///
/// The whole decision, over the bytes and the time and nothing else: no file, no
/// clock, no uid. Empty for a blob that is not a line, for a stamp in the future
/// by more than the skew, for one older than the freshness window, for a name
/// that is not a domain, and for the browser having said there is no site in
/// front.
QString siteInFrontOf(const QByteArray &blob, const QDateTime &now,
                      int freshnessSeconds = kFocusFreshnessSeconds);

/// One line for the file, as the host writes it.
///
/// Here rather than in `src/sys` so that the writer and the reader cannot come
/// to disagree about the format: they are two functions in one file with a test
/// that runs one into the other.
QByteArray focusLineFor(const QDateTime &at, const QString &site);

} // namespace omahouse
