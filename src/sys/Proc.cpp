#include "Proc.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace omahouse {

namespace {

const char *const kDefaultCgroupRoot = "/sys/fs/cgroup";
const char *const kCgroupRootVariable = "OMAHOUSE_CGROUP_ROOT";

bool endsWith(const QString &name, const char *suffix)
{
    return name.endsWith(QLatin1String(suffix));
}

/// The processes written in one `cgroup.procs`.
///
/// The file is a pid per line and it is read whole rather than counted by
/// `readLine`, because it is a handful of bytes and a partial read of it under a
/// process that is exiting is a count, not a failure. A file that cannot be
/// opened -- the cgroup went away between the readdir and here, which is the
/// ordinary end of a scope -- is zero.
int processesIn(const QString &cgroupPath)
{
    QFile file(cgroupPath + QStringLiteral("/cgroup.procs"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    int count = 0;
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (!line.trimmed().isEmpty())
            ++count;
    }
    return count;
}

/// The processes of a cgroup and of everything below it.
///
/// A scope normally has no children, but a delegated one can, and a subtree
/// whose processes are not counted is time somebody spent that nobody debited.
int processesInTree(const QString &cgroupPath)
{
    int count = processesIn(cgroupPath);
    const QDir directory(cgroupPath);
    const QFileInfoList children =
        directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &child : children)
        count += processesInTree(child.absoluteFilePath());
    return count;
}

/// Walks `app.slice` for scopes.
///
/// Down through `.slice` directories and no others: the slices are systemd's own
/// grouping -- `app-graphical.slice` for what uwsm launched, and the
/// `app-dbus\x2d….slice` a dbus activation gets -- and a `.service` under
/// `app.slice` is a user service the manager started (dconf, the keyring, the
/// portals), which is plumbing that happens to sit on this side of the tree.
/// Only a `.scope` is an app somebody opened.
void collectScopes(const QString &directoryPath, QVector<AppScope> *out)
{
    const QDir directory(directoryPath);
    const QFileInfoList children =
        directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &child : children) {
        const QString name = child.fileName();
        const QString path = child.absoluteFilePath();
        if (endsWith(name, ".scope")) {
            AppScope scope;
            scope.unit = name;
            scope.cgroupPath = path;
            scope.pidCount = processesInTree(path);
            // An unreadable name is left empty rather than guessed at, and the
            // scope still comes back: see the header, and Policy.cpp on why an
            // app with the wrong id is worse than an app with none.
            scope.id = scopeIdFromUnit(name, nullptr);
            out->append(scope);
            continue;
        }
        if (endsWith(name, ".slice"))
            collectScopes(path, out);
    }
}

} // namespace

QString Proc::defaultCgroupRoot()
{
    const QByteArray fromEnvironment = qgetenv(kCgroupRootVariable);
    if (!fromEnvironment.isEmpty())
        return QString::fromLocal8Bit(fromEnvironment);
    return QString::fromLatin1(kDefaultCgroupRoot);
}

Proc::Proc(const QString &cgroupRoot)
    : m_cgroupRoot(cgroupRoot)
{
}

QString Proc::appSlicePath(uid_t uid) const
{
    return QStringLiteral("%1/user.slice/user-%2.slice/user@%2.service/app.slice")
        .arg(m_cgroupRoot)
        .arg(static_cast<qulonglong>(uid));
}

QString Proc::sessionSlicePath(uid_t uid) const
{
    return QStringLiteral("%1/user.slice/user-%2.slice/user@%2.service/session.slice")
        .arg(m_cgroupRoot)
        .arg(static_cast<qulonglong>(uid));
}

bool Proc::hasSession(uid_t uid) const
{
    return QFileInfo(appSlicePath(uid)).isDir();
}

QVector<AppScope> Proc::scopesFor(uid_t uid) const
{
    QVector<AppScope> scopes;
    const QString root = appSlicePath(uid);
    if (!QFileInfo(root).isDir())
        return scopes;
    collectScopes(root, &scopes);
    std::sort(scopes.begin(), scopes.end(), [](const AppScope &a, const AppScope &b) {
        return a.cgroupPath < b.cgroupPath;
    });
    return scopes;
}

QVector<SessionUnit> Proc::sessionSliceUnits(uid_t uid) const
{
    QVector<SessionUnit> units;
    const QString root = sessionSlicePath(uid);
    if (!QFileInfo(root).isDir())
        return units;

    const QDir directory(root);
    const QFileInfoList children =
        directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &child : children) {
        const QString name = child.fileName();
        // The compositor's unit, whatever compositor uwsm was asked for:
        // `wayland-wm@hyprland.desktop.service` here, and the same shape with
        // another name elsewhere. A `sleep 600` from `hyprctl dispatch exec`
        // was measured inside this cgroup, sharing it with Hyprland itself,
        // which is why it cannot be counted and must not be killed.
        const bool compositor = name.startsWith(QLatin1String("wayland-wm@"))
            && endsWith(name, ".service");
        if (!compositor && !endsWith(name, ".scope"))
            continue;
        SessionUnit unit;
        unit.unit = name;
        unit.cgroupPath = child.absoluteFilePath();
        unit.pidCount = processesInTree(unit.cgroupPath);
        if (unit.pidCount > 0)
            units.append(unit);
    }
    std::sort(units.begin(), units.end(), [](const SessionUnit &a, const SessionUnit &b) {
        return a.unit < b.unit;
    });
    return units;
}

int Proc::sessionSliceProcesses(uid_t uid) const
{
    int count = 0;
    const QVector<SessionUnit> units = sessionSliceUnits(uid);
    for (const SessionUnit &unit : units)
        count += unit.pidCount;
    return count;
}

} // namespace omahouse
