#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <sys/types.h>

namespace omahouse {

// The file the browser's native messaging host writes, and the two halves that
// touch it -- docs/design.md §5.2.
//
// `src/core/Focus.h` is what a line *means*. This is where it lives, who may
// write it, and how a process running as root opens something a child owns
// without that being a hole.
//
// -- why a file, and not a socket ---------------------------------------------
//
// `docs/proposal-browser.md` §8.1 called the alternative "the largest single
// piece of unplanned work on this page": the host runs as the child
// (`.temp/spike-extension.md` §1 measured uid 1001, inside her session, with her
// bus in the environment) and the ledger is root's, so the two have to meet
// somewhere -- and the shape the proposal reached for was a socket in the root
// daemon, with framing, and a second writer's worth of validation, against
// docs/design.md §3's "no second process, no IPC, no second reader".
//
// This is the cheaper shape, and it is cheaper because the host is made stupid
// rather than trusted. It appends `<epoch> <site>` to a file in the child's own
// runtime directory and does nothing else -- no ledger, no accumulation, no
// decision, no privilege. `omahouse watch` is already root, already ticks every
// two seconds, and already knows whether anybody is in front of the screen; it
// reads the file the way it reads `/sys/class/drm`, as one more fact about the
// machine. So there is no new endpoint, no protocol to version, and nothing
// listening: the accumulation stays in exactly one place, which is the thing §3
// was protecting.
//
// -- opening a file in a directory the child owns -----------------------------
//
// `/run/user/<uid>` is systemd's, and `/run/user` is root's, so the child cannot
// replace either. Everything below them is hers: she can make `omahouse` a
// symlink to `/etc/shadow`, she can make `focus` a fifo that never answers, she
// can grow it to fill the tmpfs. So the read below walks the last two components
// with `openat` and `O_NOFOLLOW`, opens the file `O_NONBLOCK` so a fifo cannot
// hold the cycle, checks with `fstat` that what it got is a regular file owned by
// that very uid, and reads a bounded tail of it and never the whole thing.
//
// This is the same posture docs/design.md §5.1 takes towards the compositor
// socket, arrived at from the other direction. There the answer was to refuse to
// open it at all, because what was on the far side was a program the child
// chooses. Here what is on the far side is bytes, the parser is pure and has a
// test per way of being wrong, and the worst a perfectly crafted file can do is
// put a name of her choosing in her own report.

/// `/run/user`, or `$OMAHOUSE_RUNTIME_ROOT`.
///
/// The same door `$OMAHOUSE_DRM_ROOT`, `$OMAHOUSE_PROC_ROOT` and
/// `$OMAHOUSE_CGROUP_ROOT` are, and it exists for the same reason: the end to
/// end suite has to drive a browser reporting a site without a browser, without
/// a session and without root.
QString runtimeRoot();
/// `/run/user` itself, whatever this run was pointed at.
QString systemRuntimeRoot();
/// Whether that root is still the machine's own, compared by value.
bool runtimeRootIsTheSystems();

/// `<runtimeRoot>/<uid>/omahouse` -- the directory the host writes in.
QString focusDirFor(const QString &root, uid_t uid);
/// `<runtimeRoot>/<uid>/omahouse/focus` -- the file itself.
///
/// Under the child's runtime directory and not under `/var/lib`, because it is
/// hers: it is written by a process of hers, it is worth nothing after she logs
/// out, and a tmpfs that systemd empties at logout is exactly the lifetime it
/// should have. Nothing accumulates here; the accumulation is the ledger's.
QString focusFileFor(const QString &root, uid_t uid);

/// The most of the file that is ever read, in bytes.
///
/// Only the last line is looked at, and more than one line's worth is read so
/// that a tail beginning in the middle of one still has a whole one after it.
/// Four kilobytes is about forty lines and is a bound on what a child filling
/// her own tmpfs can make root read per tick.
constexpr int kFocusTailBytes = 4096;

/// The most the host lets the file grow to before it starts it again.
///
/// The file is a log of transitions plus a beat every five seconds, so it grows
/// by about a kilobyte a minute and nothing ever reads more than its last line.
/// At the cap the host truncates and carries on, which loses history nothing was
/// keeping.
constexpr int kFocusMostBytes = 256 * 1024;

/// Something that can say what the browser last reported for one user.
///
/// An interface for the same reason `PresenceSource` and `Notifier` are: the
/// loop has to be drivable through every state -- no file, a stale file, a file
/// full of rubbish -- without a browser on the machine running the suite.
class FocusSource {
public:
    virtual ~FocusSource();

    /// The last few kilobytes of that user's focus file, or nothing at all.
    ///
    /// Every failure is the same answer: no file, a directory that is a symlink
    /// now, a fifo, a file belonging to somebody else, a read that failed. What
    /// the caller does with an empty blob is bill nothing, and "she deleted it"
    /// and "she never had one" are not different kinds of nothing.
    virtual QByteArray tail(uid_t uid) = 0;
};

/// The one that really reads.
class FileFocusSource : public FocusSource {
public:
    explicit FileFocusSource(const QString &root = omahouse::runtimeRoot());

    QString root() const { return m_root; }

    QByteArray tail(uid_t uid) override;

private:
    QString m_root;
};

/// The host's half: make the directory, append one line, and keep it bounded.
///
/// Runs as the child and needs nothing. `error` is for the host's own standard
/// error and for a test; nobody upstream of it acts on a failure, because there
/// is nobody upstream of it.
bool appendFocusLine(const QString &path, const QDateTime &at, const QString &site,
                     QString *error);

} // namespace omahouse
