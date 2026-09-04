#pragma once

#include <QDate>
#include <QString>

namespace omahouse {

// Where the two files of spec.md §4 live on this machine.
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

/// `<configDir>/profiles.json` -- root writes it, everyone reads it.
QString profilesFile();
/// `<stateDir>/<user>` -- one directory per fiscalised account.
QString userStateDir(const QString &user);
/// `<stateDir>/<user>/<AAAA-MM-DD>.json` -- one ledger per day.
QString ledgerFile(const QString &user, const QDate &date);

} // namespace paths

} // namespace omahouse
