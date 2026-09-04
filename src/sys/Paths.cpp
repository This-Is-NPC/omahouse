#include "Paths.h"

#include <QByteArray>

namespace omahouse {
namespace paths {

namespace {

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
    return fromEnvironmentOr("OMAHOUSE_CONFIG_DIR", "/etc/omahouse");
}

QString stateDir()
{
    return fromEnvironmentOr("OMAHOUSE_STATE_DIR", "/var/lib/omahouse");
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
