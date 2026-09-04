#include "Presence.h"

#include "Enforce.h"

#include <QDir>
#include <QFile>
#include <QProcess>

namespace omahouse {

namespace {

/// How long `loginctl` may take before the cycle gives up on it and says
/// `Unknown`.
///
/// Shorter than the ten seconds `Enforce` gives `terminate-user`, because this
/// one runs every two seconds and the answer is worth nothing late. A logind
/// that is not answering makes the presence unknown, which is exactly what it
/// is; it must never make the loop stop counting everybody's day.
constexpr int kSeatTimeout = 2 * 1000;

/// The whole of one small file, trimmed. Empty for anything that cannot be read
/// -- a connector that went away between the listing and here is not an error,
/// it is a monitor somebody unplugged.
QString readAttribute(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromLatin1(file.readAll()).trimmed();
}

/// Runs one `loginctl` and gives back its standard output, or an empty string.
///
/// Every failure is the same answer here on purpose: not started, timed out,
/// non-zero. What the caller does with an empty string is say `Unknown`, and
/// "logind refused" and "logind is not there" are not different kinds of not
/// knowing.
QString runShow(const QStringList &command)
{
    QStringList arguments = command;
    const QString program = arguments.takeFirst();

    QProcess showing;
    showing.setProgram(program);
    showing.setArguments(arguments);
    showing.setStandardErrorFile(QProcess::nullDevice());
    showing.start();
    if (!showing.waitForStarted(kSeatTimeout))
        return QString();
    if (!showing.waitForFinished(kSeatTimeout)) {
        showing.kill();
        showing.waitForFinished(kSeatTimeout);
        return QString();
    }
    if (showing.exitStatus() != QProcess::NormalExit || showing.exitCode() != 0)
        return QString();
    return QString::fromLocal8Bit(showing.readAllStandardOutput());
}

} // namespace

QString presenceReasonName(Presence::Reason reason)
{
    switch (reason) {
    case Presence::Reason::Using:
        return QStringLiteral("using");
    case Presence::Reason::ScreenOff:
        return QStringLiteral("screen-off");
    case Presence::Reason::Locked:
        return QStringLiteral("locked");
    case Presence::Reason::OtherSession:
        return QStringLiteral("other-session");
    case Presence::Reason::NoSession:
        return QStringLiteral("no-session");
    case Presence::Reason::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

bool presenceReasonFromName(const QString &name, Presence::Reason *out)
{
    static const Presence::Reason kAll[] = {
        Presence::Reason::Using,        Presence::Reason::ScreenOff,
        Presence::Reason::Locked,       Presence::Reason::OtherSession,
        Presence::Reason::NoSession,    Presence::Reason::Unknown,
    };
    for (Presence::Reason reason : kAll) {
        if (presenceReasonName(reason) != name)
            continue;
        if (out)
            *out = reason;
        return true;
    }
    return false;
}

QString presenceSentence(const Presence &presence)
{
    switch (presence.reason) {
    case Presence::Reason::Using:
        return QStringLiteral("at the machine: the seat is showing this session and the "
                              "screen is on");
    case Presence::Reason::ScreenOff:
        return QStringLiteral("away: every connected screen is off");
    case Presence::Reason::Locked:
        return QStringLiteral("away: the session is locked");
    case Presence::Reason::OtherSession:
        return QStringLiteral("away: the seat is showing another session");
    case Presence::Reason::NoSession:
        return QStringLiteral("away: not logged in");
    case Presence::Reason::Unknown:
        break;
    }
    return QStringLiteral("not known: there is no seat or no connected screen to read");
}

QString screenStateName(ScreenState state)
{
    switch (state) {
    case ScreenState::On:
        return QStringLiteral("on");
    case ScreenState::Off:
        return QStringLiteral("off");
    case ScreenState::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

Presence presenceOf(uid_t uid, bool hasSession, const SeatReading &reading)
{
    Presence presence;

    // Nothing else is worth saying about somebody who is not logged in, and it
    // is worth saying even when the seat could not be read: an absent session is
    // an absent person whatever the screen is doing.
    if (!hasSession) {
        presence.present = false;
        presence.reason = Presence::Reason::NoSession;
        return presence;
    }

    // A seat nobody could read is not an absence. The daemon runs on machines
    // with no seat at all -- and, much more to the point, the suite runs on one
    // pointed at a $TMPDIR with no `loginctl` behind it -- and answering `away`
    // there would be inventing an absence out of not looking.
    if (!reading.read) {
        presence.present = false;
        presence.reason = Presence::Reason::Unknown;
        return presence;
    }

    // The screen is lit, but for somebody else. Measured with a `chvt`: the
    // connector stays `On` and only logind knows whose session is in front, so
    // this question has to come before the screen's.
    if (!reading.occupied || reading.uid != uid) {
        presence.present = false;
        presence.reason = Presence::Reason::OtherSession;
        return presence;
    }

    switch (reading.screen) {
    case ScreenState::Off:
        presence.present = false;
        presence.reason = Presence::Reason::ScreenOff;
        return presence;
    case ScreenState::On:
        presence.present = true;
        presence.reason = Presence::Reason::Using;
        return presence;
    case ScreenState::Unknown:
        break;
    }

    presence.present = false;
    presence.reason = Presence::Reason::Unknown;
    return presence;
}

QString drmRoot()
{
    const QByteArray fromEnvironment = qgetenv("OMAHOUSE_DRM_ROOT");
    if (!fromEnvironment.isEmpty())
        return QDir::cleanPath(QString::fromLocal8Bit(fromEnvironment));
    return systemDrmRoot();
}

QString systemDrmRoot()
{
    return QStringLiteral("/sys/class/drm");
}

bool drmRootIsTheSystems()
{
    return drmRoot() == systemDrmRoot();
}

QStringList connectedScreens(const QString &root)
{
    QStringList connected;
    const QStringList entries =
        QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        const QString path = root + QLatin1Char('/') + entry;
        // A card has no `status`; a connector does. That is the whole of the
        // difference this needs to know, and it is why the names are not parsed:
        // `card0-Virtual-1`, `card1-eDP-1` and `card1-Writeback-1` are three
        // spellings of the same shape and only one of them is a screen.
        if (readAttribute(path + QStringLiteral("/status")) != QLatin1String("connected"))
            continue;
        connected.append(entry);
    }
    return connected;
}

ScreenState screenStateFromDrm(const QString &root)
{
    const QStringList connected = connectedScreens(root);
    if (connected.isEmpty())
        return ScreenState::Unknown;

    bool sawAnAnswer = false;
    for (const QString &connector : connected) {
        const QString dpms =
            readAttribute(root + QLatin1Char('/') + connector + QStringLiteral("/dpms"));
        if (dpms.isEmpty())
            continue;
        sawAnAnswer = true;
        // Any, and not all: two monitors with one of them asleep is somebody
        // looking at the other one.
        if (dpms == QLatin1String("On"))
            return ScreenState::On;
    }
    return sawAnAnswer ? ScreenState::Off : ScreenState::Unknown;
}

QStringList seatActiveSessionCommand(const QString &seat)
{
    return {loginctlProgram(), QStringLiteral("show-seat"), seat,
            QStringLiteral("-p"), QStringLiteral("ActiveSession")};
}

QStringList sessionUserCommand(const QString &session)
{
    return {loginctlProgram(), QStringLiteral("show-session"), session,
            QStringLiteral("-p"), QStringLiteral("User")};
}

QString showProperty(const QString &output, const QString &key)
{
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        if (line.left(equals).trimmed() != key)
            continue;
        return line.mid(equals + 1).trimmed();
    }
    return QString();
}

PresenceSource::~PresenceSource() = default;

SeatPresenceSource::SeatPresenceSource(const QString &drmRoot, const QString &seat)
    : m_drmRoot(QDir::cleanPath(drmRoot)), m_seat(seat)
{
}

SeatReading SeatPresenceSource::readSeat()
{
    SeatReading reading;
    reading.screen = screenStateFromDrm(m_drmRoot);

    const QString seat = runShow(seatActiveSessionCommand(m_seat));
    if (seat.isEmpty())
        return reading;
    reading.read = true;

    // An empty `ActiveSession` is the greeter between two logins, and it is a
    // seat that was read: there really is nobody's session in front.
    const QString session = showProperty(seat, QStringLiteral("ActiveSession"));
    if (session.isEmpty())
        return reading;

    const QString owner = showProperty(runShow(sessionUserCommand(session)),
                                       QStringLiteral("User"));
    bool aNumber = false;
    const uint uid = owner.toUInt(&aNumber);
    if (!aNumber)
        return reading;

    reading.occupied = true;
    reading.uid = static_cast<uid_t>(uid);
    return reading;
}

} // namespace omahouse
