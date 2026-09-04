#include "Proc.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <unistd.h>

#include <algorithm>

namespace omahouse {

namespace {

const char *const kDefaultCgroupRoot = "/sys/fs/cgroup";
const char *const kCgroupRootVariable = "OMAHOUSE_CGROUP_ROOT";
const char *const kDefaultProcRoot = "/proc";
const char *const kProcRootVariable = "OMAHOUSE_PROC_ROOT";

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

/// The pids written in one `cgroup.procs`, appended to `out`.
///
/// The same file `processesIn` counts, read for the numbers this time. A line
/// that is not a number is skipped rather than taken as pid zero: the file is
/// written by the kernel, but it is also a file a test writes, and a zero would
/// go looking at the process table's own directory.
void pidsIn(const QString &cgroupPath, QVector<qint64> *out)
{
    QFile file(cgroupPath + QStringLiteral("/cgroup.procs"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (const QByteArray &line : lines) {
        bool ok = false;
        const qint64 pid = line.trimmed().toLongLong(&ok);
        if (ok && pid > 0)
            out->append(pid);
    }
}

void pidsInTree(const QString &cgroupPath, QVector<qint64> *out)
{
    pidsIn(cgroupPath, out);
    const QDir directory(cgroupPath);
    const QFileInfoList children =
        directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &child : children)
        pidsInTree(child.absoluteFilePath(), out);
}

/// What one process is running, by the link the kernel keeps to its executable.
///
/// readlink(2) and not QFileInfo::symLinkTarget: the link is not an ordinary
/// one. It has no length to stat, it resolves to a file that may have been
/// replaced by a package upgrade since -- the kernel then writes the path with
/// ` (deleted)` after it, which is a fact about the file and not part of its
/// name -- and it is unreadable for a process the caller does not own, which is
/// an answer rather than a failure.
QString executableOf(const QString &procRoot, qint64 pid)
{
    const QByteArray path =
        QFile::encodeName(QStringLiteral("%1/%2/exe").arg(procRoot).arg(pid));
    QByteArray buffer(1024, Qt::Uninitialized);
    for (;;) {
        const ssize_t written = ::readlink(path.constData(), buffer.data(), buffer.size());
        if (written < 0)
            return {};
        if (written < buffer.size()) {
            QString target = QFile::decodeName(buffer.left(static_cast<int>(written)));
            const QString deleted = QStringLiteral(" (deleted)");
            if (target.endsWith(deleted))
                target.chop(deleted.size());
            return target;
        }
        // Filled the buffer exactly: the path may have been truncated, and
        // readlink does not say which. Ask again with room.
        if (buffer.size() >= 64 * 1024)
            return {};
        buffer.resize(buffer.size() * 2);
    }
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

QString Proc::systemCgroupRoot()
{
    return QString::fromLatin1(kDefaultCgroupRoot);
}

QString Proc::defaultProcRoot()
{
    const QByteArray fromEnvironment = qgetenv(kProcRootVariable);
    if (!fromEnvironment.isEmpty())
        return QString::fromLocal8Bit(fromEnvironment);
    return QString::fromLatin1(kDefaultProcRoot);
}

Proc::Proc(const QString &cgroupRoot, const QString &procRoot)
    : m_cgroupRoot(cgroupRoot)
    , m_procRoot(procRoot)
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

void Proc::resolveDominantExe(AppScope *scope) const
{
    if (!scope)
        return;
    scope->dominantExe.clear();
    scope->dominantExeCount = 0;
    if (scope->cgroupPath.isEmpty())
        return;

    const QVector<qint64> pids = pidsInCgroupTree(scope->cgroupPath);

    QHash<QString, int> tally;
    for (qint64 pid : pids) {
        const QString executable = executableOf(m_procRoot, pid);
        if (!executable.isEmpty())
            ++tally[executable];
    }

    // Ties broken by the path, so that two runs over one unchanged scope say the
    // same thing. A scope with two executables in equal number is a real shape --
    // a browser and its crash handler -- and which of them gets named matters
    // less than its not changing under the operator between two reads.
    for (auto it = tally.cbegin(); it != tally.cend(); ++it) {
        const bool better = it.value() > scope->dominantExeCount
            || (it.value() == scope->dominantExeCount && it.key() < scope->dominantExe);
        if (better) {
            scope->dominantExe = it.key();
            scope->dominantExeCount = it.value();
        }
    }
}

void Proc::resolveDominantExe(QVector<AppScope> *scopes) const
{
    if (!scopes)
        return;
    for (AppScope &scope : *scopes)
        resolveDominantExe(&scope);
}

QVector<qint64> pidsInCgroupTree(const QString &cgroupPath)
{
    QVector<qint64> pids;
    if (!cgroupPath.isEmpty())
        pidsInTree(cgroupPath, &pids);
    return pids;
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
