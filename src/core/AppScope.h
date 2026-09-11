#pragma once

#include <QString>

namespace omahouse {

// One systemd scope under `app.slice`, which is what the PoC settled on as the
// identity of a running app -- see docs/design.md §5 and poc/findings.md.
//
// The path of the executable was the earlier answer and it does not work: the
// PoC found 21 processes sharing /usr/lib/chromium/chromium, and 26 sharing
// /usr/bin/bash while some of them were session plumbing and one of them was the
// terminal the user opened. The scope groups an app's whole tree by
// construction, tells apps apart from plumbing by which slice they are under,
// and carries the flatpak app id in its own name.
struct AppScope {
    /// What rules and budgets match against: `chromium`, `org.freedesktop.Platform`.
    ///
    /// Empty where the parser could not name the unit -- a
    /// `tmux-spawn-<uuid>.scope`, of which this machine has dozens. That is a
    /// scope nothing can name, not a scope nothing is running: it still counts
    /// towards `*`, and what it cannot do is match a budget or a rule that
    /// names an app, so its verdict is the profile's default. See
    /// `selectorMatches` and docs/design.md §5.
    QString id;
    /// The unit as systemd spells it, escapes and random suffix and all. What a
    /// Close decision names, because it is what `cgroup.kill` is found under.
    QString unit;
    /// Where it lives in the cgroup tree. The core only carries it.
    QString cgroupPath;
    /// Processes in the scope right now. Zero is a scope on its way out, and it
    /// is not somebody's app running.
    int pidCount = 0;
    /// The executable most of the processes inside the scope are running, as an
    /// absolute path, or empty when nothing could be read. Filled by `Proc`,
    /// because reading it is reading the machine; the core only carries it.
    ///
    /// It is here because the id alone is not always the name of what is
    /// running: poc/findings.md round 4 found seven scopes called
    /// `app-Hyprland-gtk\x2dlaunch-*.scope` on this machine whose processes were
    /// all VS Code. The launcher shim gives the scope its own name, so the id
    /// collapses every app opened that way into one word.
    ///
    /// It is not a second selector and it cannot overrule the id. The two
    /// signals fail in opposite places -- the id fails on a shim and is right
    /// about a flatpak, the executable is right about a shim and is
    /// `/usr/bin/bwrap` for every flatpak alike -- so neither answers instead of
    /// the other.
    ///
    /// What it does is answer *beside* the id, in the one place the id has
    /// nothing to say: a scope whose executable does not corroborate its name
    /// can also be matched by a selector the executable does corroborate. That
    /// is `selectorMatches` in Profile.h, and it is what lets a rule about
    /// `code` reach the program the Omarchy menu opened under the name
    /// `gtk-launch`. A scope whose id already names what is running is matched
    /// by that name and by no other, which is what keeps this a fallback rather
    /// than a second namespace.
    ///
    /// It is also what `status` reports and what the picker shows: an operator
    /// about to write a rule is told what an id would really let in.
    QString dominantExe;
    /// How many processes of the scope are running `dominantExe`. Zero with an
    /// empty path is "nobody looked, or nothing could be read"; the count is
    /// printed beside the path so that a claim made from one readable process
    /// out of twenty looks like what it is.
    int dominantExeCount = 0;

    /// Whether this is somebody using the machine right now.
    ///
    /// Processes, and nothing else. The id used to be required here and that
    /// was an accounting bug with a hole in it the size of a terminal: 46
    /// `tmux-spawn-<uuid>.scope` holding more than a hundred processes were
    /// stepped over by `Policy::evaluate` entirely, and an afternoon spent
    /// inside them debited the `session` budget zero seconds. A scope with
    /// processes in it is a person at the keyboard whether or not anything can
    /// name it, so the id belongs to matching -- see `selectorMatches` -- and
    /// not to being alive.
    bool isLive() const { return pidCount > 0; }
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
/// thing `omahouse status` can report as unseen, which is what docs/design.md §5 asks
/// of everything the model cannot account for.
QString scopeIdFromUnit(const QString &unit, QString *error);

/// Whether `exePath` backs up `id` -- whether the program running inside the
/// scope is plausibly the one the scope is named after.
///
/// There is no list of known shims anywhere in this tree, and there will not be:
/// a `baseline.json` of names to keep current is exactly what the PoC killed in
/// round 1. The disagreement is derived from the two things that were measured
/// instead. Either the id appears somewhere in the path of the executable --
/// `chromium` in `/usr/lib/chromium/chromium`, `code` in `/usr/share/code/code`
/// -- or the name of the executable appears in the id, which is what makes
/// `org.chromium.Chromium` and `/usr/lib/chromium/chromium` the same app. Case,
/// dots, dashes and underscores are all dropped before comparing, because
/// `gnome-calculator`, `gnome_calculator` and `org.gnome.Calculator` are three
/// spellings of one program and none of them is the file name.
///
/// Anything else is a disagreement: `gtk-launch` against
/// `/usr/share/code/chrome_crashpad_handler`, `xdg-terminal-exec` against the
/// terminal it opened. So is `org.freedesktop.Platform` against
/// `/usr/bin/bwrap`, and that one is not a mistake to fix here -- the executable
/// really is bwrap for every flatpak on the machine. A disagreement is still a
/// thing to say out loud to whoever is writing a rule, and the caller says both
/// halves of it so the operator can tell which case they are looking at.
///
/// This function is asked two different questions with the same two arguments,
/// and `selectorMatches` in Profile.h asks both. *Does this scope's id name what
/// is running* opens the door -- a disagreement means the id has nothing useful
/// to say. *Does this selector name what is running* is what may then walk
/// through it. Both are needed, and the first is what keeps a scope that has
/// already said what it is from being matched by a second name.
///
/// An empty path is not a disagreement. It is nobody having looked, or a process
/// whose executable could not be read, and having no opinion is the honest
/// answer to that.
bool exeCorroboratesId(const QString &id, const QString &exePath);

} // namespace omahouse
