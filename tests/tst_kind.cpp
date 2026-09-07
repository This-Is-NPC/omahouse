#include "Kind.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace omahouse;

/// What this machine is in the household.
///
/// It was implicit until now -- a machine became a manager by running one verb
/// and managed by running another -- and every case here is about the same
/// thing: that a person can ask, and that a half-written answer is refused
/// rather than read as the default.
class KindTest : public QObject
{
    Q_OBJECT

private slots:
    void aloneIsTheDefaultAndNotAFailure()
    {
        // Most households are one computer, and one computer under rules is the
        // whole product working. A machine that has never been linked is not
        // misconfigured.
        const ThisMachine machine;
        QCOMPARE(machine.kind, Kind::Alone);
        QVERIFY(machine.managedBy.isEmpty());
    }

    void everyKindSurvivesItsName()
    {
        for (const Kind kind : {Kind::Alone, Kind::Manager, Kind::Managed}) {
            Kind back;
            QVERIFY2(kindFromName(kindName(kind), &back), qPrintable(kindName(kind)));
            QCOMPARE(back, kind);
            QVERIFY(!kindSaid(kind).isEmpty());
        }
    }

    void amachineSurvivesTheFile()
    {
        ThisMachine machine;
        machine.kind = Kind::Managed;
        machine.name = QStringLiteral("the kitchen laptop");
        machine.managedBy = QStringLiteral("omk1_1c6eeda142");
        machine.since = QDateTime(QDate(2026, 9, 6), QTime(10, 0));

        ThisMachine back;
        QString error;
        QVERIFY2(ThisMachine::fromJson(machine.toJson(), &back, &error),
                 qPrintable(error));
        QCOMPARE(back.kind, machine.kind);
        QCOMPARE(back.name, machine.name);
        QCOMPARE(back.managedBy, machine.managedBy);
        QCOMPARE(back.since, machine.since);
    }

    void amanagerNamesNobodyAsItsManager()
    {
        ThisMachine machine;
        machine.kind = Kind::Manager;
        machine.name = QStringLiteral("the study");

        ThisMachine back;
        QString error;
        QVERIFY2(ThisMachine::fromJson(machine.toJson(), &back, &error),
                 qPrintable(error));
        QVERIFY(back.managedBy.isEmpty());
    }

    void managedWithNoManagerIsRefused()
    {
        // A half-written link. The verbs that follow would look for a manager
        // that is not named anywhere, and would each fail differently.
        ThisMachine back;
        QString error;
        QVERIFY(!ThisMachine::fromJson(QJsonObject {
                                               {QStringLiteral("schemaVersion"), 1},
                                               {QStringLiteral("kind"),
                                                QStringLiteral("managed")}},
                                       &back, &error));
        QVERIFY2(error.contains(QStringLiteral("names no manager")), qPrintable(error));
    }

    void akindNobodyHereKnowsIsRefused()
    {
        // A file from a build that knew something this one does not. Reading it
        // as `alone` would drop a machine out of a household it is really in.
        ThisMachine back;
        QString error;
        QVERIFY(!ThisMachine::fromJson(QJsonObject {
                                               {QStringLiteral("schemaVersion"), 1},
                                               {QStringLiteral("kind"),
                                                QStringLiteral("overlord")}},
                                       &back, &error));
        QVERIFY2(error.contains(QStringLiteral("overlord")), qPrintable(error));
    }

    void adateThatIsNotOneIsRefused()
    {
        ThisMachine back;
        QString error;
        QVERIFY(!ThisMachine::fromJson(QJsonObject {
                                               {QStringLiteral("schemaVersion"), 1},
                                               {QStringLiteral("kind"),
                                                QStringLiteral("manager")},
                                               {QStringLiteral("since"),
                                                QStringLiteral("yesterday")}},
                                       &back, &error));
        QVERIFY2(error.contains(QStringLiteral("yesterday")), qPrintable(error));
    }

    void afileThatIsNotThereIsAMachineOnItsOwn()
    {
        QTemporaryDir tmp;
        ThisMachine machine;
        QString error;
        bool missing = false;
        QVERIFY2(readThisMachine(tmp.filePath(QStringLiteral("machine.json")),
                                 &machine, &error, &missing),
                 qPrintable(error));
        QVERIFY(missing);
        QCOMPARE(machine.kind, Kind::Alone);
    }

    void itSurvivesTheDisk()
    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath(QStringLiteral("machine.json"));
        ThisMachine written;
        written.kind = Kind::Manager;
        written.name = QStringLiteral("the study");
        written.since = QDateTime(QDate(2026, 9, 6), QTime(10, 0));

        QString error;
        QVERIFY2(writeThisMachine(path, written, &error), qPrintable(error));
        ThisMachine back;
        bool missing = true;
        QVERIFY2(readThisMachine(path, &back, &error, &missing), qPrintable(error));
        QVERIFY(!missing);
        QCOMPARE(back.kind, Kind::Manager);
        QCOMPARE(back.name, written.name);
    }
};

#include "tst_kind.moc"

int runKindTests(int argc, char **argv)
{
    KindTest tests;
    return QTest::qExec(&tests, argc, argv);
}
