#include <QtTest>

#include "Presence.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <unistd.h>

using namespace omahouse;

namespace {

/// One DRM connector, as far as this walk is concerned: a directory with a
/// `status` and a `dpms` in it.
///
/// Every shape below was measured rather than invented, and the presence PoC
/// is the record. The VM's single `card0-Virtual-1`, the development machine's
/// eight unplugged DisplayPorts, its one lit eDP, and the writeback connector
/// that is not a screen at all and answers `On` forever.
void makeConnector(const QString &root, const QString &name, const QString &status,
                   const QString &enabled, const QString &dpms)
{
    const QString path = root + QLatin1Char('/') + name;
    QVERIFY2(QDir().mkpath(path), qPrintable(path));
    const auto write = [&](const QString &file, const QString &contents) {
        QFile handle(path + QLatin1Char('/') + file);
        QVERIFY2(handle.open(QIODevice::WriteOnly | QIODevice::Text), qPrintable(file));
        // The kernel's own attributes end in a newline, and the reader has to
        // cope with that rather than with a value somebody trimmed for it.
        handle.write(contents.toLatin1() + '\n');
    };
    write(QStringLiteral("status"), status);
    write(QStringLiteral("enabled"), enabled);
    write(QStringLiteral("dpms"), dpms);
}

/// A `loginctl` that is a script, so that the real `SeatPresenceSource` can be
/// driven through every state on a machine with no seat on it.
///
/// The same door `$OMAHOUSE_USERADD` and `$OMAHOUSE_NOTIFY_SEND` are, and it is
/// already in the tree: `Enforce.h` reads `$OMAHOUSE_LOGINCTL` for
/// `terminate-user`, and this is the second caller of it.
QString loginctlPath(const QString &directory)
{
    return directory + QStringLiteral("/loginctl");
}

void makeLoginctl(const QString &directory, const QString &body)
{
    const QString path = loginctlPath(directory);
    QFile script(path);
    QVERIFY2(script.open(QIODevice::WriteOnly | QIODevice::Text), qPrintable(path));
    script.write("#!/bin/sh\n");
    script.write(body.toUtf8());
    script.close();
    QVERIFY2(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
             qPrintable(path));
    qputenv("OMAHOUSE_LOGINCTL", QFile::encodeName(path));
}

SeatReading showing(uid_t uid, ScreenState screen)
{
    SeatReading reading;
    reading.read = true;
    reading.occupied = true;
    reading.uid = uid;
    reading.screen = screen;
    return reading;
}

} // namespace

