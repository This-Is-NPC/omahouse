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

// A scope the parser could not name, shaped like the ones this machine really
// has: 46 of these under app.slice, holding more than a hundred processes, and
// `Proc` hands them over with an empty id and the count intact.
AppScope unnamedScope(const QString &uuid, int pidCount = 1)
{
    AppScope entry;
    entry.unit = QStringLiteral("tmux-spawn-%1.scope").arg(uuid);
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

// The same noun with the other kind of selector -- docs/design.md §2. Written as
// its own helper only so the tests below read as being about a site; the
// structure it makes is a `Budget` with one field set differently.
Budget siteBudget(const QString &domain, int dailyMinutes,
                  OnExhausted onExhausted = OnExhausted::Block)
{
    Budget entry = budget(domain, domain, dailyMinutes, onExhausted);
    entry.selects = Selects::Site;
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

        // Told, and closed. docs/design.md §5 asks for both: the notification is what
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
        // is docs/design.md §4's focus profile for an adult.
        Profile denylist = profileOf();
        denylist.defaultVerdict = Verdict::Allow;
        denylist.rules = {Rule{QStringLiteral("steam"), Verdict::Deny}};

        outcome = evaluate(denylist, {chromium, steam}, startOfDay(), at(19, 0), 2);
        QCOMPARE(count(outcome.decisions, Decision::Kind::Close), 1);
        for (const Decision &decision : outcome.decisions)
            QCOMPARE(decision.scopeUnit, steam.unit);
    }

    // The session is time on the machine, and a scope nothing can name is
    // somebody on the machine.
    //
    // Measured on the development machine: 46 `tmux-spawn-<uuid>.scope` holding
    // more than a hundred processes, and an afternoon inside them debited the
    // two hour session zero seconds, because `evaluate` was handed them and
    // stepped over every one. For a child's profile that is the obvious way out
    // of the house: open a terminal and the clock stops.
    void billsTheSessionForAScopeItCannotName()
    {
        Profile profile = profileOf();
        profile.budgets = {
            budget(QStringLiteral("session"), QStringLiteral("*"), 120, OnExhausted::Logout),
            budget(QStringLiteral("chromium"), QStringLiteral("chromium"), 60,
                   OnExhausted::Close),
        };

        // Two scopes, thirty-nine processes, and not one of them with a name.
        const QVector<AppScope> terminal {
            unnamedScope(QStringLiteral("8d371e9b-645e-4030-a0d1-2243708321e2"), 20),
            unnamedScope(QStringLiteral("ef186a82-fc3d-4946-ab27-107f807a948c"), 19),
        };

        const Outcome outcome = evaluate(profile, terminal, startOfDay(), at(19, 0), 2);

        // One tick, once, for the budget whose selector is `*`.
        QCOMPARE(outcome.ledger.secondsFor(QStringLiteral("session")), 2);
        // And still once per budget rather than once per scope or per process:
        // two scopes and thirty-nine processes are two seconds, not four and not
        // seventy-eight.
        const Outcome second = evaluate(profile, terminal, outcome.ledger, at(19, 0, 2), 2);
        QCOMPARE(second.ledger.secondsFor(QStringLiteral("session")), 4);
    }

    // The other half of the same rule: `*` is not a wildcard for names, it is
    // "anything alive". A budget that names an app needs the name, and a scope
    // that has none is not that app.
    void neverBillsANamedBudgetForAScopeItCannotName()
    {
        Profile profile = profileOf();
        profile.budgets = {budget(QStringLiteral("chromium"), QStringLiteral("chromium"), 60,
                                  OnExhausted::Close)};

        const Outcome outcome =
            evaluate(profile,
                     {unnamedScope(QStringLiteral("8d371e9b-645e-4030-a0d1-2243708321e2"), 20)},
                     startOfDay(), at(19, 0), 2);

        QCOMPARE(outcome.ledger.secondsFor(QStringLiteral("chromium")), 0);
        QVERIFY2(outcome.ledger.seconds.isEmpty(),
                 "a budget about one app was billed for a scope that is not that app");
    }

    // With no id there is no rule to match, so the profile's default is the
    // whole of the answer -- and it is the answer in both directions, because a
    // policy that only ever lets these through also passes the allowlist test.
    void sendsAScopeWithNoIdToTheDefaultVerdict()
    {
        const AppScope terminal =
            unnamedScope(QStringLiteral("8d371e9b-645e-4030-a0d1-2243708321e2"), 20);

        // A denylist. Nothing names it, so nothing refuses it, and the rule
        // about `steam` is not a rule about a nameless scope.
        Profile denylist = profileOf();
        denylist.defaultVerdict = Verdict::Allow;
        denylist.rules = {Rule{QStringLiteral("steam"), Verdict::Deny}};

        const Outcome allowed = evaluate(denylist, {terminal}, startOfDay(), at(19, 0), 2);
        QVERIFY2(allowed.decisions.isEmpty(),
                 "a nameless scope was acted on under a default of allow");

        // An allowlist, with the teeth in. The consequence is meant: a thing
        // nobody can even name is certainly not on the list of what was
        // released, and `chromium` being on that list does not cover it.
        Profile allowlist = profileOf();
        allowlist.defaultVerdict = Verdict::Deny;
        allowlist.enforce = true;
        allowlist.rules = {Rule{QStringLiteral("chromium"), Verdict::Allow}};

        const Outcome denied = evaluate(allowlist, {terminal}, startOfDay(), at(19, 0), 2);
        QCOMPARE(denied.decisions.size(), 2);
        QCOMPARE(denied.decisions.at(0).kind, Decision::Kind::Warn);
        QCOMPARE(denied.decisions.at(0).reason, Decision::Reason::Denied);
        QCOMPARE(denied.decisions.at(0).scopeUnit, terminal.unit);
        QCOMPARE(denied.decisions.at(1).kind, Decision::Kind::Close);
        QCOMPARE(denied.decisions.at(1).scopeUnit, terminal.unit);
    }

    // And the calibration window holds for it like everything else. A profile
    // that is only observing closes nothing, which is what makes `enforce:
    // false` the default a new profile is born with: a day of the report before
    // the teeth, and the terminal not shut in somebody's face on day one.
    void holdsBackTheCloseOfANamelessScopeWithoutEnforce()
    {
        Profile profile = profileOf();
        profile.defaultVerdict = Verdict::Deny;
        profile.enforce = false;

        const Outcome outcome =
            evaluate(profile,
                     {unnamedScope(QStringLiteral("8d371e9b-645e-4030-a0d1-2243708321e2"), 20)},
                     startOfDay(), at(19, 0), 2);

        QCOMPARE(count(outcome.decisions, Decision::Kind::Close), 0);
        // Said, though: the refusal is what turns a rule into something the
        // operator can read on the report before switching the teeth on.
        QCOMPARE(count(outcome.decisions, Decision::Kind::Warn), 1);
        QCOMPARE(outcome.decisions.at(0).reason, Decision::Reason::Denied);
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
    // docs/design.md §5's default for a profile that has just been created.
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
    // open. docs/design.md §7: an operator who cannot do that is not an operator.
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

    // -- a budget on a site ---------------------------------------------------
    //
    // docs/design.md §5.2 counted the number and acted on nothing; these are the
    // stage after. What they are really asserting is that the model held: the
    // marks, the grace and the memory of what was already said are the app
    // half's machinery, reached with a domain instead of a scope id.

    // The site in front spends its budget, and nothing else does. The app
    // running the browser is billed for running -- §5 -- and the two numbers do
    // not touch.
    void billsTheSiteInFrontAndOnlyTheSiteInFront()
    {
        Profile profile = profileOf();
        profile.budgets = {
            budget(QStringLiteral("chromium"), QStringLiteral("chromium"), 0, OnExhausted::Warn),
            siteBudget(QStringLiteral("youtube.com"), 30),
            siteBudget(QStringLiteral("wikipedia.org"), 30),
        };

        const Outcome outcome = evaluate(profile, {scope(QStringLiteral("chromium"))},
                                         startOfDay(), at(19, 0), 2,
                                         QStringLiteral("youtube.com"));

        QCOMPARE(outcome.ledger.secondsFor(QStringLiteral("youtube.com")), 2);
        QCOMPARE(outcome.ledger.secondsFor(QStringLiteral("wikipedia.org")), 0);
        QCOMPARE(outcome.ledger.secondsFor(QStringLiteral("chromium")), 2);
        // And the observing number of §5.2, which goes on being written whether
        // or not anything has a budget about it.
        QCOMPARE(outcome.ledger.siteSecondsFor(QStringLiteral("youtube.com")), 2);
    }

    // The crossing of §5.2, from this side of the line: the caller hands over a
    // domain or nothing, and nothing is what a dark screen, a stale file, a
    // browser that is gone and a machine with no extension all look like here.
    void billsNoSiteWhenTheCallerNamesNone()
    {
        Profile profile = profileOf();
        profile.budgets = {siteBudget(QStringLiteral("youtube.com"), 30)};

        const Outcome outcome =
            evaluate(profile, {scope(QStringLiteral("chromium"))}, startOfDay(), at(19, 0), 2);

        QVERIFY(outcome.ledger.seconds.isEmpty());
        QVERIFY(outcome.ledger.sites.isEmpty());
        QVERIFY(outcome.decisions.isEmpty());
    }

    // The guard that would be expensive to get wrong. A site budget matching `*`
    // is a limit on browsing at all, and `*` is also the selector the session
    // budget uses -- so a shared loop would have this one billed by every scope
    // on the machine and, when it ran out, closing every one of them.
    void aSiteBudgetMatchingEverythingNeverTouchesAnApp()
    {
        Profile profile = profileOf();
        profile.budgets = {siteBudget(QStringLiteral("*"), 1)};

        // Ten scopes open and nothing in the browser: not a second spent.
        Outcome outcome = evaluate(profile, {scope(QStringLiteral("chromium"), 21),
                                             scope(QStringLiteral("code")),
                                             unnamedScope(QStringLiteral("a"), 71)},
                                   startOfDay(), at(19, 0), 2);
        QVERIFY2(outcome.ledger.seconds.isEmpty(),
                 "a budget about sites was spent by an app scope");

        // And out of time, with the same scopes open: it blocks, and it closes
        // nothing and ends nobody's session.
        Ledger spent = startOfDay();
        spent.seconds.insert(QStringLiteral("*"), 60);
        outcome = evaluate(profile, {scope(QStringLiteral("chromium"), 21)}, spent, at(19, 0), 2,
                           QStringLiteral("youtube.com"));
        QCOMPARE(count(outcome.decisions, Decision::Kind::Close), 0);
        QCOMPARE(count(outcome.decisions, Decision::Kind::Logout), 0);
        QCOMPARE(count(outcome.decisions, Decision::Kind::Block), 1);
    }

    // Runs out, warns at each mark once, waits out the grace, and then blocks --
    // and every one of those is the app half's machinery, unbranched.
    void runsASiteOutAndBlocksItAfterTheGrace()
    {
        Profile profile = profileOf();
        profile.warnAt = {5, 1};
        profile.graceSeconds = 20;
        profile.budgets = {siteBudget(QStringLiteral("youtube.com"), 30)};

        Ledger ledger = startOfDay();
        // Twenty-five minutes in: the five minute mark, said once.
        ledger.seconds.insert(QStringLiteral("youtube.com"), 25 * 60);
        const Outcome first =
            evaluate(profile, {}, ledger, at(19, 0), 2, QStringLiteral("youtube.com"));
        QCOMPARE(count(first.decisions, Decision::Kind::Warn), 1);
        QCOMPARE(first.decisions.first().reason, Decision::Reason::Warning);
        const Outcome again =
            evaluate(profile, {}, first.ledger, at(19, 0, 2), 2, QStringLiteral("youtube.com"));
        QVERIFY2(again.decisions.isEmpty(), "the five minute mark was said twice");

        // Out of time: the last word first, and nothing done yet.
        Ledger out = startOfDay();
        out.seconds.insert(QStringLiteral("youtube.com"), 30 * 60);
        const Outcome opens =
            evaluate(profile, {}, out, at(19, 30), 2, QStringLiteral("youtube.com"));
        QCOMPARE(count(opens.decisions, Decision::Kind::Warn), 1);
        QCOMPARE(opens.decisions.first().reason, Decision::Reason::GraceStarted);
        QCOMPARE(count(opens.decisions, Decision::Kind::Block), 0);

        // Inside the window, still nothing.
        const Outcome waiting =
            evaluate(profile, {}, opens.ledger, at(19, 30, 10), 2, QStringLiteral("youtube.com"));
        QCOMPARE(count(waiting.decisions, Decision::Kind::Block), 0);

        // And past it, the block -- named by the selector, which is what the
        // browser has to be told.
        const Outcome acts =
            evaluate(profile, {}, waiting.ledger, at(19, 30, 20), 2, QStringLiteral("youtube.com"));
        QCOMPARE(count(acts.decisions, Decision::Kind::Block), 1);
        QCOMPARE(acts.decisions.first().site, QStringLiteral("youtube.com"));
        QCOMPARE(acts.decisions.first().budgetId, QStringLiteral("youtube.com"));
    }

    // A site that is out of time goes on being out of time with nobody at the
    // keyboard and nothing in the front tab, because the block belongs to the
    // day. This is the zero-tick question `watch` and `saveProfiles` both ask,
    // and it is what makes the turn of the date let the site back through
    // without anybody having to remember to.
    void aSiteStaysBlockedWithNothingInFrontAndComesBackWithTheDay()
    {
        Profile profile = profileOf();
        profile.budgets = {siteBudget(QStringLiteral("youtube.com"), 30)};

        Ledger out = startOfDay();
        out.seconds.insert(QStringLiteral("youtube.com"), 30 * 60);

        const Outcome standing = evaluate(profile, {}, out, at(21, 0), 0);
        QCOMPARE(count(standing.decisions, Decision::Kind::Block), 1);
        QVERIFY2(standing.ledger.sites.isEmpty(), "a zero tick with no site billed one");

        // The next day. Same ledger handed in, and the balance is a new file.
        const Outcome tomorrow =
            evaluate(profile, {}, out,
                     QDateTime(QDate(2026, 9, 4), QTime(8, 0)), 0);
        QCOMPARE(count(tomorrow.decisions, Decision::Kind::Block), 0);

        // And a grant does the same thing before the day turns.
        Ledger granted = out;
        Grant more;
        more.at = at(21, 0);
        more.by = QStringLiteral("howl");
        more.budget = QStringLiteral("youtube.com");
        more.minutes = 10;
        granted.grants.append(more);
        QCOMPARE(count(evaluate(profile, {}, granted, at(21, 0), 0).decisions,
                       Decision::Kind::Block),
                 0);
    }

    // Observing, for sites too. `enforce: false` counts the site and blocks
    // nothing, which is what §5.2 shipped and what an operator gets before they
    // decide the number is worth teeth.
    void neverBlocksWithoutEnforce()
    {
        Profile profile = profileOf();
        profile.enforce = false;
        profile.budgets = {siteBudget(QStringLiteral("youtube.com"), 30)};

        Ledger out = startOfDay();
        out.seconds.insert(QStringLiteral("youtube.com"), 30 * 60);

        const Outcome outcome =
            evaluate(profile, {}, out, at(19, 30), 2, QStringLiteral("youtube.com"));

        QCOMPARE(count(outcome.decisions, Decision::Kind::Block), 0);
        // Said, once, and with no window in front of it: there is nothing to
        // wait for when nothing is going to happen.
        QCOMPARE(count(outcome.decisions, Decision::Kind::Warn), 1);
        QCOMPARE(outcome.decisions.first().reason, Decision::Reason::Exhausted);
        // And still counting, because observing is counting.
        QCOMPARE(outcome.ledger.secondsFor(QStringLiteral("youtube.com")), 30 * 60 + 2);
    }

    // A site budget whose action is `warn` is its own last word, exactly as an
    // app budget's is: the number runs out, the person is told, and the site
    // goes on opening.
    void aSiteBudgetThatOnlyWarnsBlocksNothing()
    {
        Profile profile = profileOf();
        profile.budgets = {siteBudget(QStringLiteral("youtube.com"), 30, OnExhausted::Warn)};

        Ledger out = startOfDay();
        out.seconds.insert(QStringLiteral("youtube.com"), 30 * 60);

        const Outcome outcome =
            evaluate(profile, {}, out, at(19, 30), 2, QStringLiteral("youtube.com"));
        QCOMPARE(count(outcome.decisions, Decision::Kind::Block), 0);
        QCOMPARE(count(outcome.decisions, Decision::Kind::Warn), 1);
    }
};

int runPolicyTests(int argc, char **argv)
{
    PolicyTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_policy.moc"
