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

    // Handing over ten minutes is the one verb that has to work while nobody is
    // listening, and on an enrolled profile it did nothing at all. The balance
    // came from the statement -- credit less what the others spent -- and a
    // grant was in neither number, so `grant` wrote the ledger, printed the
    // same balance back and changed nothing. The minutes arrived at the next
    // plan, if there was one.
    //
    // The reason it could not simply be added is that the manager folds every
    // grant it collects into `credit`, so a machine adding its own on top would
    // count them twice the moment the next statement landed. So the statement
    // says how much of *this machine's* credit it has already counted, and what
    // is added here is the rest. Exactly once, by subtraction and not by
    // comparing clocks.
    void aGrantTakesEffectHereBeforeTheHouseholdHearsAboutIt()
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
        a.addSeconds("session", 1800); b.addSeconds("session", 1800);

        QJsonObject plan;
        QString error;
        QVERIFY2(planAllocations(profile, {{"here", a}, {"b", b}}, {}, now, &plan, &error),
                 qPrintable(error));
        const auto documents = plan.value("documents").toObject();
        QVERIFY2(applyAllocation(&profile, documents.value("here").toObject(), now.date(), &error),
                 qPrintable(error));
        // Two hours, half an hour spent on each machine: an hour left here.
        QCOMPARE(allowanceSeconds(profile, budget, a, now.date()) - a.secondsFor("session"), 3600);

        // Nothing has been counted from here yet, and the statement says so.
        QCOMPARE(documents.value("here").toObject().value("house").toObject()
                     .value("session").toObject().value("counted").toInt(-1), 0);

        // Ten minutes handed over on this machine, with the manager none the
        // wiser. It is ten minutes *here*, now.
        a.grants.append(Grant{now, "operator", "session", 10});
        QCOMPARE(allowanceSeconds(profile, budget, a, now.date()) - a.secondsFor("session"), 4200);

        // And when the household does hear about it, it is still ten minutes
        // and not twenty: the credit rose by six hundred and so did what the
        // statement says it has counted from here.
        QJsonObject next;
        b.allocation = documents.value("b").toObject();
        a.allocation = profile.allocation;
        QVERIFY2(planAllocations(profile, {{"here", a}, {"b", b}}, plan, now, &next, &error),
                 qPrintable(error));
        const auto after = next.value("documents").toObject().value("here").toObject();
        const auto one = after.value("house").toObject().value("session").toObject();
        QCOMPARE(one.value("credit").toInt(), 7800);
        QCOMPARE(one.value("counted").toInt(), 600);
        QVERIFY2(applyAllocation(&profile, after, now.date(), &error), qPrintable(error));
        QCOMPARE(allowanceSeconds(profile, budget, a, now.date()) - a.secondsFor("session"), 4200);

        // The other machine is told about the credit and counted none of it,
        // because `counted` is about that machine's own grants and not the
        // household's.
        const auto elsewhere = next.value("documents").toObject().value("b").toObject()
                                   .value("house").toObject().value("session").toObject();
        QCOMPARE(elsewhere.value("credit").toInt(), 7800);
        QCOMPARE(elsewhere.value("counted").toInt(), 0);

        // A statement that says it has counted less than the last one is a
        // report going backwards, and honouring it would hand this machine the
        // same ten minutes a second time. The same rule `elsewhere` keeps.
        auto backwards = after;
        auto house = backwards.value("house").toObject();
        auto session = house.value("session").toObject();
        session.insert("counted", 0);
        house.insert("session", session);
        backwards.insert("house", house);
        backwards.insert("revision", after.value("revision").toInt() + 1);
        QVERIFY2(!applyAllocation(&profile, backwards, now.date(), &error),
                 "a statement unlearned what it had counted and the grant came back");

        // A budget with no `counted` at all is refused where the document is
        // read. It would be taken for a zero, which is the shape that hands
        // over every grant this machine has made a second time.
        auto silent = after;
        auto quiet = silent.value("house").toObject();
        auto without = quiet.value("session").toObject();
        without.remove("counted");
        quiet.insert("session", without);
        silent.insert("house", quiet);
        QVERIFY(!validAllocation(silent, &error));
    }

    // A statement is for a day, and the day after it there is none. Until this
    // was written that left an enrolled machine at nothing at all, and nothing
    // an operator could type changed it -- which is the worst moment for the
    // one verb that exists so somebody can hand over ten minutes with the
    // manager unreachable.
    //
    // So on a day the household has said nothing about, the allowance is
    // exactly what an operator has handed over and no daily number at all. It
    // cannot leak time: there is no floor under it but zero, and the only thing
    // that raises it is somebody deciding to.
    void withNoStatementForTodayAnOperatorCanStillHandOverTen()
    {
        Profile profile;
        profile.user = "kid";
        profile.allocation = QJsonObject{{"authority", "manager"}, {"machine", "here"}};
        Budget budget;
        budget.id = "session"; budget.match = {QStringLiteral("*")}; budget.dailyMinutes = 120;
        budget.onExhausted = OnExhausted::Logout;
        profile.budgets << budget;
        const QDateTime now(QDate(2026, 9, 7), QTime(12, 0), QTimeZone::UTC);

        Ledger a;
        a.user = "kid"; a.date = now.date(); a.observedAt = now;
        a.allocation = profile.allocation;
        QJsonObject plan;
        QString error;
        QVERIFY2(planAllocations(profile, {{"here", a}}, {}, now, &plan, &error),
                 qPrintable(error));
        QVERIFY(applyAllocation(&profile, plan.value("documents").toObject()
                                    .value("here").toObject(), now.date(), &error));

        // Tomorrow, with no statement for it. Nothing, as before.
        const QDate tomorrow = now.date().addDays(1);
        Ledger fresh;
        fresh.user = "kid"; fresh.date = tomorrow;
        QCOMPARE(allowanceSeconds(profile, budget, fresh, tomorrow), 0);

        // Ten minutes is ten minutes, and the two hours the household has not
        // granted are still not granted.
        fresh.grants.append(Grant{QDateTime(tomorrow, QTime(9, 0), QTimeZone::UTC),
                                  "operator", "session", 10});
        QCOMPARE(allowanceSeconds(profile, budget, fresh, tomorrow), 600);

        // A local adjustment is not credit and never was. `leave` is refused on
        // an enrolled profile for that reason, and this is the same rule where
        // the number is worked out.
        fresh.grants.append(Grant{QDateTime(tomorrow, QTime(9, 5), QTimeZone::UTC),
                                  "operator", "session", 45, true});
        QCOMPARE(allowanceSeconds(profile, budget, fresh, tomorrow), 600);
    }

    // A pot has no day, and a statement is made of days.
    //
    // The guard that returns the day's household allowance to zero when the
    // statement is not for today is right, and it is right about a *daily*
    // budget: the turn of the date is exactly when the household should have
    // spoken again, and a machine out of contact must not re-issue an allowance
    // that is the household's to give. A pot is not that. It does not recharge
    // at midnight, so there is no new allowance to issue -- what is left of it
    // was decided once and already belongs to the person. Applying the guard to
    // one confused *the household has not spoken about today* with *the credit
    // is finished*, and those are different things, one of which cannot happen
    // to a pot.
    //
    // The network drops, midnight passes, and an hour and a half of a two hour
    // pot is still an hour and a half. That is the whole of it.
    void anEnrolledPotIsSpentToZeroWhileTheHouseholdIsSilent()
    {
        Profile profile;
        profile.user = "kid";
        profile.allocation = QJsonObject{{"authority", "manager"}, {"machine", "here"}};
        Budget pot;
        pot.id = "pot"; pot.match = {QStringLiteral("chromium")}; pot.dailyMinutes = 120;
        pot.resets = Resets::Never; pot.onExhausted = OnExhausted::Close;
        Budget session;
        session.id = "session"; session.match = {QStringLiteral("*")};
        session.dailyMinutes = 120; session.onExhausted = OnExhausted::Logout;
        profile.budgets << pot << session;
        const QDateTime now(QDate(2026, 9, 7), QTime(12, 0), QTimeZone::UTC);

        Ledger today;
        today.user = "kid"; today.date = now.date(); today.observedAt = now;
        today.allocation = profile.allocation;
        today.addKeptSeconds("pot", 1800);
        today.addSeconds("session", 600);

        QJsonObject plan;
        QString error;
        QVERIFY2(planAllocations(profile, {{"here", today}}, {}, now, &plan, &error),
                 qPrintable(error));
        QVERIFY2(applyAllocation(&profile, plan.value("documents").toObject()
                                     .value("here").toObject(), now.date(), &error),
                 qPrintable(error));

        // Half an hour of the pot gone, an hour and a half left.
        QCOMPARE(allowanceSeconds(profile, pot, today, now.date())
                     - spentSeconds(pot, today), 5400);

        // The line goes down. Midnight passes, the pot walks into the new day
        // and the statement stays yesterday's.
        const QDate tomorrow = now.date().addDays(1);
        Ledger next = carryInto(tomorrow, today, profile);
        QCOMPARE(next.keptSecondsFor("pot"), 1800);
        QCOMPARE(allowanceSeconds(profile, pot, next, tomorrow) - spentSeconds(pot, next), 5400);

        // A week of silence is the same answer. Nothing about a pot is measured
        // in days, so nothing about it can go stale.
        const QDate later = now.date().addDays(7);
        Ledger week = next;
        week.date = later;
        QCOMPARE(allowanceSeconds(profile, pot, week, later) - spentSeconds(pot, week), 5400);

        // And it can be spent to zero out there, with no floor under it but
        // zero and no household to ask.
        week.addKeptSeconds("pot", 5400);
        QCOMPARE(allowanceSeconds(profile, pot, week, later) - spentSeconds(pot, week), 0);

        // The guard has not moved for the budget it was written about. The
        // session's allowance is the household's to give, and on a day the
        // household has said nothing about there is none.
        QCOMPARE(allowanceSeconds(profile, session, next, tomorrow), 0);

        // Except what an operator handed over, which is the rule the day before
        // this one established and is not disturbed here.
        next.grants.append(Grant{QDateTime(tomorrow, QTime(9, 0), QTimeZone::UTC),
                                 "operator", "session", 10});
        QCOMPARE(allowanceSeconds(profile, session, next, tomorrow), 600);
    }

    // Handing over more is the only thing that refills a pot, and the enrolled
    // path could not see it either -- the fold that carries a grant across a
    // night is read on one side of this function and not the other. A pot
    // refilled on Monday came into Tuesday on an enrolled machine with Monday's
    // spending against Monday's smaller allowance.
    void aRefillOnAnEnrolledPotSurvivesTheNightToo()
    {
        Profile profile;
        profile.user = "kid";
        profile.allocation = QJsonObject{{"authority", "manager"}, {"machine", "here"}};
        Budget pot;
        pot.id = "pot"; pot.match = {QStringLiteral("chromium")}; pot.dailyMinutes = 120;
        pot.resets = Resets::Never; pot.onExhausted = OnExhausted::Close;
        profile.budgets << pot;
        const QDateTime now(QDate(2026, 9, 7), QTime(12, 0), QTimeZone::UTC);

        Ledger monday;
        monday.user = "kid"; monday.date = now.date(); monday.observedAt = now;
        monday.allocation = profile.allocation;
        monday.addKeptSeconds("pot", 7200);
        monday.grants.append(Grant{now, "operator", "pot", 30});

        // Spent to the last second, and half an hour handed over.
        QCOMPARE(allowanceSeconds(profile, pot, monday, now.date())
                     - spentSeconds(pot, monday), 1800);

        const QDate tuesday = now.date().addDays(1);
        const Ledger next = carryInto(tuesday, monday, profile);
        QCOMPARE(next.keptGrantedFor("pot"), 1800);
        QCOMPARE(allowanceSeconds(profile, pot, next, tuesday) - spentSeconds(pot, next), 1800);
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
                                    QJsonObject{{"credit", credit}, {"elsewhere", elsewhere},
                                                {"counted", 0}}}}}};
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
                              QJsonObject{{"credit", 7200}, {"elsewhere", 7200},
                                          {"counted", 0}}}}}};
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

    // A pot the whole household shares, which is the case the product is for
    // and the one that did not work.
    //
    // The plan was written for a thing that recharges. It summed `secondsFor`,
    // which is zero for every pot there has ever been, so `elsewhere` came out
    // zero on every machine and each was told the whole pot; and it rebuilt
    // `credit` from the daily number every morning, so every refill older than
    // one night was forgotten. Refills crossed between computers and spending
    // did not, which is the direction that gives time away.
    //
    // Neither number is a day. What a pot has spent is its running total and
    // what it has been given is the fold beside it, and a collected day is a
    // whole ledger and brings both.
    void aPotIsOnePotAcrossTheWholeHouse()
    {
        Profile profile;
        profile.user = "kid";
        profile.allocation = QJsonObject{{"authority", "manager"}, {"machine", "here"}};
        Budget pot;
        pot.id = "pot"; pot.match = {QStringLiteral("chromium")}; pot.dailyMinutes = 120;
        pot.resets = Resets::Never; pot.onExhausted = OnExhausted::Close;
        profile.budgets << pot;
        const QDateTime now(QDate(2026, 9, 7), QTime(12, 0), QTimeZone::UTC);

        Ledger a, b;
        a.user = b.user = "kid"; a.date = b.date = now.date();
        a.observedAt = b.observedAt = now;
        a.allocation = profile.allocation;
        b.allocation = QJsonObject{{"authority", "manager"}, {"machine", "b"}};
        a.addKeptSeconds("pot", 1800);
        b.addKeptSeconds("pot", 900);

        QJsonObject plan;
        QString error;
        QVERIFY2(planAllocations(profile, {{"here", a}, {"b", b}}, {}, now, &plan, &error),
                 qPrintable(error));
        const auto documents = plan.value("documents").toObject();
        const auto here = documents.value("here").toObject().value("house").toObject()
                              .value("pot").toObject();
        const auto there = documents.value("b").toObject().value("house").toObject()
                               .value("pot").toObject();

        // Two hours of pot, and each is told what the other has spent of it --
        // all of it, and not what it spent today, because a pot has no today.
        QCOMPARE(here.value("credit").toInt(), 7200);
        QCOMPARE(there.value("credit").toInt(), 7200);
        QCOMPARE(here.value("elsewhere").toInt(), 900);
        QCOMPARE(there.value("elsewhere").toInt(), 1800);

        // And both work out the same balance, which is the household's: three
        // quarters of an hour gone between them.
        QVERIFY2(applyAllocation(&profile, documents.value("here").toObject(), now.date(),
                                 &error), qPrintable(error));
        QCOMPARE(allowanceSeconds(profile, pot, a, now.date()) - spentSeconds(pot, a), 4500);

        Profile other = profile;
        other.allocation = QJsonObject{{"authority", "manager"}, {"machine", "b"}};
        QVERIFY2(applyAllocation(&other, documents.value("b").toObject(), now.date(), &error),
                 qPrintable(error));
        QCOMPARE(allowanceSeconds(other, pot, b, now.date()) - spentSeconds(pot, b), 4500);

        // Spending on one machine moves the other machine's balance. That is
        // the whole point, and a pot could not do it.
        b.addKeptSeconds("pot", 600);
        QJsonObject next;
        a.allocation = profile.allocation;
        b.allocation = documents.value("b").toObject();
        QVERIFY2(planAllocations(profile, {{"here", a}, {"b", b}}, plan, now, &next, &error),
                 qPrintable(error));
        const auto moved = next.value("documents").toObject().value("here").toObject();
        QCOMPARE(moved.value("house").toObject().value("pot").toObject()
                     .value("elsewhere").toInt(), 1500);
        QVERIFY(applyAllocation(&profile, moved, now.date(), &error));
        QCOMPARE(allowanceSeconds(profile, pot, a, now.date()) - spentSeconds(pot, a), 3900);
    }

    // A refill outlives the night on the way round the house too. Handing over
    // half an hour is the only thing that puts anything back in a pot, and the
    // fold that carries it past midnight lives on each machine -- so the plan
    // has to ask for it, or every top-up older than one night is forgotten by
    // everybody but the computer it was typed on.
    void arefillOnOneMachineIsCreditForTheWholeHouse()
    {
        Profile profile;
        profile.user = "kid";
        profile.allocation = QJsonObject{{"authority", "manager"}, {"machine", "here"}};
        Budget pot;
        pot.id = "pot"; pot.match = {QStringLiteral("chromium")}; pot.dailyMinutes = 120;
        pot.resets = Resets::Never; pot.onExhausted = OnExhausted::Close;
        profile.budgets << pot;
        const QDateTime now(QDate(2026, 9, 7), QTime(12, 0), QTimeZone::UTC);

        Ledger a, b;
        a.user = b.user = "kid"; a.date = b.date = now.date();
        a.observedAt = b.observedAt = now;
        a.allocation = profile.allocation;
        b.allocation = QJsonObject{{"authority", "manager"}, {"machine", "b"}};
        // Half an hour handed over here last week and folded by the nights
        // since, and ten minutes handed over today.
        a.addKeptGranted("pot", 1800);
        a.grants.append(Grant{now, "operator", "pot", 10});

        QJsonObject plan;
        QString error;
        QVERIFY2(planAllocations(profile, {{"here", a}, {"b", b}}, {}, now, &plan, &error),
                 qPrintable(error));
        const auto documents = plan.value("documents").toObject();
        const auto there = documents.value("b").toObject().value("house").toObject()
                               .value("pot").toObject();

        // Three hours: the pot's two, plus the half hour and the ten minutes.
        QCOMPARE(there.value("credit").toInt(), 7200 + 1800 + 600);
        // None of it came from that machine, so it has counted none of it.
        QCOMPARE(there.value("counted").toInt(), 0);
        // And all of it came from this one.
        QCOMPARE(documents.value("here").toObject().value("house").toObject()
                     .value("pot").toObject().value("counted").toInt(), 2400);

        QVERIFY2(applyAllocation(&profile, documents.value("here").toObject(), now.date(),
                                 &error), qPrintable(error));
        QCOMPARE(allowanceSeconds(profile, pot, a, now.date()), 9600);

        // The night passes with no new statement. The ten minutes fold into the
        // running total, the statement stops counting them separately, and the
        // number does not move.
        const QDate tomorrow = now.date().addDays(1);
        Ledger next = carryInto(tomorrow, a, profile);
        QCOMPARE(next.keptGrantedFor("pot"), 2400);
        QCOMPARE(allowanceSeconds(profile, pot, next, tomorrow), 9600);
    }

    // A pot in the household's day, added up the way the statement adds it up.
    //
    // This is the row that kept `omahouse house` and the machines panel from
    // agreeing with anything: both show what each computer spent, and read out
    // of the daily counter a pot was nothing spent on every one of them, for
    // ever. The capacity over it has to come from the same place the machines
    // are told -- everything the pot has ever been handed, not what somebody
    // handed it since midnight -- or the household says one number here and a
    // different one to each machine.
    void aPotIsAddedUpTheWayTheStatementAddsItUp()
    {
        Profile profile;
        profile.user = "kid";
        Budget pot;
        pot.id = "pot"; pot.match = {QStringLiteral("chromium")}; pot.dailyMinutes = 120;
        pot.resets = Resets::Never; pot.onExhausted = OnExhausted::Close;
        Budget session;
        session.id = "session"; session.match = {QStringLiteral("*")};
        session.dailyMinutes = 120; session.onExhausted = OnExhausted::Logout;
        profile.budgets << pot << session;

        Ledger here, there;
        here.user = there.user = "kid";
        here.date = there.date = QDate(2026, 9, 7);
        here.addKeptSeconds("pot", 1800);
        here.addSeconds("session", 600);
        there.addKeptSeconds("pot", 900);
        there.addSeconds("session", 300);
        // Half an hour handed over on the far machine a week ago, folded by the
        // nights since. Read out of today's grants alone it would be gone.
        there.addKeptGranted("pot", 1800);

        const auto house = consolidate(profile, {{"here", here}, {"b", there}});
        const HouseBudget *jar = nullptr;
        const HouseBudget *daily = nullptr;
        for (const auto &one : house) {
            if (one.id == "pot") jar = &one;
            if (one.id == "session") daily = &one;
        }
        QVERIFY(jar && daily);

        // Half an hour here, a quarter there, and the sum of the two.
        QCOMPARE(jar->spent.size(), 2);
        QCOMPARE(jar->spent.first().seconds, 1800);
        QCOMPARE(jar->spent.last().seconds, 900);
        QCOMPARE(jar->totalSeconds, 2700);

        // Two hours of pot, three quarters of an hour gone between them, and
        // the capacity counts a refill that outlived the night it was made in.
        QVERIFY(jar->hasLimit());
        QCOMPARE(jar->limitSeconds, 9000);
        QCOMPARE(jar->leftSeconds(), 6300);

        // The daily budget beside it is untouched: a household number, and what
        // is left of it after both computers.
        QVERIFY(daily->hasLimit());
        QCOMPARE(daily->limitSeconds, 7200);
        QCOMPARE(daily->totalSeconds, 900);
        QCOMPARE(daily->leftSeconds(), 6300);
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
