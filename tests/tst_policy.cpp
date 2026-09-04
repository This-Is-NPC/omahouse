#include <QtTest>

#include "Policy.h"

using namespace omahouse;

namespace {

// A scope shaped like the ones the PoC measured, so the tests judge the same
// strings `Proc` will hand over in stage 3.
AppScope scope(const QString &id, int pidCount = 1)
{
    AppScope entry;
    entry.id = id;
    entry.unit = QStringLiteral("app-Hyprland-%1-031bdc27.scope").arg(id);
    entry.cgroupPath = QStringLiteral("/app.slice/app-graphical.slice/") + entry.unit;
    entry.pidCount = pidCount;
    return entry;
}

// The clock the whole suite runs on. Nobody reads the real one: `now` is an
// argument, so a budget that takes two hours to run out takes two lines.
QDateTime at(int hour, int minute, int second = 0)
{
    return QDateTime(QDate(2026, 9, 3), QTime(hour, minute, second));
}

Ledger startOfDay(const QString &user = QStringLiteral("julia"))
{
    Ledger ledger;
    ledger.user = user;
    ledger.date = QDate(2026, 9, 3);
    return ledger;
}

Budget budget(const QString &id, const QString &match, int dailyMinutes, OnExhausted onExhausted)
{
    Budget entry;
    entry.id = id;
    entry.match = match;
    entry.dailyMinutes = dailyMinutes;
    entry.onExhausted = onExhausted;
    return entry;
}

Profile profileOf(const QString &user = QStringLiteral("julia"))
{
    Profile profile;
    profile.user = user;
    profile.enabled = true;
    profile.enforce = true;
    return profile;
}

int count(const QVector<Decision> &decisions, Decision::Kind kind)
{
    int total = 0;
    for (const Decision &decision : decisions) {
        if (decision.kind == kind)
            ++total;
    }
    return total;
}

} // namespace

// The verdicts, the balance, and the moment the two of them turn into something
// the caller has to do.
class PolicyTest : public QObject {
    Q_OBJECT

private slots:
    // Both directions of the default, because a policy that only ever says
    // `deny` also passes the allowlist test.
    void letsThroughWhatTheDefaultAllows()
    {
        Profile profile = profileOf();
        profile.defaultVerdict = Verdict::Allow;

        const Outcome outcome =
            evaluate(profile, {scope(QStringLiteral("chromium"))}, startOfDay(), at(19, 0), 2);

        QVERIFY2(outcome.decisions.isEmpty(),
                 "an unmatched app was acted on under a default of allow");
    }

    void refusesWhatTheDefaultDenies()
    {
        Profile profile = profileOf();
        profile.defaultVerdict = Verdict::Deny;
        const AppScope steam = scope(QStringLiteral("steam"));

        const Outcome outcome = evaluate(profile, {steam}, startOfDay(), at(19, 0), 2);

        // Told, and closed. spec.md §5 asks for both: the notification is what
        // turns a window vanishing into a rule the user can name.
        QCOMPARE(outcome.decisions.size(), 2);
        QCOMPARE(outcome.decisions.at(0).kind, Decision::Kind::Warn);
        QCOMPARE(outcome.decisions.at(0).reason, Decision::Reason::Denied);
        QCOMPARE(outcome.decisions.at(0).scopeUnit, steam.unit);
        QCOMPARE(outcome.decisions.at(1).kind, Decision::Kind::Close);
        QCOMPARE(outcome.decisions.at(1).scopeUnit, steam.unit);

        // The second tick still tries to close it -- a SIGTERM that has not
        // landed yet has to be followed up -- and does not say it twice.
        const Outcome again = evaluate(profile, {steam}, outcome.ledger, at(19, 0, 2), 2);
        QCOMPARE(count(again.decisions, Decision::Kind::Warn), 0);
        QCOMPARE(count(again.decisions, Decision::Kind::Close), 1);
    }

