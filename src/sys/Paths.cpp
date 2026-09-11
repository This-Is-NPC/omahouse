#include "Paths.h"

#include <QByteArray>
#include <QDir>

namespace omahouse {
namespace paths {

namespace {

const char *const kSystemConfigDir = "/etc/omahouse";
const char *const kSystemStateDir = "/var/lib/omahouse";
// A compile-time constant on the browser's side too -- `policy_paths.cc`, which
// docs/the-browser-half.md §3.1 quotes -- so there is nothing to look up and
// nothing that could differ per account.
const char *const kSystemChromiumPolicyDir = "/etc/chromium/policies/managed";

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

QString systemConfigDir()
{
    return QString::fromLatin1(kSystemConfigDir);
}

bool configDirIsTheSystems()
{
    return configDir() == QString::fromLatin1(kSystemConfigDir);
}

bool stateDirIsTheSystems()
{
    return stateDir() == QString::fromLatin1(kSystemStateDir);
}

QString chromiumPolicyDir()
{
    return fromEnvironmentOr("OMAHOUSE_CHROMIUM_POLICY_DIR", kSystemChromiumPolicyDir);
}

bool chromiumPolicyDirIsTheSystems()
{
    return chromiumPolicyDir() == QString::fromLatin1(kSystemChromiumPolicyDir);
}

QString chromiumPolicyFile()
{
    return chromiumPolicyDir() + QStringLiteral("/omahouse.json");
}

QString profilesFile()
{
    return configDir() + QStringLiteral("/profiles.json");
}

QString blockedFile()
{
    return configDir() + QStringLiteral("/blocked");
}

QString machinesFile()
{
    return configDir() + QStringLiteral("/machines.json");
}

QString thisMachineFile()
{
    return configDir() + QStringLiteral("/machine.json");
}

QString machineTokensFile()
{
    return configDir() + QStringLiteral("/machine-tokens.json");
}

QString furnitureFile()
{
    return configDir() + QStringLiteral("/furniture");
}

QString stagedDir()
{
    return configDir() + QStringLiteral("/staged");
}

QString stagedProfileFile()
{
    return stagedDir() + QStringLiteral("/profile.json");
}

QString elsewhereDir()
{
    return stateDir() + QStringLiteral("/elsewhere");
}

QString publishedProfileFile(const QString &machine, const QString &user)
{
    return QFileInfo(elsewhereProfileFile(machine, user)).dir().filePath(QStringLiteral("published.json"));
}
QString resolvedProfileFile(const QString &machine, const QString &user)
{
    return QFileInfo(elsewhereProfileFile(machine, user)).dir().filePath(QStringLiteral("resolved.json"));
}

QString elsewhereProfileFile(const QString &machine, const QString &user)
{
    return elsewhereDir() + QLatin1Char('/') + machine + QLatin1Char('/') + user
        + QStringLiteral("/profile.json");
}

QString elsewhereLedgerFile(const QString &machine, const QString &user, const QDate &date)
{
    return elsewhereDir() + QLatin1Char('/') + machine + QLatin1Char('/') + user
        + QLatin1Char('/') + date.toString(QStringLiteral("yyyy-MM-dd"))
        + QStringLiteral(".json");
}

QString userStateDir(const QString &user)
{
    return stateDir() + QLatin1Char('/') + user;
}

QString ledgerFile(const QString &user, const QDate &date)
{
    // The name is the date as docs/design.md §4 writes it, and it is built here rather
    // than by the caller so that the reader of a day and the writer of it cannot
    // disagree about the format.
    return userStateDir(user) + QLatin1Char('/')
        + date.toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".json");
}

QString lastLedgerFileBefore(const QString &user, const QDate &date)
{
    // The same spelling the writer uses, from the same place, so that the
    // comparison below is a comparison of dates and not of two opinions about
    // how a date is written.
    const QString today = date.toString(QStringLiteral("yyyy-MM-dd"));
    QDir dir(userStateDir(user));
    QString best;
    const QStringList names = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &name : names) {
        const QString stem = name.chopped(5);
        // Anything else in the directory is not a day. `<user>/` holds one file
        // per date and nothing has ever put something else there, but a name
        // that is not a date must not be able to become the newest day by
        // sorting above one.
        if (!QDate::fromString(stem, QStringLiteral("yyyy-MM-dd")).isValid())
            continue;
        if (stem >= today)
            continue;
        if (best.isEmpty() || stem > best)
            best = stem;
    }
    if (best.isEmpty())
        return {};
    return userStateDir(user) + QLatin1Char('/') + best + QStringLiteral(".json");
}

} // namespace paths
} // namespace omahouse
