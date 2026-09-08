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
    // One pot, and every machine told the same thing about it.
    //
    // The manager divided the credit into an exclusive portion per computer,
    // and that is exactly what stopped an hour being an hour wherever the
    // person sat: time reserved on the machine in the bedroom was time the one
    // in the kitchen could not spend. Now each is told the household's credit
    // and what the *others* have spent of it, and works out the same balance.
    void everyMachineIsToldTheSameBalance()
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
        QVERIFY2(planAllocations(profile, {{"here", a}, {"b", b}}, {}, now, &plan, &error),
                 qPrintable(error));
        const auto documents = plan.value("documents").toObject();
        QCOMPARE(documents.size(), 2);

        // Two hours of credit, and each is told what the other spent.
        const auto here = documents.value("here").toObject().value("house").toObject()
                              .value("session").toObject();
        const auto there = documents.value("b").toObject().value("house").toObject()
                               .value("session").toObject();
        QCOMPARE(here.value("credit").toInt(), 7200);
        QCOMPARE(there.value("credit").toInt(), 7200);
        QCOMPARE(here.value("elsewhere").toInt(), 2400);
        QCOMPARE(there.value("elsewhere").toInt(), 1800);

        // And both work out the same balance, which is the household's. Under
        // the portions this was 1500 on one and 1500 on the other, and neither
        // could touch the other's.
        QVERIFY(applyAllocation(&profile, documents.value("here").toObject(), now.date(), &error));
        QCOMPARE(allowanceSeconds(profile, budget, a, now.date()) - a.secondsFor("session"), 3000);

        Profile other = profile;
        other.allocation = QJsonObject{{"authority", "manager"}, {"machine", "b"}};
        QVERIFY2(applyAllocation(&other, documents.value("b").toObject(), now.date(), &error),
                 qPrintable(error));
        QCOMPARE(allowanceSeconds(other, budget, b, now.date()) - b.secondsFor("session"), 3000);

        // Tomorrow's statement is nobody's allowance today.
        QCOMPARE(allowanceSeconds(profile, budget, a, now.date().addDays(1)), 0);

        // Applying the same document twice is not more time, and the plan does
        // not move when nothing has been spent since.
        QVERIFY(applyAllocation(&profile, profile.allocation, now.date(), &error));
        QCOMPARE(allowanceSeconds(profile, budget, a, now.date()) - a.secondsFor("session"), 3000);
        QJsonObject next;
        a.allocation = profile.allocation;
        b.allocation = documents.value("b").toObject();
        QVERIFY(planAllocations(profile, {{"here", a}, {"b", b}}, plan, now, &next, &error));
        QCOMPARE(next, plan);

        // Spending on one machine moves the other machine's balance, which is
        // the whole point and the thing portions could not do.
        b.addSeconds("session", 600);
        QVERIFY2(planAllocations(profile, {{"here", a}, {"b", b}}, plan, now, &next, &error),
                 qPrintable(error));
        const auto moved = next.value("documents").toObject().value("here").toObject();
        QCOMPARE(moved.value("house").toObject().value("session").toObject()
                     .value("elsewhere").toInt(), 3000);
        QVERIFY(applyAllocation(&profile, moved, now.date(), &error));
        QCOMPARE(allowanceSeconds(profile, budget, a, now.date()) - a.secondsFor("session"), 2400);

        // A grant anywhere is credit everywhere.
        a.grants.append(Grant{now, "operator", "session", 10});
        QVERIFY2(planAllocations(profile, {{"here", a}, {"b", b}}, next, now, &next, &error),
                 qPrintable(error));
        QCOMPARE(next.value("documents").toObject().value("b").toObject()
                     .value("house").toObject().value("session").toObject()
                     .value("credit").toInt(), 7800);

        // The refusals that were there before and are not about portions.
        QJsonObject refused;
        QVERIFY2(!planAllocations(profile, {{"here", a}, {"b", b}}, {}, now, &refused, &error),
                 "a manager with no plan issued one over machines that already hold today's");
        b.observedAt = now.addSecs(-121);
        QVERIFY2(!planAllocations(profile, {{"here", a}, {"b", b}}, next, now, &refused, &error),
                 "a day older than two minutes was planned against");
        b.observedAt = now;
        QVERIFY2(!planAllocations(profile, {{"here", a}}, next, now, &refused, &error),
                 "membership changed inside the day");
        auto corrupt = next;
        corrupt.insert("revision", 0);
        QVERIFY(!planAllocations(profile, {{"here", a}, {"b", b}}, corrupt, now, &refused, &error));
    }

    // `elsewhere` is what other computers have spent, and consumption adds up.
    // A statement saying they spent less than the last one is a report going
    // backwards, and honouring it would hand this machine the same minutes
    // twice. `credit` is a decision and is free to move either way.
    void aStatementMayLowerTheCreditAndNeverTheSpending()
    {
        Profile profile;
        profile.user = "kid";
        profile.allocation = QJsonObject{{"authority", "manager"}, {"machine", "here"}};
        Budget budget;
        budget.id = "session"; budget.match = {QStringLiteral("*")}; budget.dailyMinutes = 120;
        profile.budgets << budget;
        const QDate today(2026, 9, 7);
        QString error;

        const auto statement = [&](int revision, int credit, int elsewhere) {
            return QJsonObject{{"authority", "manager"}, {"machine", "here"},
                               {"user", "kid"}, {"date", today.toString(Qt::ISODate)},
                               {"revision", revision},
                               {"house", QJsonObject{{"session",
                                    QJsonObject{{"credit", credit}, {"elsewhere", elsewhere}}}}}};
        };

        QVERIFY2(applyAllocation(&profile, statement(1, 7200, 1200), today, &error),
                 qPrintable(error));
        Ledger day; day.user = "kid"; day.date = today;
        QCOMPARE(allowanceSeconds(profile, budget, day, today), 6000);

        // The operator lowered the day's number at four in the afternoon.
        QVERIFY2(applyAllocation(&profile, statement(2, 3600, 1200), today, &error),
                 qPrintable(error));
        QCOMPARE(allowanceSeconds(profile, budget, day, today), 2400);

        // And the household spending it has gone backwards, which it cannot.
        QVERIFY2(!applyAllocation(&profile, statement(3, 3600, 600), today, &error),
                 "a statement reporting less spent elsewhere was honoured");
        QVERIFY2(error.contains(QStringLiteral("backwards")), qPrintable(error));

        // A stale revision is refused whatever it says.
        QVERIFY(!applyAllocation(&profile, statement(1, 7200, 5000), today, &error));

        // More spent elsewhere than the household has is nobody's minutes back.
        QVERIFY2(applyAllocation(&profile, statement(3, 3600, 9000), today, &error),
                 qPrintable(error));
        QCOMPARE(allowanceSeconds(profile, budget, day, today), 0);
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
                         {"house", QJsonObject{{"session",
                              QJsonObject{{"credit", 7200}, {"elsewhere", 7200}}}}}};
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