    void letsAnExplicitRuleBeatTheDefault()
    {
        const AppScope chromium = scope(QStringLiteral("chromium"));
        const AppScope steam = scope(QStringLiteral("steam"));

        // An allowlist: deny by default, one program named.
        Profile allowlist = profileOf();
        allowlist.defaultVerdict = Verdict::Deny;
        allowlist.rules = {Rule{QStringLiteral("chromium"), Verdict::Allow}};

        Outcome outcome = evaluate(allowlist, {chromium, steam}, startOfDay(), at(19, 0), 2);
        QCOMPARE(count(outcome.decisions, Decision::Kind::Close), 1);
        for (const Decision &decision : outcome.decisions)
            QCOMPARE(decision.scopeUnit, steam.unit);

        // A denylist: the same engine with the default the other way up, which
        // is spec.md §4's focus profile for an adult.
        Profile denylist = profileOf();
        denylist.defaultVerdict = Verdict::Allow;
        denylist.rules = {Rule{QStringLiteral("steam"), Verdict::Deny}};

        outcome = evaluate(denylist, {chromium, steam}, startOfDay(), at(19, 0), 2);
        QCOMPARE(count(outcome.decisions, Decision::Kind::Close), 1);
        for (const Decision &decision : outcome.decisions)
            QCOMPARE(decision.scopeUnit, steam.unit);
    }

    // Two hours of session, proved without waiting two hours: the ledger is
    // handed the seconds already spent and `now` is whatever the test says.
    void runsABudgetOutAndActsOnIt()
    {
        Profile profile = profileOf();
        profile.budgets = {budget(QStringLiteral("session"), QStringLiteral("*"), 120,
                                  OnExhausted::Logout)};

        Ledger ledger = startOfDay();
        ledger.seconds.insert(QStringLiteral("session"), 120 * 60 - 2);

        const Outcome outcome =
            evaluate(profile, {scope(QStringLiteral("chromium"))}, ledger, at(19, 0), 2);

        QCOMPARE(outcome.ledger.secondsFor(QStringLiteral("session")), 120 * 60);
        QCOMPARE(count(outcome.decisions, Decision::Kind::Logout), 1);
        QCOMPARE(outcome.decisions.last().budgetId, QStringLiteral("session"));
        // And the day remembers when it ran out, which is what the grace window
        // is measured from and what the report reads back.
        QCOMPARE(outcome.ledger.exhaustedAt(QStringLiteral("session")), at(19, 0));
    }

    // One warning per mark, and not one per tick. A tick is two seconds long, so
    // the difference between the two is five minutes of notifications.
    void warnsOncePerMarkAndNotOncePerTick()
    {
        Profile profile = profileOf();
        profile.warnAt = {10, 5, 1};
        profile.budgets = {budget(QStringLiteral("minecraft"),
                                  QStringLiteral("minecraft-launcher"), 45, OnExhausted::Close)};
        const AppScope minecraft = scope(QStringLiteral("minecraft-launcher"));

        // 5:02 left, so the 10 minute mark is already behind and the tick lands
        // on the 5 minute one.
        Ledger ledger = startOfDay();
        ledger.seconds.insert(QStringLiteral("minecraft"), 45 * 60 - 302);
        ledger.events.append(Event{at(19, 20), EventKind::Warn, QStringLiteral("minecraft"),
                                   QString(), 10});

        const Outcome first = evaluate(profile, {minecraft}, ledger, at(19, 25), 2);
        QCOMPARE(first.decisions.size(), 1);
        QCOMPARE(first.decisions.at(0).kind, Decision::Kind::Warn);
        QCOMPARE(first.decisions.at(0).reason, Decision::Reason::Warning);
        QCOMPARE(first.decisions.at(0).budgetId, QStringLiteral("minecraft"));
        QCOMPARE(first.decisions.at(0).secondsLeft, 300);

        // The next tick is inside the same minute and still under five minutes
        // left. Nothing is said.
        const Outcome second = evaluate(profile, {minecraft}, first.ledger, at(19, 25, 2), 2);
        QVERIFY2(second.decisions.isEmpty(), "the five minute mark was announced twice");
        QCOMPARE(second.ledger.secondsFor(QStringLiteral("minecraft")), 45 * 60 - 298);

        // And the mark below it still gets its own warning when it arrives.
        Ledger nearlyOut = second.ledger;
        nearlyOut.seconds.insert(QStringLiteral("minecraft"), 45 * 60 - 62);
        const Outcome third = evaluate(profile, {minecraft}, nearlyOut, at(19, 29), 2);
        QCOMPARE(third.decisions.size(), 1);
        QCOMPARE(third.decisions.at(0).reason, Decision::Reason::Warning);
        QCOMPARE(third.decisions.at(0).secondsLeft, 60);
    }

