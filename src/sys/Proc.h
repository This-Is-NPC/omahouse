#pragma once

#include "AppScope.h"

#include <QString>
#include <QVector>

#include <sys/types.h>

namespace omahouse {

// One unit under `session.slice` that is holding processes omahouse can neither
// count nor close -- spec.md §5, "O ponto cego".
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

    explicit Proc(const QString &cgroupRoot = defaultCgroupRoot());

    QString cgroupRoot() const { return m_cgroupRoot; }

    /// `<root>/user.slice/user-<uid>.slice/user@<uid>.service/app.slice`, the
    /// path of spec.md §5 step 2.
    QString appSlicePath(uid_t uid) const;
    QString sessionSlicePath(uid_t uid) const;

    /// Whether the user's own systemd manager is up. False is not a failure: it
    /// is a user who is not logged in, and there is nothing to count for them.
    bool hasSession(uid_t uid) const;

    /// Every scope under `app.slice`, recursively -- they sit under
    /// `app-graphical.slice` when uwsm launched them and directly under
    /// `app.slice` when a `.desktop` did.
    ///
    /// A scope whose unit name the parser refuses comes back with an empty `id`
    /// and its `pidCount` intact rather than being dropped. `Policy::evaluate`
    /// ignores it -- `AppScope::isLive` is false without an id -- and `status`
    /// reports it as something seen and not named, which is the same duty §5
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

private:
    QString m_cgroupRoot;
};

} // namespace omahouse
