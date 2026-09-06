#pragma once

#include <QDate>
#include <QString>

namespace omahouse {

// Where the two files of docs/design.md §4 live on this machine.
//
// In `src/sys` and not in `src/core` for the one reason everything else here is:
// it reads the environment. The core is handed a path and writes to it; deciding
// which path that is depends on the machine, and on whether the caller is a test
// that may not touch /etc.
//
// Both roots are overridable by variable. That is not a convenience: the CLI of
// stage 4 reads only, and its end to end suite has to run as an ordinary user
// with no /etc/omahouse and no /var/lib/omahouse on the machine at all. The
// stage that writes gets the same two doors, and so gets tested the same way.
namespace paths {

/// `/etc/omahouse`, or `$OMAHOUSE_CONFIG_DIR`.
QString configDir();
/// `/var/lib/omahouse`, or `$OMAHOUSE_STATE_DIR`.
QString stateDir();

/// `/etc/omahouse` itself, whatever this run was pointed at. Named rather than
/// spelled again by whoever needs it, so that the refusal in `whyNotBlockable`
/// and the check in `configDirIsTheSystems` cannot come to disagree.
QString systemConfigDir();

/// `<configDir>/profiles.json` -- root writes it, everyone reads it.
QString profilesFile();
/// `<configDir>/blocked` -- the file the PAM line of docs/design.md §2 reads.
///
/// Not JSON, and it cannot be: `pam_listfile.so` reads one name per line, and
/// the whole point of §2 is that the block is a stock PAM module and not a
/// module of ours. So this is the one file omahouse writes that it did not
/// choose the format of.
QString blockedFile();
/// `<configDir>/machines.json` -- the machines of this household.
///
/// Beside `profiles.json` and read the same way: root writes it, everybody
/// reads it. Its absence is the ordinary state of a machine that is the only
/// one there is, which is most of them, and it is never an error.
QString machinesFile();
/// `<stateDir>/elsewhere/<machine>/<user>/<YYYY-MM-DD>.json` -- a day that was
/// spent on another computer.
///
/// The same shape as this machine's own ledgers, one directory deeper, and read
/// with the same reader. Whatever brings them here writes files and nothing
/// else: a central that held a database of everybody's days would be a second
/// answer to what a day is, and the first one is already on disk.
QString elsewhereLedgerFile(const QString &machine, const QString &user,
                            const QDate &date);
/// `<stateDir>/elsewhere` -- where every other machine's days are collected.
QString elsewhereDir();
/// `<configDir>/furniture` -- what this machine starts for itself, one name per
/// line, beyond the two `src/core/Furniture.h` was measured with.
///
/// The same shape as `blocked` and for the same reason: an operator has to be
/// able to read it, add a line and be sure of what it did. Its absence is the
/// ordinary state and means the built-in list stands. Blank lines and `#`
/// comments are skipped, because a list somebody maintains by hand is a list
/// they will want to write a note in.
QString furnitureFile();
/// `/etc/chromium/policies/managed`, or `$OMAHOUSE_CHROMIUM_POLICY_DIR`.
///
/// A third root, and it gets a variable for the same reason the other two do:
/// the end to end suite has to be able to prove that the file is written, with
/// the right bytes in it, without a Chromium on the machine and without root --
/// and, much more to the point, without changing the browser policy of whoever
/// is running the suite. This is the one root omahouse writes that belongs to
/// another program.
QString chromiumPolicyDir();
/// `<chromiumPolicyDir>/omahouse.json`.
///
/// Its own file, and never a shared one. Omarchy's `browser-policy.sh` owns
/// `policies.json` in the same directory and purges what it does not recognise;
/// Chromium merges every file it finds there. So omahouse writes a file with its
/// own name, which is also what makes removing the package a matter of removing
/// one path rather than editing somebody else's document.
QString chromiumPolicyFile();
/// Whether that root is still the machine's own.
bool chromiumPolicyDirIsTheSystems();

/// `<stateDir>/<user>` -- one directory per fiscalised account.
QString userStateDir(const QString &user);
/// `<stateDir>/<user>/<AAAA-MM-DD>.json` -- one ledger per day.
QString ledgerFile(const QString &user, const QDate &date);

/// Whether that root is still the machine's own.
///
/// This is what tells "writing needs root" from "writing needs whatever the
/// filesystem says". A run pointed at a tree of its own -- the end to end suite,
/// somebody trying a profile out in $TMPDIR -- is writing where it was told to
/// write, and demanding root for that would be demanding root to write in
/// somebody's home directory. A run pointed at /etc/omahouse is writing the file
/// the daemon reads, and that one is root's.
///
/// One question per root and not one for both together: moving the ledger out
/// of /var must not quietly excuse a write to /etc.
bool configDirIsTheSystems();
bool stateDirIsTheSystems();

} // namespace paths

} // namespace omahouse
