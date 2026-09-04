#pragma once

#include <QString>
#include <QVector>

namespace omahouse {

// One program the machine has a `.desktop` for.
//
// The studio's allowlist editor is fed from these rather than from a text field,
// because an operator writing a rule is choosing among the programs that exist,
// not spelling an identifier from memory. A typo in a text field is a rule that
// never fires and never says why.
struct DesktopApp {
    /// What a rule would be written about: the base name of the desktop file,
    /// minus `.desktop`.
    ///
    /// That is the same string uwsm gives the scope it launches the entry in --
    /// `code.desktop` becomes `app-code-3579042.scope`, and a flatpak's
    /// `org.freedesktop.Platform.desktop` becomes
    /// `app-flatpak-org.freedesktop.Platform-2351381583.scope`. It is a guess in
    /// the sense that the scope is the ground truth and only exists once the
    /// program is running; the studio shows the running scopes beside these, so
    /// the guess is checkable against what is actually open.
    QString id;
    /// The `Name` of the entry, which is what a person calls the program.
    QString name;
    /// The program `Exec` starts, without its arguments. This is the shim
    /// evidence of spec.md §5 for an entry that is not running: an entry whose
    /// `Exec` is `gtk-launch` names a launcher and not a program, and the
    /// operator is shown that before writing the rule rather than after.
    QString exec;
};

/// Every application entry under the XDG data directories, by id, sorted by
/// name.
///
/// `NoDisplay` and `Hidden` entries are left out: the first is plumbing a
/// desktop is asked not to show -- mime handlers, the settings panes of another
/// program -- and the second is an entry a later directory has retracted. A list
/// nobody can find their program in is a list they will type past.
QVector<DesktopApp> installedApps();

/// The `applications` directories that walk reads, whether they exist or not.
///
/// Exposed so the window can watch them rather than re-walking a few hundred
/// files every two seconds to find out that nothing was installed in the last
/// two seconds. A program appearing is a directory changing, and that is a thing
/// the kernel will say out loud.
QStringList applicationDirs();

} // namespace omahouse
