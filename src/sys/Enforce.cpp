#include "Enforce.h"

#include "Paths.h"
#include "Proc.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QProcess>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <unistd.h>

namespace omahouse {

namespace {

/// How long `loginctl` may take before the loop gives up on it.
///
/// `terminate-user` returns once logind has the request, not once the session is
/// gone -- round 3 measured the sessions taking about twenty-five seconds to
/// actually go -- so this is generous for what it waits on. What it is really
/// for is the machine whose logind is not answering, where a daemon that blocked
/// forever would stop counting everybody else's day.
constexpr int kLoginctlTimeout = 10 * 1000;

QString clean(const QString &path)
{
    return QDir::cleanPath(path);
}

} // namespace

QString whyNotCloseable(const QString &cgroupRoot, const QString &appSlicePath,
                        const AppScope &scope)
{
    // 1. The tree has to be the machine's own. See the header: the end to end
    //    suite's cgroup.procs hold pids somebody typed, and they are real pids on
    //    the machine running the suite.
    if (clean(cgroupRoot) != Proc::systemCgroupRoot()) {
        return QStringLiteral("the cgroup tree is %1 and not %2, so the pids in it are not "
                              "this machine's to signal")
            .arg(clean(cgroupRoot), Proc::systemCgroupRoot());
    }

    if (scope.unit.isEmpty() || scope.cgroupPath.isEmpty())
        return QStringLiteral("the scope has no unit or no cgroup of its own");

    // 4. Only a scope is an app somebody opened.
    if (!scope.unit.endsWith(QLatin1String(".scope")))
        return QStringLiteral("%1 is not a .scope").arg(scope.unit);

    const QString path = clean(scope.cgroupPath);
    const QString root = clean(appSlicePath);

    // 5. A cleaned path holds no `..`, and one that still does after cleaning is
    //    a relative path that climbed out of wherever it started.
    if (path.contains(QLatin1String("../")) || path.endsWith(QLatin1String("/..")))
        return QStringLiteral("%1 climbs out of the tree it was found in").arg(path);

    // The root itself has to be an app.slice. A caller that handed a session
    // slice in here would otherwise pass every other test.
    if (!root.endsWith(QLatin1String("/app.slice")))
        return QStringLiteral("%1 is not an app.slice").arg(root);

    // 2. Inside this user's app.slice, by prefix, with the separator in the
    //    prefix so that `app.slice.evil` is not `app.slice`.
    if (!path.startsWith(root + QLatin1Char('/')))
        return QStringLiteral("%1 is not under %2").arg(path, root);

    // 3. And `session.slice` nowhere in it. Implied by the line above and
    //    checked anyway: this is the one mistake that ends somebody's session
    //    without ever meaning to, and spec.md §5 makes the rule structural.
    if (path.contains(QLatin1String("/session.slice/"))
        || path.endsWith(QLatin1String("/session.slice"))) {
        return QStringLiteral("%1 is in session.slice, which is never judged").arg(path);
    }

    return {};
}

QString whyNotBlockable(const QString &configDir)
{
    if (clean(configDir) == clean(paths::systemConfigDir()))
        return {};
    return QStringLiteral("the configuration is %1 and not %2, so %1/blocked is a file no PAM "
                          "stack reads -- ending a session behind it would be an eviction with "
                          "no lock on the door")
        .arg(clean(configDir), clean(paths::systemConfigDir()));
}

QString loginctlProgram()
{
    const QByteArray fromEnvironment = qgetenv("OMAHOUSE_LOGINCTL");
    if (!fromEnvironment.isEmpty())
        return QString::fromLocal8Bit(fromEnvironment);
    return QStringLiteral("loginctl");
}

QStringList terminateUserCommand(const QString &user)
{
    return {loginctlProgram(), QStringLiteral("terminate-user"), user};
}

Enforcer::~Enforcer() = default;

bool MachineEnforcer::terminate(const AppScope &scope, int *count, QString *error)
{
    if (count)
        *count = 0;

    const QVector<qint64> pids = pidsInCgroupTree(scope.cgroupPath);
    int signalled = 0;
    QString firstFailure;
    for (qint64 pid : pids) {
        // Never pid 1, never 0 or a negative -- `kill(0, ...)` is this process's
        // whole group and a negative is somebody else's -- and never this daemon.
        // None of these can be in a fiscalised user's app.slice, which is the
        // point: a guard that only fires when something else has already gone
        // wrong is the only kind worth having here.
        if (pid <= 1 || pid == static_cast<qint64>(::getpid())
            || pid == static_cast<qint64>(::getppid())) {
            continue;
        }
        if (::kill(static_cast<pid_t>(pid), SIGTERM) == 0) {
            ++signalled;
            continue;
        }
        // ESRCH is the ordinary end of a process that left between the read of
        // cgroup.procs and here, and it is not a failure of anything.
        if (errno == ESRCH)
            continue;
        if (firstFailure.isEmpty()) {
            firstFailure = QStringLiteral("cannot signal %1: %2")
                               .arg(pid)
                               .arg(QString::fromLocal8Bit(::strerror(errno)));
        }
    }
    if (count)
        *count = signalled;
    if (!firstFailure.isEmpty()) {
        if (error)
            *error = firstFailure;
        return false;
    }
    return true;
}

bool MachineEnforcer::killTree(const AppScope &scope, QString *error)
{
    const QString path = scope.cgroupPath + QStringLiteral("/cgroup.kill");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        // A scope that ended between the decision and this write takes its whole
        // directory with it, so "there is no such file" is the good ending. The
        // caller sees a scope that is gone on the next cycle either way.
        if (!QFile::exists(scope.cgroupPath))
            return true;
        if (error)
            *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write("1\n") < 0 || !file.flush()) {
        if (error)
            *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool MachineEnforcer::endSessions(const QString &user, QString *error)
{
    QStringList command = terminateUserCommand(user);
    const QString program = command.takeFirst();

    QProcess ending;
    ending.setProgram(program);
    ending.setArguments(command);
    ending.setProcessChannelMode(QProcess::MergedChannels);
    ending.start();
    if (!ending.waitForStarted(kLoginctlTimeout)) {
        if (error)
            *error = QStringLiteral("cannot run %1: %2").arg(program, ending.errorString());
        return false;
    }
    if (!ending.waitForFinished(kLoginctlTimeout)) {
        ending.kill();
        ending.waitForFinished();
        if (error) {
            *error = QStringLiteral("%1 did not answer in %2s")
                         .arg(program)
                         .arg(kLoginctlTimeout / 1000);
        }
        return false;
    }
    if (ending.exitStatus() != QProcess::NormalExit || ending.exitCode() != 0) {
        if (error) {
            const QString said = QString::fromLocal8Bit(ending.readAll()).trimmed();
            *error = QStringLiteral("%1 terminate-user %2 failed (%3)%4")
                         .arg(program, user)
                         .arg(ending.exitCode())
                         .arg(said.isEmpty() ? QString() : QStringLiteral(": %1").arg(said));
        }
        return false;
    }
    return true;
}

} // namespace omahouse
