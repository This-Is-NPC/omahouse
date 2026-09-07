#include "Policy.h"
#include "Allocation.h"

#include "Furniture.h"

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
                 const QDateTime &now, int tickSeconds, const QString &siteInFront,
                 const QStringList &alsoFurniture)
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
        // The session's own furniture is not judged. `default: deny` is about
        // what somebody chose to open, and closing `udiskie` two seconds after
        // login is omahouse breaking the desktop it is a guest on. See
        // `Furniture.h` for why unknown is never furniture.
        if (isFurniture(scope.id, alsoFurniture))
            continue;
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
    //    A site budget is spent by the other observation, and by that one only.
    //    This is the first of the two places a `kind` has to be looked at, and
    //    the reason is `*`: a site budget matching everything would be matched
    //    by `anyLiveScopeMatches` against every scope on the machine and would
    //    spend a day of browsing in an afternoon of anything at all. The two
    //    namespaces are told apart here rather than by the shape of the string,
    //    because there is no shape that tells `org.freedesktop.Platform` from
    //    `youtube.com`.
    const int tick = qMax(0, tickSeconds);

    // Furniture is not evidence that anybody is at the keyboard, so it is held
    // out of the one selector that means "anything at all". A budget that names
    // one of these by id is somebody asking for exactly that number, and it
    // still gets it -- refusing would be this file deciding what an operator is
    // allowed to be curious about.
    QVector<AppScope> billable;
    QVector<AppScope> furniture;
    for (const AppScope &scope : live) {
        if (isFurniture(scope.id, alsoFurniture))
            furniture.append(scope);
        else
            billable.append(scope);
    }

    // The site in front, beside the budgets and never inside them --
    // docs/design.md §5.2. Written whether or not anything has a budget about
    // it, because the observing number is the whole of what §5.2 shipped and it
    // goes on being true for a machine that never grows a site limit.
    if (!siteInFront.isEmpty())
        outcome.ledger.addSiteSeconds(siteInFront, tick);

    for (const Budget &budget : profile.budgets) {
        if (budget.id.isEmpty())
            continue;
        const bool spending = budget.isSite()
            ? (!siteInFront.isEmpty() && selectorMatches(budget.match, siteInFront))
            : anyLiveScopeMatches(billable, budget.match)
                || (budget.match != QStringLiteral("*")
                    && anyLiveScopeMatches(furniture, budget.match));
        if (spending)
            outcome.ledger.addSeconds(budget.id, tick);
    }

    // 3. The balance, per budget.
    for (const Budget &budget : profile.budgets) {
        // A budget with no limit counts and never runs out, so there is nothing
        // here to decide about it.
        if (budget.id.isEmpty() || !budget.hasLimit())
            continue;

        const int limit = allowanceSeconds(profile, budget, outcome.ledger, now.date());
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
                // A mark the budget was never above is not a mark. The marks are
                // absolute -- ten, five, one -- and a budget smaller than one of
                // them is born with it already crossed, so it fires the instant
                // the app opens and says nothing anybody can act on: a three
                // minute budget announced five minutes left, and a ten minute
                // session spent its ten minute mark at login. Strictly above,
                // because a mark equal to the whole budget is the same event as
                // opening the app. What is left when every mark goes this way --
                // a budget of one minute -- still gets the grace warning, which
                // is the one that was always going to matter there.
                if (limit <= mark * 60)
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

        // The second place a `kind` has to be looked at, and the one that would
        // be expensive to get wrong. Below this line the app half reaches into
        // the cgroup tree and into logind; a site budget must never arrive
        // there. It is a branch and not a filter on purpose -- the two halves
        // cannot fall through into each other, so a site budget matching `*`
        // closes nothing and ends nobody's session, which is exactly what a
        // shared loop with an `if` in the middle of it would eventually do.
        if (budget.isSite()) {
            Decision decision;
            decision.kind = Decision::Kind::Block;
            decision.reason = Decision::Reason::Exhausted;
            decision.budgetId = budget.id;
            // The selector, not the id: what the browser has to be told is the
            // domain, and a budget called `web` matching `*` is a limit on
            // browsing rather than on a site called web.
            decision.site = budget.match;
            outcome.decisions.append(decision);
            continue;
        }

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
