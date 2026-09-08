#include "Fleet.h"
#include "Allocation.h"
#include "Policy.h"
#include <QTimeZone>

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
    void allocationsReserveCreditAndRejectReplay()
    {
        Profile profile;
        profile.user = "kid";
        profile.allocation = QJsonObject{{"authority", "manager"}, {"machine", "here"}};
        Budget budget;
        budget.id = "session"; budget.match = {QStringLiteral("*")}; budget.dailyMinutes = 120;
        budget.onExhausted = OnExhausted::Logout;
        profile.budgets << budget;
        const QDateTime now(QDate(2026, 9, 7), QTime(12, 0), QTimeZone::UTC);
        Ledger a, b;
        a.user = b.user = "kid"; a.date = b.date = now.date();
        a.observedAt = b.observedAt = now;
        a.allocation = profile.allocation;
        b.allocation = QJsonObject{{"authority", "manager"}, {"machine", "b"}};
        a.addSeconds("session", 1800); b.addSeconds("session", 2400);
        QJsonObject plan;
        QString error;
        QVERIFY2(planAllocations(profile, {{"here", a}, {"b", b}}, {}, now, &plan, &error), qPrintable(error));
        auto documents = plan.value("documents").toObject();
        QCOMPARE(documents.size(), 2);
        QCOMPARE(documents.value("here").toObject().value("limits").toObject().value("session").toInt(), 3300);
        QCOMPARE(documents.value("b").toObject().value("limits").toObject().value("session").toInt(), 3900);
        QVERIFY(applyAllocation(&profile, documents.value("here").toObject(), now.date(), &error));
        const auto applied = profile.allocation;
        QVERIFY(applyAllocation(&profile, applied, now.date(), &error));
        a.addSeconds("session", 60);
        QCOMPARE(allowanceSeconds(profile, budget, a, now.date()) - a.secondsFor("session"), 1440);
        QCOMPARE(allowanceSeconds(profile, budget, a, now.date().addDays(1)), 0);
        QJsonObject next;
        a.allocation = profile.allocation;
        b.allocation = documents.value("b").toObject();
        QVERIFY(planAllocations(profile, {{"here", a}, {"b", b}}, plan, now, &next, &error));
        QCOMPARE(next, plan); // Consumption/retry does not refill a portion.
        QVERIFY(!planAllocations(profile, {{"here", a}, {"b", b}}, {}, now, &next, &error));
        b.observedAt = now.addSecs(-121);
        QVERIFY(!planAllocations(profile, {{"here", a}, {"b", b}}, plan, now, &next, &error));
        b.observedAt = now;
        QVERIFY(!planAllocations(profile, {{"here", a}}, plan, now, &next, &error));
        a.grants.append(Grant{now, "operator", "session", 10});
        QVERIFY(planAllocations(profile, {{"here", a}, {"b", b}}, plan, now, &next, &error));
        auto newer = next.value("documents").toObject().value("here").toObject();
        const auto oldRemote = b.allocation;
        b.allocation = next.value("documents").toObject().value("b").toObject();
        QJsonObject refusedPlan;
        QVERIFY(!planAllocations(profile, {{"here", a}, {"b", b}}, plan, now, &refusedPlan, &error));
        b.allocation = oldRemote;
        auto corrupt = plan;
        corrupt.insert("revision", 0);
        QVERIFY(!planAllocations(profile, {{"here", a}, {"b", b}}, corrupt, now, &refusedPlan, &error));
        QCOMPARE(newer.value("limits").toObject().value("session").toInt(), 3600);
        QVERIFY(applyAllocation(&profile, newer, now.date(), &error));
        QVERIFY(!applyAllocation(&profile, applied, now.date(), &error));
        newer.insert("authority", "other");
        QVERIFY(!applyAllocation(&profile, newer, now.date(), &error));
        newer = profile.allocation;
        newer.insert("limits", QJsonObject{{"session", 9000}});
        QVERIFY(!applyAllocation(&profile, newer, now.date(), &error));
        QCOMPARE(profile.allocation.value("limits").toObject().value("session").toInt(), 3600);
    }

    void allocationZeroExhaustsAndStandaloneStillWorks()
    {
        Profile profile;
        profile.user = "kid"; profile.enforce = true;
        Budget budget;
        budget.id = "session"; budget.match = {QStringLiteral("*")}; budget.dailyMinutes = 120;
        budget.onExhausted = OnExhausted::Logout;
        profile.budgets << budget;
        const QDateTime now(QDate(2026, 9, 7), QTime(12, 0), QTimeZone::UTC);
        Ledger day; day.user = "kid"; day.date = now.date();
        QCOMPARE(allowanceSeconds(profile, budget, day, now.date()), 7200);
        profile.allocation = QJsonObject{{"authority", "manager"}, {"machine", "here"}};
        QCOMPARE(allowanceSeconds(profile, budget, day, now.date()), 0);
        auto outcome = evaluate(profile, {}, day, now, 0);
        QVERIFY(!outcome.decisions.isEmpty());
        bool logout = false;
        for (const auto &decision : outcome.decisions)
            logout |= decision.kind == Decision::Kind::Logout;
        QVERIFY(logout);
        QString error;
        QJsonObject zero{{"user", "kid"}, {"authority", "manager"}, {"machine", "here"},
                         {"date", now.date().toString(Qt::ISODate)}, {"revision", 1},
                         {"limits", QJsonObject{{"session", 0}}}};
        QVERIFY(applyAllocation(&profile, zero, now.date(), &error));
        QCOMPARE(allowanceSeconds(profile, budget, day, now.date()), 0);
        zero.insert("date", now.date().addDays(1).toString(Qt::ISODate));
        QVERIFY(!applyAllocation(&profile, zero, now.date(), &error));
    }

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

    // -- what the house spent ------------------------------------------------

    void addsOneBudgetAcrossTheMachines()
    {
        // The profile's number is the household's: two hours in the house, not
        // two hours per computer.
        Profile profile;
        profile.user = QStringLiteral("kid");
        Budget session;
        session.id = QStringLiteral("session");
        session.match = {QStringLiteral("*")};
        session.dailyMinutes = 120;
        profile.budgets << session;

        Ledger here;
        here.addSeconds(QStringLiteral("session"), 1800);
        Ledger laptop;
        laptop.addSeconds(QStringLiteral("session"), 2400);

        const QVector<HouseBudget> house = consolidate(
            profile, {{QStringLiteral("here"), here}, {QStringLiteral("laptop"), laptop}});
        QCOMPARE(house.size(), 1);
        QCOMPARE(house.at(0).totalSeconds, 4200);
        QCOMPARE(house.at(0).limitSeconds, 7200);
        QCOMPARE(house.at(0).leftSeconds(), 3000);
        QCOMPARE(house.at(0).spent.size(), 2);
        QCOMPARE(house.at(0).spent.at(1).machine, QStringLiteral("laptop"));
        QCOMPARE(house.at(0).spent.at(1).seconds, 2400);
    }

    void aGrantOnOneMachineRaisesTheHouseholdNumber()
    {
        // A grant of ten minutes on the laptop raised the laptop's limit, and
        // what the house has to know is that the day's number moved. Not adding
        // them would make `leave` fight every grant an operator gives.
        Profile profile;
        Budget session;
        session.id = QStringLiteral("session");
        session.dailyMinutes = 60;
        profile.budgets << session;

        Ledger laptop;
        laptop.grants.append(Grant {QDateTime::currentDateTime(), QStringLiteral("root"),
                                    QStringLiteral("session"), 10});

        const QVector<HouseBudget> house =
            consolidate(profile, {{QStringLiteral("laptop"), laptop}});
        QCOMPARE(house.at(0).limitSeconds, 60 * 60 + 600);
    }

    void aBudgetWithNoLimitIsAddedAndNeverRunsOut()
    {
        Profile profile;
        Budget counted;
        counted.id = QStringLiteral("code");
        profile.budgets << counted;

        Ledger here;
        here.addSeconds(QStringLiteral("code"), 900);
        const QVector<HouseBudget> house =
            consolidate(profile, {{QStringLiteral("here"), here}});
        QCOMPARE(house.at(0).totalSeconds, 900);
        QVERIFY(!house.at(0).hasLimit());
        QCOMPARE(house.at(0).leftSeconds(), 0);
    }

    void aHouseThatWentOverHasNothingLeftAndNotLessThanNothing()
    {
        // A negative would only invite somebody to subtract it twice.
        Profile profile;
        Budget session;
        session.id = QStringLiteral("session");
        session.dailyMinutes = 30;
        profile.budgets << session;

        Ledger here;
        here.addSeconds(QStringLiteral("session"), 3600);
        const QVector<HouseBudget> house =
            consolidate(profile, {{QStringLiteral("here"), here}});
        QCOMPARE(house.at(0).totalSeconds, 3600);
        QCOMPARE(house.at(0).leftSeconds(), 0);
    }

    void aMachineThatSpentNothingIsStillInTheRow()
    {
        // Zero from a machine is a fact -- nobody used it today -- and leaving
        // the column out would make the row read as though it had not been
        // asked.
        Profile profile;
        Budget session;
        session.id = QStringLiteral("session");
        session.dailyMinutes = 60;
        profile.budgets << session;

        Ledger here;
        here.addSeconds(QStringLiteral("session"), 600);
        const QVector<HouseBudget> house = consolidate(
            profile, {{QStringLiteral("here"), here}, {QStringLiteral("spare"), Ledger {}}});
        QCOMPARE(house.at(0).spent.size(), 2);
        QCOMPARE(house.at(0).spent.at(1).seconds, 0);
        QCOMPARE(house.at(0).totalSeconds, 600);
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