    // The window between running out and the action. The caller has to be able
    // to tell "say it closes in twenty seconds" from "close it now", and the
    // stamp it measures from lives in the ledger, so a daemon restarted in the
    // middle of the window does not restart the window.
    void holdsTheGraceWindowBeforeActing()
    {
        Profile profile = profileOf();
        profile.graceSeconds = 20;
        profile.budgets = {budget(QStringLiteral("minecraft"),
                                  QStringLiteral("minecraft-launcher"), 45, OnExhausted::Close)};
        const AppScope minecraft = scope(QStringLiteral("minecraft-launcher"));

        Ledger ledger = startOfDay();
        ledger.seconds.insert(QStringLiteral("minecraft"), 45 * 60 - 2);

        const Outcome opens = evaluate(profile, {minecraft}, ledger, at(19, 30, 0), 2);
        QCOMPARE(opens.decisions.size(), 1);
        QCOMPARE(opens.decisions.at(0).kind, Decision::Kind::Warn);
        QCOMPARE(opens.decisions.at(0).reason, Decision::Reason::GraceStarted);
        QCOMPARE(opens.decisions.at(0).secondsLeft, 20);

        // Mid window: nothing to do, and nothing said a second time.
        const Outcome waiting = evaluate(profile, {minecraft}, opens.ledger, at(19, 30, 10), 2);
        QVERIFY2(waiting.decisions.isEmpty(), "it acted, or spoke again, inside the grace window");

        // The window closes on the second it was promised for.
        const Outcome acts = evaluate(profile, {minecraft}, waiting.ledger, at(19, 30, 20), 2);
        QCOMPARE(acts.decisions.size(), 1);
        QCOMPARE(acts.decisions.at(0).kind, Decision::Kind::Close);
        QCOMPARE(acts.decisions.at(0).reason, Decision::Reason::Exhausted);
        QCOMPARE(acts.decisions.at(0).scopeUnit, minecraft.unit);
        QCOMPARE(acts.decisions.at(0).budgetId, QStringLiteral("minecraft"));

        // A budget that closes takes every scope it matches, one decision each,
        // because a close is a write to one scope's cgroup.kill.
        Profile everything = profile;
        everything.budgets = {
            budget(QStringLiteral("session"), QStringLiteral("*"), 45, OnExhausted::Close)};
        everything.graceSeconds = 0;
        Ledger spent = startOfDay();
        spent.seconds.insert(QStringLiteral("session"), 45 * 60);
        const Outcome all = evaluate(everything,
                                     {minecraft, scope(QStringLiteral("chromium"))}, spent,
                                     at(19, 30), 2);
        QCOMPARE(count(all.decisions, Decision::Kind::Close), 2);
    }

    // Observation mode. It counts, it says so, and it never closes anything --
    // spec.md §5's default for a profile that has just been created.
    void neverClosesOrLogsOutWithoutEnforce()
    {
        Profile profile = profileOf();
        profile.enforce = false;
        profile.defaultVerdict = Verdict::Deny;
        profile.graceSeconds = 20;
        profile.budgets = {
            budget(QStringLiteral("session"), QStringLiteral("*"), 120, OnExhausted::Logout)};

        Ledger ledger = startOfDay();
        ledger.seconds.insert(QStringLiteral("session"), 120 * 60 - 2);

        const Outcome outcome =
            evaluate(profile, {scope(QStringLiteral("chromium"))}, ledger, at(19, 0), 2);

        QCOMPARE(count(outcome.decisions, Decision::Kind::Close), 0);
        QCOMPARE(count(outcome.decisions, Decision::Kind::Logout), 0);
        QCOMPARE(count(outcome.decisions, Decision::Kind::Warn), 2);
        // A refused app and a spent session, both said once and neither acted
        // on. No grace either: there is nothing to hold back.
        QCOMPARE(outcome.decisions.at(0).reason, Decision::Reason::Denied);
        QCOMPARE(outcome.decisions.at(1).reason, Decision::Reason::Exhausted);

        // And the counting carries on past the limit, which is the whole point
        // of a day of observation: the report has to show the overrun.
        const Outcome later =
            evaluate(profile, {scope(QStringLiteral("chromium"))}, outcome.ledger, at(19, 0, 2), 2);
        QVERIFY2(later.decisions.isEmpty(), "observation mode repeated itself");
        QCOMPARE(later.ledger.secondsFor(QStringLiteral("session")), 120 * 60 + 2);
    }

