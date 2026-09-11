#pragma once

#include "AppScope.h"

#include <QString>
#include <QVector>

#include <sys/types.h>

namespace omahouse {

// One unit under `session.slice` that is holding processes omahouse can neither
// count nor close -- docs/design.md §5, "O ponto cego".
//
// It is not a list of things that went wrong. It is the shape of what the model
// cannot reach: an app started by a raw `exec` from a keybinding, or from inside
// a terminal, lands in the compositor's own unit and is from there on
// indistinguishable from the compositor. Naming the unit and its process count
// is all that can honestly be said about it, and saying it is the requirement.
struct SessionUnit {
    QString unit;
    QString cgroupPath;
    int pidCount = 0;
};

// The one place that reads the machine.
//
// `src/core` is pure by rule -- no clock, no environment, no /proc, no
// /sys/fs/cgroup -- so the reading lives here, in the library that is allowed to
// know what machine it is on. Stage 7 of plan.md adds the writing beside it
// (cgroup.kill, notify-send through systemd-run, terminate-user); this class
// only ever reads.
//
// The root of the cgroup tree comes in by constructor, defaulting to the real
// one. That is what lets the tests exercise the walk against a tree built in
// $TMPDIR out of the unit names poc/findings.md measured, instead of demanding
// a graphical session with a Chromium open in it.
class Proc {
public:
    /// `/sys/fs/cgroup`, or `$OMAHOUSE_CGROUP_ROOT` where it is set. The
    /// variable is what `tests/test_cli.py` points at a fake tree, and the
    /// reason it exists on the read path at all: the e2e has to be able to
    /// assert what `status` prints, and a real session is a moving target.
    static QString defaultCgroupRoot();

    /// `/sys/fs/cgroup` itself, whatever this instance was pointed at.
    static QString systemCgroupRoot();

    /// `/proc`, or `$OMAHOUSE_PROC_ROOT`. The same door as the cgroup root and
    /// for the same reason: the dominant executable of a scope is read from the
    /// kernel's process table, and a suite that needs a real session with a real
    /// VS Code in it to assert anything is a suite nobody runs.
    static QString defaultProcRoot();

    explicit Proc(const QString &cgroupRoot = defaultCgroupRoot(),
                  const QString &procRoot = defaultProcRoot());

    QString cgroupRoot() const { return m_cgroupRoot; }
    QString procRoot() const { return m_procRoot; }

    /// Whether that tree is still the machine's own, compared by value so that
    /// pointing the variable back at /sys/fs/cgroup means what not setting it
    /// means -- exactly as `paths::configDirIsTheSystems` does it.
    ///
    /// This is the question the teeth ask first. A tree somewhere else is a tree
    /// whose `cgroup.procs` were written by a person, and the numbers in it are
    /// real pids belonging to whoever is running the suite.
    bool cgroupRootIsTheSystems() const { return m_cgroupRoot == systemCgroupRoot(); }

    /// `<root>/user.slice/user-<uid>.slice/user@<uid>.service/app.slice`, the
    /// path of docs/design.md §5 step 2.
    QString appSlicePath(uid_t uid) const;
    QString sessionSlicePath(uid_t uid) const;

    /// Whether the user's own systemd manager is up. False is not a failure: it
    /// is a user who is not logged in, and there is nothing to count for them.
    bool hasSession(uid_t uid) const;

    /// Every account with a session on this machine right now, by uid.
    ///
    /// The other direction from everything else here, and it exists for one
    /// reason: a profile that names no account cannot be found by asking about
    /// an account. `watch` goes from profiles to accounts -- a profile, its
    /// user, its uid -- and a fallback has no user to start from, so the cycle
    /// has to be able to ask the machine who is sitting at it.
    ///
    /// Read from the cgroup tree and not from `/etc/passwd`, deliberately. The
    /// question is who is *here*, not who could be: an account that has never
    /// logged in has nothing to count and nothing to close, and enumerating
    /// every human on the machine would mean a minimum-uid rule, which is a
    /// guess about somebody else's naming.
    ///
    /// Sorted, so a cycle's answer does not depend on the order a directory
    /// happened to be read in.
    QVector<uid_t> accountsWithSessions() const;

    /// Every scope under `app.slice`, recursively -- they sit under
    /// `app-graphical.slice` when uwsm launched them and directly under
    /// `app.slice` when a `.desktop` did.
    ///
    /// A scope whose unit name the parser refuses comes back with an empty `id`
    /// and its `pidCount` intact rather than being dropped, and the count is the
    /// point: `Policy::evaluate` bills it to every budget whose selector is `*`,
    /// because a scope with processes in it is somebody at the keyboard whether
    /// or not anything can name it. What the missing id costs it is a budget or
    /// a rule of its own, so its verdict is the profile's default. `status`
    /// reports it as something counted and not named, which is the same duty §5
    /// puts on the blind spot: say what cannot be accounted for.
    ///
    /// Ordered by cgroup path, so two runs over an unchanged tree print the same
    /// list in the same order; readdir(3) order is not an order.
    QVector<AppScope> scopesFor(uid_t uid) const;

    /// The units of `session.slice` that could be hiding somebody's app.
    ///
    /// The rule is structural rather than a list of names to keep up to date --
    /// which is what the PoC took away from the executable baseline. A
    /// `.service` under `session.slice` is a unit that some package declared, so
    /// it is session plumbing: pipewire, the portals, dbus. A `.scope` is a
    /// process tree registered at run time, so it is something that was
    /// launched. Those, and the compositor's own `wayland-wm@*.service`, which
    /// the PoC measured a raw `exec` landing inside of.
    QVector<SessionUnit> sessionSliceUnits(uid_t uid) const;

    /// The processes in those units, summed. Never zero on a live graphical
    /// session, because the compositor's own processes are in it: the count is
    /// "how much of this session omahouse cannot tell apart from Hyprland", not
    /// "how many rules were broken".
    int sessionSliceProcesses(uid_t uid) const;

    /// Fills `dominantExe` and `dominantExeCount` on a scope: the executable
    /// most of its processes are running, read one process at a time from the
    /// kernel's own record of them.
    ///
    /// A step of its own rather than part of `scopesFor`, because the two are
    /// wanted in different places. Counting a tick needs the id and the process
    /// count and nothing else; showing an operator what an id would really let in
    /// needs this, and pays a readlink per process for it. The daemon of stage 6
    /// ticks every two seconds and has no use for it at all.
    ///
    /// An executable that cannot be read leaves the fields empty. That is the
    /// ordinary answer for an unprivileged run looking at another account -- the
    /// kernel refuses the link of a process it does not own -- and it means "no
    /// opinion", never "they disagree".
    void resolveDominantExe(AppScope *scope) const;
    void resolveDominantExe(QVector<AppScope> *scopes) const;

private:
    QString m_cgroupRoot;
    QString m_procRoot;
};

/// Every pid in one cgroup and in everything below it, in the order the kernel
/// wrote them.
///
/// A free function and not a method, because the two callers want it for
/// opposite reasons and neither of them wants a `Proc`: `resolveDominantExe`
/// reads what those processes are running, and `Enforcer::terminate` sends them
/// a signal. The second is why a line that is not a number is skipped rather
/// than taken for pid zero -- `kill(0, SIGTERM)` is the whole process group.
QVector<qint64> pidsInCgroupTree(const QString &cgroupPath);

} // namespace omahouse
