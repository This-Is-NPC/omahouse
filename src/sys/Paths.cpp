#include "Paths.h"

#include <QByteArray>

namespace omahouse {
namespace paths {

namespace {

const char *const kSystemConfigDir = "/etc/omahouse";
const char *const kSystemStateDir = "/var/lib/omahouse";

QString fromEnvironmentOr(const char *variable, const char *fallback)
{
    const QByteArray value = qgetenv(variable);
    if (!value.isEmpty())
        return QString::fromLocal8Bit(value);
    return QString::fromLatin1(fallback);
}

} // namespace

QString configDir()
{
    return fromEnvironmentOr("OMAHOUSE_CONFIG_DIR", kSystemConfigDir);
}

QString stateDir()
{
    return fromEnvironmentOr("OMAHOUSE_STATE_DIR", kSystemStateDir);
}

// Compared by value rather than by whether the variable is set, so that pointing
// a variable back at /etc/omahouse is the same thing as not setting it. The two
// spellings of one path have to mean one thing, or the check is a check somebody
// can talk their way out of.

bool configDirIsTheSystems()
{
    return configDir() == QString::fromLatin1(kSystemConfigDir);
}

bool stateDirIsTheSystems()
{
    return stateDir() == QString::fromLatin1(kSystemStateDir);
}

QString profilesFile()
{
    return configDir() + QStringLiteral("/profiles.json");
}

QString userStateDir(const QString &user)
{
    return stateDir() + QLatin1Char('/') + user;
}

QString ledgerFile(const QString &user, const QDate &date)
{
    // The name is the date as spec.md §4 writes it, and it is built here rather
    // than by the caller so that the reader of a day and the writer of it cannot
    // disagree about the format.
    return userStateDir(user) + QLatin1Char('/')
        + date.toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".json");
}

} // namespace paths
} // namespace omahouse