    // A budget with no limit is a counter somebody wanted a number from, not a
    // rule. It never runs out, and it never warns.
    void countsABudgetWithNoLimitWithoutRunningItOut()
    {
        Profile profile = profileOf();
        profile.warnAt = {5};
        profile.budgets = {budget(QStringLiteral("chromium"), QStringLiteral("chromium"), 0,
                                  OnExhausted::Close)};

        Ledger ledger = startOfDay();
        ledger.seconds.insert(QStringLiteral("chromium"), 8 * 3600);

        const Outcome outcome =
            evaluate(profile, {scope(QStringLiteral("chromium"))}, ledger, at(19, 0), 2);

        QCOMPARE(outcome.ledger.secondsFor(QStringLiteral("chromium")), 8 * 3600 + 2);
        QVERIFY2(outcome.decisions.isEmpty(), "a budget with no limit ran out");
        QVERIFY(!outcome.ledger.exhaustedAt(QStringLiteral("chromium")).isValid());
    }

    // The operator hands over ten minutes with the game open, and the game stays
    // open. spec.md §7: an operator who cannot do that is not an operator.
    void addsAGrantToTheLimit()
    {
        Profile profile = profileOf();
        profile.budgets = {budget(QStringLiteral("minecraft"),
                                  QStringLiteral("minecraft-launcher"), 45, OnExhausted::Close)};
        const AppScope minecraft = scope(QStringLiteral("minecraft-launcher"));

        Ledger ledger = startOfDay();
        ledger.seconds.insert(QStringLiteral("minecraft"), 45 * 60 - 2);

        const Outcome without = evaluate(profile, {minecraft}, ledger, at(19, 30), 2);
        QCOMPARE(count(without.decisions, Decision::Kind::Close), 1);

        Ledger granted = ledger;
        granted.grants.append(Grant{at(19, 12, 4), QStringLiteral("howl"),
                                    QStringLiteral("minecraft"), 10});

        const Outcome with = evaluate(profile, {minecraft}, granted, at(19, 30), 2);
        QVERIFY2(with.decisions.isEmpty(), "a granted ten minutes did not extend the budget");
        // Ten minutes on, the granted time is gone too and the close lands.
        Ledger spent = with.ledger;
        spent.seconds.insert(QStringLiteral("minecraft"), 55 * 60);
        const Outcome after = evaluate(profile, {minecraft}, spent, at(19, 40), 2);
        QCOMPARE(count(after.decisions, Decision::Kind::Close), 1);
    }

    // Off is off: a disabled profile is counted for nothing and decided about
    // for nothing, and still gets a ledger dated today so the caller writes the
    // file it expects.
    void doesNothingForADisabledProfile()
    {
        Profile profile = profileOf();
        profile.enabled = false;
        profile.defaultVerdict = Verdict::Deny;
        profile.budgets = {
            budget(QStringLiteral("session"), QStringLiteral("*"), 1, OnExhausted::Logout)};

        const Outcome outcome =
            evaluate(profile, {scope(QStringLiteral("chromium"))}, startOfDay(), at(19, 0), 2);

        QVERIFY(outcome.decisions.isEmpty());
        QVERIFY(outcome.ledger.seconds.isEmpty());
        QCOMPARE(outcome.ledger.date, QDate(2026, 9, 3));
    }
};

int runPolicyTests(int argc, char **argv)
{
    PolicyTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_policy.moc"
