#include "Notify.h"

#include <QByteArray>
#include <QProcess>

#include <unistd.h>

namespace omahouse {

namespace {

/// How long a notification may take before the loop gives up on it.
///
/// Bounded, and shorter than the tick it runs inside. `notify-send` returns as
/// soon as the daemon has the message and `systemd-run` as soon as the
/// transient unit is queued, so a second is already generous; what this number
/// is really for is the session with no notification daemon in it, where the
/// call can sit waiting for a name on the bus. A daemon that stops counting
/// because a screen would not listen has its priorities backwards.
constexpr int kSpeakingTimeout = 2000;

QString programFromEnvironmentOr(const char *variable, const char *fallback)
{
    const QByteArray value = qgetenv(variable);
    if (!value.isEmpty())
        return QString::fromLocal8Bit(value);
    return QString::fromLatin1(fallback);
}

} // namespace

QString notifySendProgram()
{
    return programFromEnvironmentOr("OMAHOUSE_NOTIFY_SEND", "notify-send");
}

QString systemdRunProgram()
{
    return programFromEnvironmentOr("OMAHOUSE_SYSTEMD_RUN", "systemd-run");
}

QStringList notifyCommand(uid_t target, uid_t self, const QString &summary, const QString &body)
{
    if (target == self)
        return {notifySendProgram(), summary, body};

    // The line of poc/findings.md round 2, spelled out. The bus address is built
    // from the uid rather than read from anywhere, because /run/user/<uid>/bus
    // is where logind puts it and the daemon has no other way to learn it for a
    // session it is not in.
    return {
        systemdRunProgram(),
        QStringLiteral("--uid=%1").arg(static_cast<qulonglong>(target)),
        QStringLiteral("--setenv=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/%1/bus")
            .arg(static_cast<qulonglong>(target)),
        notifySendProgram(),
        summary,
        body,
    };
}

Notifier::~Notifier() = default;

DesktopNotifier::DesktopNotifier(uid_t self)
    : m_self(self)
{
}

DesktopNotifier::DesktopNotifier()
    : DesktopNotifier(::getuid())
{
}

bool DesktopNotifier::notify(uid_t uid, const QString &summary, const QString &body,
                             QString *error)
{
    QStringList command = notifyCommand(uid, m_self, summary, body);
    const QString program = command.takeFirst();

    QProcess speaking;
    speaking.setProgram(program);
    speaking.setArguments(command);
    speaking.setProcessChannelMode(QProcess::MergedChannels);
    speaking.start();
    if (!speaking.waitForStarted(kSpeakingTimeout)) {
        if (error)
            *error = QStringLiteral("cannot run %1: %2").arg(program, speaking.errorString());
        return false;
    }
    if (!speaking.waitForFinished(kSpeakingTimeout)) {
        speaking.kill();
        speaking.waitForFinished();
        if (error)
            *error = QStringLiteral("%1 did not answer in %2s").arg(program).arg(
                kSpeakingTimeout / 1000);
        return false;
    }
    if (speaking.exitStatus() != QProcess::NormalExit || speaking.exitCode() != 0) {
        if (error) {
            const QString said = QString::fromLocal8Bit(speaking.readAll()).trimmed();
            *error = QStringLiteral("%1 failed (%2)%3")
                         .arg(program)
                         .arg(speaking.exitCode())
                         .arg(said.isEmpty() ? QString() : QStringLiteral(": %1").arg(said));
        }
        return false;
    }
    return true;
}

} // namespace omahouse
