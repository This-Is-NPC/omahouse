#pragma once

#include "AppScope.h"

#include <QString>
#include <QVector>

#include <sys/types.h>

namespace omahouse {

// The teeth -- plan.md stage 7, and the only part of omahouse that can take
// something away from somebody.
//
// Three mechanisms, and every one of them was measured working before a line of
// this was written. poc/findings.md is the record:
//
//   Close   round 2   SIGTERM into the scope, wait, `echo 1 > cgroup.kill`.
//                     One scope went from one process to none in a single write
//                     while Hyprland, pid 484, carried on either side of it.
//   Logout  round 3   the name into /etc/omahouse/blocked, then `loginctl
//                     terminate-user`. Sessions gone in 25 s, the tty1 autologin
//                     refused by pam_listfile, and the session back on its own
//                     20 s after the name came out again.
//
// So nothing here is a discovery. What is new, and what this file is mostly
// about, is refusing.

// -- what may never be closed -------------------------------------------------

/// Why this scope must not be closed, or an empty string when it may be.
///
/// This is the one function that stands between `omahouse` and somebody's
/// compositor, and it is a pure predicate over three strings so that it can be
/// proved on a development machine where none of the rest of this file may run.
///
/// Five questions, and every one of them has to answer yes:
///
/// 1. **Is this tree the machine's own?** `$OMAHOUSE_CGROUP_ROOT` points the
///    whole read path at a directory in $TMPDIR for the end to end suite, and
///    that suite's `cgroup.procs` files hold pids like 4000 that were typed by a
///    person. Those numbers are real pids on the machine running the suite. A
///    build that signalled them would be a test suite killing whatever happened
///    to be process 4000 -- so a cgroup root that is not `/sys/fs/cgroup` may be
///    read, counted and reported on, and never signalled. This is testing.md §6
///    made into code rather than into a rule somebody has to remember.
/// 2. **Is it under this user's `app.slice`?** The path has to be inside the
///    `app.slice` of the very uid the decision was about, by cleaned prefix. The
///    scopes only ever come from `Proc::scopesFor`, which walks nothing else, so
///    this is a second lock on a door that is already shut -- and it is the lock
///    that would hold if somebody ever added a third source of scopes.
/// 3. **Is `session.slice` nowhere in it?** Structurally implied by 2, and
///    checked anyway, because this is the failure that kills the product:
///    docs/design.md §5 makes `session.slice` untouchable, `cgroup.kill` there takes
///    Hyprland, `quickshell`, `pipewire` and the user's own systemd with it, and
///    what the person sees is a greeter two seconds after logging in with no
///    explanation. A check that is redundant today is a check that costs nothing
///    and catches the refactor that made it necessary.
/// 4. **Is it a `.scope`?** A slice is a grouping and a service is something a
///    package declared. Only a scope is an app somebody opened.
/// 5. **Is the path free of `..`?** A cleaned path cannot climb, and the prefix
///    test is only worth anything against a cleaned path.
QString whyNotCloseable(const QString &cgroupRoot, const QString &appSlicePath,
                        const AppScope &scope);

/// Why the block of docs/design.md §2 must not be acted on, or an empty string.
///
/// One question: is `/etc/omahouse` the directory this run is configured from?
///
/// The two halves of `logout` are one action. `blocked` without the PAM line is
/// a file nobody reads; the PAM line without `blocked` is nothing at all. But
/// the dangerous asymmetry is the third combination: a run whose `blocked` is a
/// file in $TMPDIR, and whose `loginctl terminate-user` is the real one on the
/// real machine. That is a session ended with no block behind it -- theatre, by
/// round 2's measurement -- and on a development machine it is somebody's
/// afternoon.
///
/// So the termination is gated on the block being the machine's own file. A run
/// pointed at a config directory of its own writes its `blocked` there, prints
/// what it would have ended, and ends nothing.
QString whyNotBlockable(const QString &configDir);

/// Why the browser's managed policy must not be written, or an empty string.
///
/// The third refusal of the same family, after the cgroup root and the
/// configuration root, and docs/design.md §11 calls it the one with the shortest
/// fuse: the end to end suite runs on the developer's laptop with the
/// developer's Chromium open, and a bug here is somebody's browser taken away in
/// the middle of an afternoon.
///
/// One question, and it is the same shape as `whyNotBlockable`'s: the machine's
/// own `/etc/chromium/policies/managed` is written only by a run that is also
/// managing the machine's own `/etc/omahouse`. A run pointed at a tree of its
/// own writes its policy where it was pointed and says nothing.
///
/// Here rather than in the CLI because there are two callers now. `saveProfiles`
/// reconciles the policy whenever a rule changes, and `watch` reconciles it
/// every cycle for the sites that have run out today -- and a permission that
/// existed in one of them and not the other would be exactly the hole this is
/// for.
QString whyNotWriteTheBrowserPolicy();

// -- doing it -----------------------------------------------------------------

/// The programs, and the doors that let a suite prove the command without the
/// command happening: `loginctl`, or `$OMAHOUSE_LOGINCTL`. The same door
/// `$OMAHOUSE_USERADD` and `$OMAHOUSE_NOTIFY_SEND` are, for the same reason.
QString loginctlProgram();

/// `loginctl terminate-user <user>`, program first.
QStringList terminateUserCommand(const QString &user);

/// Something that can close a scope and end a session.
///
/// An interface for the same reason `Notifier` is one: a suite has to be able to
/// watch what would have been done without a process dying. Here it matters
/// more -- there is no version of "try the real one and see" that is safe on the
/// machine this is written on.
class Enforcer {
public:
    virtual ~Enforcer();

    /// SIGTERM to every process in the scope's tree. `count` gets how many were
    /// signalled, which is zero for a scope that has already gone.
    ///
    /// The polite half. An editor gets to write its buffers, a browser gets to
    /// close its session without the crash bar next time, and a scope whose
    /// processes all leave is a scope `cgroup.kill` is never asked about.
    virtual bool terminate(const AppScope &scope, int *count, QString *error) = 0;

    /// `echo 1 > <scope>/cgroup.kill` -- the write of docs/design.md §5.
    ///
    /// The whole cgroup at once, with no reaping order, no orphan and no hunting
    /// for pids that forked while the list was being read. It is also why the
    /// path is checked rather than the pids: this is one write, and the only
    /// thing that makes it safe is which file it lands in.
    virtual bool killTree(const AppScope &scope, QString *error) = 0;

    /// `loginctl terminate-user <user>`.
    virtual bool endSessions(const QString &user, QString *error) = 0;
};

/// The one that really does it.
class MachineEnforcer : public Enforcer {
public:
    bool terminate(const AppScope &scope, int *count, QString *error) override;
    bool killTree(const AppScope &scope, QString *error) override;
    bool endSessions(const QString &user, QString *error) override;
};

} // namespace omahouse
