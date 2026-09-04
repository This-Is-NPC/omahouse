#include "Policy.h"

namespace omahouse {

namespace {

/// The mark a Warn writes when a budget runs out, told apart from a warnAt mark
/// of five minutes by being zero: there are no zero-minute marks, and `warnAt`
/// drops anything that is not above zero.
constexpr int kFinalWarning = 0;

Decision warning(Decision::Reason reason, const QString &budgetId, const QString &scopeUnit,
                 int secondsLeft)
{
    Decision decision;
    decision.kind = Decision::Kind::Warn;
    decision.reason = reason;
    decision.budgetId = budgetId;
    decision.scopeUnit = scopeUnit;
    decision.secondsLeft = secondsLeft;
    return decision;
}

Event stamp(EventKind kind, const QDateTime &now)
{
    Event event;
    event.at = now;
    event.kind = kind;
    return event;
}

bool anyLiveScopeMatches(const QVector<AppScope> &live, const QString &selector)
{
    for (const AppScope &scope : live) {
        if (selectorMatches(selector, scope.id))
            return true;
    }
    return false;
}

} // namespace

Outcome evaluate(const Profile &profile, const QVector<AppScope> &scopes, const Ledger &ledger,
                 const QDateTime &now, int tickSeconds)
{
    Outcome outcome;
    outcome.ledger = ledger;

    // The turn of the date. A ledger from another day is not carried forward and
    // not corrected: the day it belongs to has its own file, this one starts at
    // zero, and a session that was open across midnight simply starts debiting
    // the new file. Warnings and exhaustions go with it, which is what makes a
    // fresh morning a fresh set of warnings.
    const QDate today = now.date();
    if (outcome.ledger.date != today) {
        Ledger fresh;
        fresh.user = outcome.ledger.user.isEmpty() ? profile.user : outcome.ledger.user;
        fresh.date = today;
        outcome.ledger = fresh;
    }
    if (outcome.ledger.user.isEmpty())
        outcome.ledger.user = profile.user;

    // A disabled profile still gets a ledger dated today, so the caller writes
    // the file it expects to write, and gets nothing else: no seconds counted
    // and nothing to do. Off is off.
    if (!profile.enabled)
        return outcome;

    // A scope with no processes is one on its way out, not somebody's app
    // running.
    //
    // A scope whose unit name the parser refused stays: it has no id, but it
    // has processes, and the second is what "somebody is using this machine"
    // means. It is `selectorMatches` that knows the difference -- `*` takes it,
    // a named selector cannot -- so below it is billed by the session, never by
    // a budget about one app, and judged by the profile's default verdict.
    // Naming what could not be identified is still `status`'s job (docs/design.md §5);
    // counting it is this one's.
    QVector<AppScope> live;
    for (const AppScope &scope : scopes) {
        if (scope.isLive())
            live.append(scope);
    }

    // 1. The verdict, per app.
    for (const AppScope &scope : live) {
        if (profile.verdictFor(scope.id) != Verdict::Deny)
            continue;
        // Said once per scope. A refused app that is closed and opened again
        // gets a new unit name from systemd and so is worth saying again, which
        // is the right answer: the user tried again.
        if (!outcome.ledger.hasDenied(scope.unit)) {
            Event event = stamp(EventKind::Denied, now);
            event.scope = scope.unit;
            outcome.ledger.events.append(event);
            outcome.decisions.append(warning(Decision::Reason::Denied, QString(), scope.unit, 0));
        }
        if (!profile.enforce)
            continue;
        Decision close;
        close.kind = Decision::Kind::Close;
        close.reason = Decision::Reason::Denied;
        close.scopeUnit = scope.unit;
        outcome.decisions.append(close);
    }

    // 2. The debit. Once per budget with at least one live app matching its
    //    selector -- never once per process, which is what would make the 21
    //    processes of a Chromium window eat a two hour session in six minutes.
    //    A refused app is debited too: it did run for these seconds, and the
    //    tick that closes it is also the tick that counts it.
    //    A negative tick is a caller with a bug, and it is floored rather than
    //    honoured: handing back time is how a loop that misfires turns into an
    //    afternoon nobody spent.
    const int tick = qMax(0, tickSeconds);
    for (const Budget &budget : profile.budgets) {
        if (budget.id.isEmpty())
            continue;
        if (anyLiveScopeMatches(live, budget.match))
            outcome.ledger.addSeconds(budget.id, tick);
    }

    // 3. The balance, per budget.
    for (const Budget &budget : profile.budgets) {
        // A budget with no limit counts and never runs out, so there is nothing
        // here to decide about it.
        if (budget.id.isEmpty() || !budget.hasLimit())
            continue;

        const int limit = budget.dailyMinutes * 60 + outcome.ledger.grantedSeconds(budget.id);
        const int left = limit - outcome.ledger.secondsFor(budget.id);

        if (left > 0) {
            // Every mark this tick crossed is written down, so a tick long
            // enough to pass two of them does not leave the second one to fire
            // later at a time that would be a lie. Only one warning is sent, for
            // the lowest of them, because two notifications in the same second
            // saying different numbers is worse than one saying the smaller.
            int lowest = -1;
            for (int mark : profile.warnAt) {
                if (mark <= 0 || left > mark * 60)
                    continue;
                if (outcome.ledger.hasWarned(budget.id, mark))
                    continue;
                Event event = stamp(EventKind::Warn, now);
                event.budget = budget.id;
                event.minutes = mark;
                outcome.ledger.events.append(event);
                if (lowest < 0 || mark < lowest)
                    lowest = mark;
            }
            if (lowest >= 0) {
                outcome.decisions.append(
                    warning(Decision::Reason::Warning, budget.id, QString(), left));
            }
            continue;
        }

        // Out of time. When it happened is written down on the first tick that
        // notices, and it is that stamp -- not a countdown living in the
        // daemon's memory -- that the grace window is measured from. A daemon
        // restarted mid-window picks the window back up where it left it.
        QDateTime since = outcome.ledger.exhaustedAt(budget.id);
        if (!since.isValid()) {
            since = now;
            Event event = stamp(EventKind::Exhausted, now);
            event.budget = budget.id;
            outcome.ledger.events.append(event);
        }

        // Without enforcement there is no action to hold back, and a budget
        // whose action is `warn` is its own last word: both cases are the same
        // as a window of nothing, said once, with the counting carrying on.
        const bool acts = profile.enforce && budget.onExhausted != OnExhausted::Warn;
        const int grace = acts ? qMax(0, profile.graceSeconds) : 0;
        const qint64 elapsed = since.secsTo(now);
        const qint64 unspent = grace - elapsed;
        const int remaining =
            static_cast<int>(unspent < 0 ? 0 : qMin<qint64>(unspent, grace));

        // The last word, once, before anything is closed -- docs/design.md §6: nobody
        // is cut off cold. `remaining` is what tells the caller whether to speak
        // now and act later or whether this is the end of it.
        if (!outcome.ledger.hasWarned(budget.id, kFinalWarning)) {
            Event event = stamp(EventKind::Warn, now);
            event.budget = budget.id;
            event.minutes = kFinalWarning;
            outcome.ledger.events.append(event);
            outcome.decisions.append(warning(remaining > 0 ? Decision::Reason::GraceStarted
                                                           : Decision::Reason::Exhausted,
                                             budget.id, QString(), remaining));
        }
        if (!acts || elapsed < grace)
            continue;

        if (budget.onExhausted == OnExhausted::Logout) {
            Decision decision;
            decision.kind = Decision::Kind::Logout;
            decision.reason = Decision::Reason::Exhausted;
            decision.budgetId = budget.id;
            outcome.decisions.append(decision);
            continue;
        }

        // Close names every live scope the budget matches, one decision each,
        // because closing is a write to one scope's cgroup.kill and a budget
        // can be holding several of them.
        for (const AppScope &scope : live) {
            if (!selectorMatches(budget.match, scope.id))
                continue;
            Decision decision;
            decision.kind = Decision::Kind::Close;
            decision.reason = Decision::Reason::Exhausted;
            decision.budgetId = budget.id;
            decision.scopeUnit = scope.unit;
            outcome.decisions.append(decision);
        }
    }

    return outcome;
}

} // namespace omahouse
