#include "Fleet.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace omahouse;

/// The household's list of computers, which omahouse never had.
///
/// Everything here is about the file being legible and honest: a household
/// edits it by hand, so what it refuses matters more than what it accepts.
class FleetTest : public QObject
{
    Q_OBJECT

private slots:
    void aMachineSurvivesTheFile()
    {
        Machine machine;
        machine.name = QStringLiteral("the kitchen laptop");
        machine.nodeId = QStringLiteral("omk1_1c6eeda1420f17b356d");
        machine.endpoint = QStringLiteral("192.168.1.20:7879");
        machine.addedAt = QDateTime(QDate(2026, 9, 6), QTime(10, 0));

        Machine back;
        QString error;
        QVERIFY2(Machine::fromJson(machine.toJson(), &back, &error), qPrintable(error));
        QCOMPARE(back.name, machine.name);
        QCOMPARE(back.nodeId, machine.nodeId);
        QCOMPARE(back.endpoint, machine.endpoint);
        QCOMPARE(back.addedAt, machine.addedAt);
        QVERIFY(back.reachable());
    }

    void aMachineWrittenDownBeforeItIsPairedIsNotAFailure()
    {
        // The state between "we own that computer" and "it answers". A name on
        // its own is a note to self, and the file says so rather than carrying
        // two empty strings for somebody to wonder about.
        Machine noted;
        noted.name = QStringLiteral("the spare");
        const QJsonObject written = noted.toJson();
        QVERIFY(!written.contains(QStringLiteral("nodeId")));
        QVERIFY(!written.contains(QStringLiteral("endpoint")));

        Machine back;
        QString error;
        QVERIFY(Machine::fromJson(written, &back, &error));
        QVERIFY(!back.reachable());
    }

    void refusesAMachineWithNoName()
    {
        // The name is the handle every verb takes, so a nameless entry is a row
        // nothing can ever be said about.
        Machine out;
        QString error;
        QVERIFY(!Machine::fromJson(QJsonObject {{QStringLiteral("nodeId"),
                                                 QStringLiteral("omk1_a")}},
                                   &out, &error));
        QVERIFY(error.contains(QStringLiteral("no name")));
    }

    void refusesADateThatIsNotOne()
    {
        Machine out;
        QString error;
        QVERIFY(!Machine::fromJson(QJsonObject {{QStringLiteral("name"),
                                                 QStringLiteral("one")},
                                                {QStringLiteral("addedAt"),
                                                 QStringLiteral("yesterday")}},
                                   &out, &error));
        QVERIFY2(error.contains(QStringLiteral("not a date")), qPrintable(error));
    }

    void refusesTwoMachinesOfOneName()
    {
        // A household that cannot say which computer it means. Refused where the
        // file is read rather than left for a verb to trip over later.
        const QJsonObject root {
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("machines"),
             QJsonArray {QJsonObject {{QStringLiteral("name"), QStringLiteral("twin")}},
                         QJsonObject {{QStringLiteral("name"), QStringLiteral("twin")}}}},
        };
        QVector<Machine> out;
        QString error;
        QVERIFY(!machinesFromJson(root, &out, &error));
        QVERIFY2(error.contains(QStringLiteral("twice")), qPrintable(error));
    }

    void refusesASchemaVersionItDoesNotKnow()
    {
        QVector<Machine> out;
        QString error;
        QVERIFY(!machinesFromJson(QJsonObject {{QStringLiteral("schemaVersion"), 99}},
                                  &out, &error));
    }

    void aFileThatIsNotThereIsAHouseholdOfOne()
    {
        // Most households are one computer, and one that has never been told
        // about another is not misconfigured.
        QTemporaryDir dir;
        QVector<Machine> out;
        QString error;
        bool missing = false;
        QVERIFY2(readMachines(dir.filePath(QStringLiteral("machines.json")), &out, &error,
                              &missing),
                 qPrintable(error));
        QVERIFY(missing);
        QVERIFY(out.isEmpty());
    }

    void theWholeListSurvivesTheDisk()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("machines.json"));
        QVector<Machine> written;
        Machine one;
        one.name = QStringLiteral("desk");
        one.nodeId = QStringLiteral("omk1_one");
        one.endpoint = QStringLiteral("10.0.0.2:7879");
        Machine two;
        two.name = QStringLiteral("the spare");
        written << one << two;

        QString error;
        QVERIFY2(writeMachines(path, written, &error), qPrintable(error));
        QVector<Machine> back;
        QVERIFY2(readMachines(path, &back, &error), qPrintable(error));
        QCOMPARE(back.size(), 2);
        QCOMPARE(back.at(0).endpoint, one.endpoint);
        QVERIFY(back.at(0).reachable());
        QVERIFY(!back.at(1).reachable());
        QCOMPARE(indexOfMachine(back, QStringLiteral("the spare")), 1);
        QCOMPARE(indexOfMachine(back, QStringLiteral("nobody")), -1);
    }
};

#include "tst_fleet.moc"

int runFleetTests(int argc, char **argv)
{
    FleetTest tests;
    return QTest::qExec(&tests, argc, argv);
}