// Whether anybody is in front of the machine, proved on a machine that may have
// nobody in front of it.
//
// That is the whole design of this file. The browser spike measured a
// browser answering `active` for twenty-five minutes with the monitor off, and
// the answer to it was to ask the machine instead -- but a suite that had to ask
// the *real* machine would pass or fail depending on whether whoever ran it had
// gone to make coffee. So the decision is a pure function over facts, the facts
// come out of a $TMPDIR that looks like `/sys/class/drm`, and the seat comes out
// of a shell script.
class PresenceTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_tree.isValid());
    }

    void init()
    {
        m_box = m_tree.filePath(QString::fromLatin1(QTest::currentTestFunction()));
        QVERIFY(QDir().mkpath(m_box));
    }

    void cleanup()
    {
        qunsetenv("OMAHOUSE_DRM_ROOT");
        qunsetenv("OMAHOUSE_LOGINCTL");
    }

    // -- the verdict ---------------------------------------------------------

    // The seat is showing this session and the screen is lit. There is somebody
    // there, and this is the only combination that says so.
    void aLitScreenOnThisSessionIsSomebodyThere()
    {
        const Presence presence = presenceOf(1001, true, showing(1001, ScreenState::On));
        QVERIFY(presence.present);
        QCOMPARE(presence.reason, Presence::Reason::Using);
        QVERIFY(presence.known());
    }

    // The measurement this whole file exists for. Omarchy's shell locks at five
    // minutes and the display goes dark a second later; the browser goes on
    // reporting `active` through all of it, and the machine does not.
    void aScreenThatWentDarkIsNobodyThere()
    {
        const Presence presence = presenceOf(1001, true, showing(1001, ScreenState::Off));
        QVERIFY(!presence.present);
        QCOMPARE(presence.reason, Presence::Reason::ScreenOff);
        QVERIFY(presence.known());
    }

    // Measured with a `chvt`: the connector stays `On` and only logind knows
    // that what is on it belongs to somebody else. This is why the seat is read
    // at all, and why it is asked about before the screen.
    void aLitScreenShowingSomebodyElseIsNotThisPerson()
    {
        const Presence presence = presenceOf(1001, true, showing(1000, ScreenState::On));
        QVERIFY(!presence.present);
        QCOMPARE(presence.reason, Presence::Reason::OtherSession);
    }

    // The greeter between two logins: a seat that was read, with nobody on it.
    void anEmptySeatIsNobodysScreen()
    {
        SeatReading reading;
        reading.read = true;
        reading.screen = ScreenState::On;
        const Presence presence = presenceOf(1001, true, reading);
        QVERIFY(!presence.present);
        QCOMPARE(presence.reason, Presence::Reason::OtherSession);
    }

    // Not logged in beats everything, including a seat nobody could read. There
    // is nothing else worth saying about somebody who is not there at all, and
    // saying `unknown` about them would be pretending the question was hard.
    void noSessionIsSaidBeforeAnythingElse()
    {
        SeatReading unread;
        const Presence presence = presenceOf(1001, false, unread);
        QVERIFY(!presence.present);
        QCOMPARE(presence.reason, Presence::Reason::NoSession);
        QVERIFY(presence.known());
    }

    // A seat that could not be read is not an absence. The daemon runs on
    // machines with no seat, and answering `away` there would be inventing an
    // absence out of not having looked -- which is the exact failure of the
    // browser this replaces, with the sign flipped.
    void aSeatThatCouldNotBeReadIsNotAnAbsence()
    {
        SeatReading unread;
        unread.screen = ScreenState::On;
        const Presence presence = presenceOf(1001, true, unread);
        QVERIFY(!presence.present);
        QCOMPARE(presence.reason, Presence::Reason::Unknown);
        QVERIFY(!presence.known());
    }

    // A seat with this session on it and no screen to ask about. The seat was
    // read and the screen was not, so the answer is not known -- and it is not
    // `using` on the strength of half the facts.
    void aSeatWithNoScreenToReadIsNotKnown()
    {
        const Presence presence = presenceOf(1001, true, showing(1001, ScreenState::Unknown));
        QVERIFY(!presence.present);
        QCOMPARE(presence.reason, Presence::Reason::Unknown);
    }

    // -- the screens ---------------------------------------------------------

    // The VM, measured: one virtual connector, plugged in, lit.
    void oneLitConnectorIsAScreenThatIsOn()
    {
        makeConnector(m_box, QStringLiteral("card0-Virtual-1"), QStringLiteral("connected"),
                      QStringLiteral("enabled"), QStringLiteral("On"));
        QCOMPARE(connectedScreens(m_box), QStringList({QStringLiteral("card0-Virtual-1")}));
        QCOMPARE(screenStateFromDrm(m_box), ScreenState::On);
    }

    // The measurement that chose the filter. Hyprland turning the monitor off
    // moved `enabled` to `disabled` *and* `dpms` to `Off` in the same modeset,
    // so a walk that only looked at enabled connectors would find none and
    // answer `unknown` for the very state this exists to catch. `status` is the
    // filter; `enabled` is not.
    void aDarkConnectorIsStillConnectedAndStillCounts()
    {
        makeConnector(m_box, QStringLiteral("card0-Virtual-1"), QStringLiteral("connected"),
                      QStringLiteral("disabled"), QStringLiteral("Off"));
        QCOMPARE(connectedScreens(m_box), QStringList({QStringLiteral("card0-Virtual-1")}));
        QCOMPARE(screenStateFromDrm(m_box), ScreenState::Off);
    }

    // The development machine, measured: eight unplugged DisplayPorts that all
    // read `Off`, one lit internal panel, and a writeback connector that is not
    // a screen and answers `On` forever. Only the panel is a screen.
    void unpluggedPortsAndAWritebackAreNotScreens()
    {
        laptop(QStringLiteral("On"));
        QCOMPARE(connectedScreens(m_box), QStringList({QStringLiteral("card1-eDP-1")}));
        QCOMPARE(screenStateFromDrm(m_box), ScreenState::On);
    }

    // And with the panel dark, the answer is dark -- the writeback's permanent
    // `On` must not be able to out-vote the one screen somebody actually looks
    // at.
    void aWritebackCannotKeepADarkMachineAwake()
    {
        laptop(QStringLiteral("Off"));
        QCOMPARE(screenStateFromDrm(m_box), ScreenState::Off);
    }

    // Two monitors with one of them asleep is somebody looking at the other one.
    // Any, and never all.
    void oneLitMonitorOutOfTwoIsAScreenThatIsOn()
    {
        makeConnector(m_box, QStringLiteral("card1-DP-1"), QStringLiteral("connected"),
                      QStringLiteral("disabled"), QStringLiteral("Off"));
        makeConnector(m_box, QStringLiteral("card1-eDP-1"), QStringLiteral("connected"),
                      QStringLiteral("enabled"), QStringLiteral("On"));
        QCOMPARE(screenStateFromDrm(m_box), ScreenState::On);
    }

    // A machine with no screen on it, and a root pointed somewhere with nothing
    // in it, are the same answer: nothing was learned.
    void noConnectedScreenIsNotKnown()
    {
        QCOMPARE(screenStateFromDrm(m_box), ScreenState::Unknown);
        makeConnector(m_box, QStringLiteral("card1-DP-1"), QStringLiteral("disconnected"),
                      QStringLiteral("disabled"), QStringLiteral("Off"));
        QVERIFY(connectedScreens(m_box).isEmpty());
        QCOMPARE(screenStateFromDrm(m_box), ScreenState::Unknown);
    }

    // A card is not a connector, and the difference is not in the name: it is
    // that a connector has a `status` and a card does not. `card0`, `card0-Virtual-1`
    // and `card1-Writeback-1` are three spellings of the same shape.
    void aCardIsNotAConnector()
    {
        QVERIFY(QDir().mkpath(m_box + QStringLiteral("/card0")));
        QVERIFY(connectedScreens(m_box).isEmpty());
    }

    // The root moves by variable, exactly as the cgroup tree and the process
    // table do, and for exactly the same reason: a suite that needs a real
    // monitor to assert anything about a monitor is a suite nobody runs.
    void theRootMovesByVariable()
    {
        QCOMPARE(drmRoot(), systemDrmRoot());
        QVERIFY(drmRootIsTheSystems());
        qputenv("OMAHOUSE_DRM_ROOT", QFile::encodeName(m_box));
        QCOMPARE(drmRoot(), m_box);
        QVERIFY(!drmRootIsTheSystems());
        // Pointing it back at /sys/class/drm means what not setting it means.
        qputenv("OMAHOUSE_DRM_ROOT", QByteArray("/sys/class/drm"));
        QVERIFY(drmRootIsTheSystems());
    }

    // -- what logind was asked --------------------------------------------

    void theSeatCommandsAreTheOnesThatWereMeasured()
    {
        qputenv("OMAHOUSE_LOGINCTL", QByteArray("/tmp/no-such-loginctl"));
        QCOMPARE(seatActiveSessionCommand(QStringLiteral("seat0")),
                 QStringList({QStringLiteral("/tmp/no-such-loginctl"),
                              QStringLiteral("show-seat"), QStringLiteral("seat0"),
                              QStringLiteral("-p"), QStringLiteral("ActiveSession")}));
        QCOMPARE(sessionUserCommand(QStringLiteral("21")),
                 QStringList({QStringLiteral("/tmp/no-such-loginctl"),
                              QStringLiteral("show-session"), QStringLiteral("21"),
                              QStringLiteral("-p"), QStringLiteral("User")}));
    }

    // `show-*` output is `Key=Value` a line at a time, and an unoccupied seat
    // prints the key with nothing after it -- which is a seat that was read and
    // has nobody on it, not a seat that could not be read.
    void oneKeyIsPickedOutOfShowOutput()
    {
        const QString output = QStringLiteral("ActiveSession=21\nOther=nonsense\n");
        QCOMPARE(showProperty(output, QStringLiteral("ActiveSession")), QStringLiteral("21"));
        QCOMPARE(showProperty(QStringLiteral("ActiveSession=\n"),
                              QStringLiteral("ActiveSession")),
                 QString());
        QCOMPARE(showProperty(output, QStringLiteral("User")), QString());
        // A longer key that starts with the one being asked for is a different
        // key, and answering with its value would be answering the wrong
        // question with a confident number.
        QCOMPARE(showProperty(QStringLiteral("ActiveSessionOfSomethingElse=9\n"),
                              QStringLiteral("ActiveSession")),
                 QString());
    }

    // -- the reasons ---------------------------------------------------------

    // The names are keys in a file people read, so they round trip and they do
    // not change when somebody reorders the enumeration.
    void everyReasonHasAStableName()
    {
        const QVector<Presence::Reason> all {
            Presence::Reason::Using,        Presence::Reason::ScreenOff,
            Presence::Reason::Locked,       Presence::Reason::OtherSession,
            Presence::Reason::NoSession,    Presence::Reason::Unknown,
        };
        QStringList names;
        for (Presence::Reason reason : all) {
            const QString name = presenceReasonName(reason);
            QVERIFY2(!name.isEmpty(), qPrintable(name));
            names.append(name);
            Presence::Reason back = Presence::Reason::Unknown;
            QVERIFY2(presenceReasonFromName(name, &back), qPrintable(name));
            QCOMPARE(back, reason);
        }
        QCOMPARE(names,
                 QStringList({QStringLiteral("using"), QStringLiteral("screen-off"),
                              QStringLiteral("locked"), QStringLiteral("other-session"),
                              QStringLiteral("no-session"), QStringLiteral("unknown")}));
        QVERIFY(!presenceReasonFromName(QStringLiteral("asleep"), nullptr));
    }

    // -- the real reader, without a seat -------------------------------------

    // The class the daemon really uses, driven through the state the VM was in
    // at 20:07: this session on the seat, and every screen dark.
    void theRealReaderReadsASeatThatIsAScript()
    {
        makeConnector(m_box, QStringLiteral("card0-Virtual-1"), QStringLiteral("connected"),
                      QStringLiteral("disabled"), QStringLiteral("Off"));
        makeLoginctl(m_box, QStringLiteral("case \"$1\" in\n"
                                           "  show-seat) echo ActiveSession=21 ;;\n"
                                           "  show-session) echo User=1001 ;;\n"
                                           "esac\n"));

        SeatPresenceSource source(m_box);
        const SeatReading reading = source.readSeat();
        QVERIFY(reading.read);
        QVERIFY(reading.occupied);
        QCOMPARE(reading.uid, static_cast<uid_t>(1001));
        QCOMPARE(reading.screen, ScreenState::Off);
        QCOMPARE(presenceOf(1001, true, reading).reason, Presence::Reason::ScreenOff);
    }

    // The greeter: a seat with no session on it. Read, and empty.
    void theRealReaderSeesAnEmptySeat()
    {
        makeConnector(m_box, QStringLiteral("card0-Virtual-1"), QStringLiteral("connected"),
                      QStringLiteral("enabled"), QStringLiteral("On"));
        makeLoginctl(m_box, QStringLiteral("echo ActiveSession=\n"));

        SeatPresenceSource source(m_box);
        const SeatReading reading = source.readSeat();
        QVERIFY(reading.read);
        QVERIFY(!reading.occupied);
        QCOMPARE(reading.screen, ScreenState::On);
    }

    // A `loginctl` that fails is a seat that was not read, and never a seat with
    // nobody on it. The difference is the whole of `Unknown`: one of them is a
    // fact about the machine and the other is a fact about the reading.
    void aLoginctlThatFailsLeavesTheSeatUnread()
    {
        makeConnector(m_box, QStringLiteral("card0-Virtual-1"), QStringLiteral("connected"),
                      QStringLiteral("enabled"), QStringLiteral("On"));
        makeLoginctl(m_box, QStringLiteral("exit 1\n"));

        SeatPresenceSource source(m_box);
        const SeatReading reading = source.readSeat();
        QVERIFY(!reading.read);
        QCOMPARE(reading.screen, ScreenState::On);
        QCOMPARE(presenceOf(1001, true, reading).reason, Presence::Reason::Unknown);
    }

    // A `loginctl` that is not on the machine at all. Same answer, and it must
    // be: a daemon on a system with no logind has to go on counting, not stop.
    void aLoginctlThatIsNotThereLeavesTheSeatUnread()
    {
        qputenv("OMAHOUSE_LOGINCTL",
                QFile::encodeName(m_box + QStringLiteral("/no-loginctl-here")));
        SeatPresenceSource source(m_box);
        QVERIFY(!source.readSeat().read);
    }

    // A session whose owner is not a number is a session nobody can be compared
    // against. Read, and unoccupied -- never uid zero, which is root.
    void anUnreadableOwnerIsNotUidZero()
    {
        makeLoginctl(m_box, QStringLiteral("case \"$1\" in\n"
                                           "  show-seat) echo ActiveSession=21 ;;\n"
                                           "  show-session) echo User=nobody ;;\n"
                                           "esac\n"));
        SeatPresenceSource source(m_box);
        const SeatReading reading = source.readSeat();
        QVERIFY(reading.read);
        QVERIFY(!reading.occupied);
        QCOMPARE(presenceOf(0, true, reading).reason, Presence::Reason::OtherSession);
    }

private:
    /// The development machine's connectors, measured: eight unplugged
    /// DisplayPorts, the internal panel, and the writeback.
    void laptop(const QString &panel)
    {
        for (int i = 1; i <= 8; ++i) {
            makeConnector(m_box, QStringLiteral("card1-DP-%1").arg(i),
                          QStringLiteral("disconnected"), QStringLiteral("disabled"),
                          QStringLiteral("Off"));
        }
        makeConnector(m_box, QStringLiteral("card1-eDP-1"), QStringLiteral("connected"),
                      QStringLiteral("enabled"), panel);
        makeConnector(m_box, QStringLiteral("card1-Writeback-1"), QStringLiteral("unknown"),
                      QStringLiteral("disabled"), QStringLiteral("On"));
    }

    QTemporaryDir m_tree;
    QString m_box;
};

int runPresenceTests(int argc, char **argv)
{
    PresenceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_presence.moc"
