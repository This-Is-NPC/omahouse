#pragma once

#include <QString>

namespace omahouse {

// One systemd scope under `app.slice`, which is what the PoC settled on as the
// identity of a running app -- see spec.md §5 and poc/findings.md.
//
// The path of the executable was the earlier answer and it does not work: the
// PoC found 21 processes sharing /usr/lib/chromium/chromium, and 26 sharing
// /usr/bin/bash while some of them were session plumbing and one of them was the
// terminal the user opened. The scope groups an app's whole tree by
// construction, tells apps apart from plumbing by which slice they are under,
// and carries the flatpak app id in its own name.
struct AppScope {
    /// What rules and budgets match against: `chromium`, `org.freedesktop.Platform`.
    QString id;
    /// The unit as systemd spells it, escapes and random suffix and all. What a
    /// Close decision names, because it is what `cgroup.kill` is found under.
    QString unit;
    /// Where it lives in the cgroup tree. The core only carries it.
    QString cgroupPath;
    /// Processes in the scope right now. Zero is a scope on its way out, and it
    /// is not somebody's app running.
    int pidCount = 0;

    bool isLive() const { return pidCount > 0 && !id.isEmpty(); }
};

/// The app id inside a scope unit name, or an empty string with `error` set.
///
/// systemd writes `app-<launcher>-<id>-<random>.scope`, with the launcher part
/// absent when nothing claimed one, and with the id escaped -- which is why
/// `xdg-terminal-exec` arrives as `xdg\x2dterminal\x2dexec` and has to be
/// unescaped rather than split on. All five shapes below were measured on a
/// real session (poc/findings.md, rounds 1 to 3):
///
///     app-Hyprland-chromium-031bdc27.scope                    -> chromium
///     app-code-3579042.scope                                  -> code
///     app-flatpak-org.freedesktop.Platform-2351381583.scope   -> org.freedesktop.Platform
///     app-Hyprland-xdg\x2dterminal\x2dexec-151e8e07.scope     -> xdg-terminal-exec
///     app-Hyprland-sleep-7865852f.scope                       -> sleep
///
/// A unit that does not have that shape is refused by name instead of being cut
/// into something id-shaped. An app whose id came out wrong is worse than an app
/// with no id at all: the first quietly matches the wrong budget or falls
/// through to the default verdict under a name nobody wrote, and the second is a
/// thing `omahouse status` can report as unseen, which is what spec.md §5 asks
/// of everything the model cannot account for.
QString scopeIdFromUnit(const QString &unit, QString *error);

} // namespace omahouse
