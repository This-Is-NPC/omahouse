#include "Watch.h"

#include "Blocked.h"
#include "Paths.h"
#include "Users.h"

#include <algorithm>

namespace omahouse {

namespace {

/// The longest a SIGTERMed scope is given before `cgroup.kill`.
///
/// The window is the profile's `grace`, because that is the number a household
/// set for "how long does something get when its time is up", and asking for a
/// second number would be asking the same question twice. The ceiling is here
/// because `grace` is also the warning window and somebody may reasonably set it
/// to ten minutes: ten minutes of an app that ignored its SIGTERM still running
/// after the ledger says it closed is a report that does not match the screen.
constexpr int kLongestSettle = 60;

QString settleKey(const QString &user, const QString &unit)
{
    return user + QLatin1Char('\n') + unit;
}

const Budget *budgetOf(const Profile &profile, const QString &id)
{
    for (const Budget &budget : profile.budgets) {
        if (budget.id == id)
            return &budget;
    }
    return nullptr;
}

/// How a budget is named in a sentence that starts with it.
///
/// `session` is the budget whose selector is `*` -- spec.md §2 -- and to whoever
/// is being warned it is not a budget at all, it is their evening. Every other
/// id is left exactly as it was written, lower case and all: `chromium` is the
/// word an operator typed and the word `omahouse status` prints, and
/// `org.freedesktop.Platform` with its capital is a different flatpak from
/// `org.freedesktop.platform`. A sentence that begins in lower case is a smaller
/// price than a program renamed to look tidy.
QString budgetPhrase(const QString &id)
{
    if (id == QLatin1String("session"))
        return QStringLiteral("Your session");
    return id;
}

/// What is left, rounded up, because a warning that fires at 299 seconds is the
/// five minute mark and saying `4 minutes left` about it would make the mark
/// somebody configured look wrong.
QString minutesLeft(int seconds)
{
    const int minutes = (seconds + 59) / 60;
    if (minutes == 1)
        return QStringLiteral("1 minute left");
    return QStringLiteral("%1 minutes left").arg(minutes);
}

QString secondsPhrase(int seconds)
{
    if (seconds == 1)
        return QStringLiteral("1 second");
    return QStringLiteral("%1 seconds").arg(seconds);
}

/// Whoever the profile is about, as they would be addressed.
QString whoIsIt(const Profile &profile)
{
    return profile.displayName.isEmpty() ? profile.user : profile.displayName;
}

/// The scope a decision names, or null. Looked up in the very list this cycle
/// read out of `app.slice`, which is the first of the reasons `Close` cannot
/// reach anywhere else: a unit that is not in that list is a unit nothing here
/// has a path for.
const AppScope *scopeOfUnit(const QVector<AppScope> &scopes, const QString &unit)
{
    if (unit.isEmpty())
        return nullptr;
    for (const AppScope &scope : scopes) {
        if (scope.unit == unit)
            return &scope;
    }
    return nullptr;
}

/// The id of the scope a decision names, or an empty string.
QString appOfUnit(const QVector<AppScope> &scopes, const QString &unit)
{
    const AppScope *scope = scopeOfUnit(scopes, unit);
    return scope ? scope->id : QString();
}

QStringList sortedWithoutRepeats(QStringList values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

} // namespace

QString decisionKindName(Decision::Kind kind)
{
    switch (kind) {
    case Decision::Kind::Close:
        return QStringLiteral("close");
    case Decision::Kind::Logout:
        return QStringLiteral("logout");
    case Decision::Kind::Warn:
        break;
    }
    return QStringLiteral("warn");
}

QString doneWhatName(Done::What what)
{
    switch (what) {
    case Done::What::Kill:
        return QStringLiteral("kill");
    case Done::What::Block:
        return QStringLiteral("block");
    case Done::What::Unblock:
        return QStringLiteral("unblock");
    case Done::What::EndSession:
        return QStringLiteral("end-session");
    case Done::What::Terminate:
        break;
    }
    return QStringLiteral("terminate");
}

QString decisionReasonName(Decision::Reason reason)
{
    switch (reason) {
    case Decision::Reason::Denied:
        return QStringLiteral("denied");
    case Decision::Reason::GraceStarted:
        return QStringLiteral("grace");
    case Decision::Reason::Exhausted:
        return QStringLiteral("exhausted");
    case Decision::Reason::Warning:
        break;
    }
    return QStringLiteral("warning");
}

Words wordsFor(const Profile &profile, const Decision &decision, const QString &app,
               const QDateTime &now)
{
    Words words;

    // Whether anything is really going to happen when the time is out. A profile
    // that is only observing, and a budget whose action is `warn`, both end with
    // the warning -- and a notification that promises a program will close when
    // nothing is going to close it teaches whoever reads it to ignore the next
    // one.
    const Budget *budget = budgetOf(profile, decision.budgetId);
    const bool acts = profile.enforce && budget && budget->onExhausted != OnExhausted::Warn;
    const bool logout = acts && budget->onExhausted == OnExhausted::Logout;
    const QString phrase = budgetPhrase(budget ? budget->id : decision.budgetId);

    switch (decision.reason) {
    case Decision::Reason::Denied:
        // The id, because that is the word on the screen an operator configures
        // from and the word in `omahouse status`. The unit only when the scope
        // has no id -- and then it is better than a blank, because a person can
        // at least see which window it is about.
        words.summary = QStringLiteral("%1 is not allowed")
                            .arg(app.isEmpty() ? decision.scopeUnit : app);
        words.body = QStringLiteral("It is not one of the programs released for %1.")
                         .arg(whoIsIt(profile));
        break;

    case Decision::Reason::Warning:
        // The clock time, and not the number of minutes twice: `spec.md` §6
        // writes the example this way -- "Faltam 5 minutos" / "Minecraft fecha
        // às 19:35" -- and it is right, because a time of day is a thing
        // somebody can plan around and "in five minutes" is not.
        words.summary = minutesLeft(decision.secondsLeft);
        words.body = QStringLiteral("%1 %2 at %3.")
                         .arg(phrase,
                              logout ? QStringLiteral("ends")
                                     : (acts ? QStringLiteral("closes")
                                             : QStringLiteral("runs out")),
                              now.addSecs(decision.secondsLeft)
                                  .toString(QStringLiteral("HH:mm")));
        break;

    case Decision::Reason::GraceStarted:
        // Only ever reached where something really is about to happen: the core
        // gives a window of zero to a profile that is only observing, and a
        // window of zero comes back as `Exhausted`.
        words.summary = QStringLiteral("Time is up");
        words.body = logout
            ? QStringLiteral("You will be logged out in %1.")
                  .arg(secondsPhrase(decision.secondsLeft))
            : QStringLiteral("%1 closes in %2.")
                  .arg(phrase, secondsPhrase(decision.secondsLeft));
        break;

    case Decision::Reason::Exhausted:
        words.summary = QStringLiteral("Time is up");
        if (logout)
            words.body = QStringLiteral("The session is ending now.");
        else if (acts)
            words.body = QStringLiteral("%1 is closing now.").arg(phrase);
        else
            words.body = QStringLiteral("%1 is out of time for today. Nothing is being closed.")
                             .arg(phrase);
        break;
    }
    return words;
}

Watch::Watch(const Proc *proc, Notifier *notifier, Enforcer *enforcer, const Options &options)
    : m_proc(proc)
    , m_notifier(notifier)
    , m_enforcer(enforcer)
    , m_options(options)
{
}

Cycle Watch::tick(const QVector<Profile> &profiles, const QDateTime &now)
{
    Cycle cycle;
    cycle.at = now;
    cycle.tickSeconds = m_options.tickSeconds;
    cycle.dryRun = m_options.dryRun;

    for (const Profile &profile : profiles) {
        Watched watched;
        watched.user = profile.user;
        watched.displayName = profile.displayName;
        watched.enabled = profile.enabled;

        // spec.md §5 step 1, and the only two ways there is nothing to do about
        // somebody: no account of that name on this machine, and a profile
        // switched off. Neither is an error. A profile can be written before its
        // account and outlive it, and off is off.
        //
        // Both also mean no logout stands, which is how a name comes out of
        // `blocked` when a profile is switched off or its account is gone.
        uid_t uid = 0;
        watched.account = uidForUser(profile.user, &uid);
        watched.uid = uid;
        if (watched.account && watched.enabled)
            observe(profile, &watched, now);

        cycle.users.append(watched);
    }

    // The file first, then the terminations. spec.md §2 makes them one action,
    // and this is the order that makes them one: a session ended before the name
    // is in `blocked` is a session the tty1 autologin brings back before the next
    // cycle, which is exactly what round 2 measured.
    reconcileBlocked(&cycle);
    endSessions(&cycle);

    for (Watched &watched : cycle.users)
        watched.worthSaying = worthSaying(watched);
    return cycle;
}

void Watch::observe(const Profile &profile, Watched *watched, const QDateTime &now)
{
    // Whether the user's own systemd manager is up, which is the same question
    // spec.md §5 asks as "does /run/user/<uid> exist". This is the stronger half
    // of it: a session with no `app.slice` has no scope to count and none to
    // close, so there is nothing here either way -- and it is asked of the one
    // tree that already moves by variable, which is what lets the suite ask it
    // without a session.
    watched->session = m_proc->hasSession(watched->uid);

    // The day's own file, spec.md §4. Read by the date of `now` and not by a
    // date the loop is holding on to, so the turn of midnight simply starts
    // reading and writing tomorrow's file.
    //
    // Read before the session is asked about, and that is stage 7's doing: a
    // user who has just been logged out has no session, and whether the block of
    // §2 still stands is a question about their day and not about whether they
    // are at the keyboard.
    const QString path = paths::ledgerFile(profile.user, now.date());
    Ledger before;
    bool missing = false;
    QString error;
    if (!readLedger(path, &before, &error, &missing)) {
        // A ledger that is there and will not parse is not a day to start over:
        // counting from zero on top of a file somebody could still repair is how
        // an afternoon disappears. It is said and skipped, and the other users
        // go on being counted.
        //
        // And no logout stands on a day nobody can read. That is `onerr=succeed`
        // again, one layer up: the failure that lets somebody in is recoverable
        // and the one that locks them out is not.
        watched->error = error;
        return;
    }
    if (missing) {
        before.user = profile.user;
        before.date = now.date();
    }

    if (!watched->session) {
        // Nothing open, nothing to count, nothing to write -- and still the one
        // question worth asking. `evaluate` over no scopes with a tick of
        // nothing debits nothing and appends nothing, and answers whether the
        // budget that ended this session is still out of time.
        const Outcome quiet = evaluate(profile, {}, before, now, 0);
        for (const Decision &decision : quiet.decisions) {
            if (decision.kind == Decision::Kind::Logout)
                watched->logouts.append(decision);
        }
        watched->ledger = before;
        return;
    }

    const QVector<AppScope> scopes = m_proc->scopesFor(watched->uid);
    QStringList apps;
    for (const AppScope &scope : scopes) {
        if (!scope.isLive())
            continue;
        if (scope.id.isEmpty())
            ++watched->unnamedScopes;
        else
            apps.append(scope.id);
    }
    watched->apps = sortedWithoutRepeats(apps);

    const Outcome outcome = evaluate(profile, scopes, before, now, m_options.tickSeconds);
    watched->ledger = outcome.ledger;
    for (auto it = outcome.ledger.seconds.cbegin(); it != outcome.ledger.seconds.cend(); ++it) {
        if (it.value() > before.secondsFor(it.key()))
            watched->debited.append(it.key());
    }

    // Written when it changed, and not on every tick: an unchanged file rewritten
    // every two seconds is a disk that never rests to say nothing. It changes on
    // every tick that debits anything, which is every tick of a session with an
    // app open, so this is quiet exactly when there is quiet to keep.
    const bool changed = outcome.ledger.toJson() != before.toJson();
    if (changed && !m_options.dryRun) {
        if (!writeLedger(path, outcome.ledger, &error)) {
            // Nothing is said out loud either, and that is the point of doing
            // the writing first. Every Warn the core hands back is a Warn it has
            // already written into the ledger, so a notification sent over a
            // ledger that did not land is a notification that will be sent again
            // in two seconds, and again, for as long as the disk stays full.
            watched->error = error;
            return;
        }
        watched->wrote = true;
    }

    // The warnings first, and all of them, before anything closes. spec.md §6:
    // nobody is cut off cold, and a notification that arrives after the window
    // it was about is a notification about a window that is already shut.
    for (const Decision &decision : outcome.decisions) {
        if (decision.kind != Decision::Kind::Warn)
            continue;
        Said said;
        said.decision = decision;
        said.app = appOfUnit(scopes, decision.scopeUnit);
        said.words = wordsFor(profile, decision, said.app, now);
        if (!m_options.dryRun && m_notifier) {
            said.sent = m_notifier->notify(watched->uid, said.words.summary, said.words.body,
                                           &said.error);
        }
        watched->said.append(said);
    }

    QStringList stillOpen;
    for (const Decision &decision : outcome.decisions) {
        switch (decision.kind) {
        case Decision::Kind::Warn:
            break;
        case Decision::Kind::Close: {
            // The scope the decision names, out of the very list this cycle read
            // from `app.slice`. A unit that is not in it -- a scope that ended
            // between the read and here -- is nothing to close and nothing to
            // report: it is already gone.
            const AppScope *scope = scopeOfUnit(scopes, decision.scopeUnit);
            if (!scope)
                break;
            stillOpen.append(scope->unit);
            closeScope(profile, watched, decision, *scope, now);
            break;
        }
        case Decision::Kind::Logout:
            // Held until after `blocked` has been written. The two halves of §2
            // are one action, and this is the half that has to go second.
            watched->logouts.append(decision);
            break;
        }
    }

    // What the loop remembers about closing is only ever about scopes that are
    // still there. Without this the map grows by one entry for every app that
    // was ever closed, for as long as the daemon runs -- and a scope whose unit
    // name systemd reused would inherit a window it never had.
    const QStringList keys = m_termed.keys();
    for (const QString &key : keys) {
        if (!key.startsWith(profile.user + QLatin1Char('\n')))
            continue;
        if (!stillOpen.contains(key.section(QLatin1Char('\n'), 1)))
            m_termed.remove(key);
    }
}

void Watch::closeScope(const Profile &profile, Watched *watched, const Decision &decision,
                       const AppScope &scope, const QDateTime &now)
{
    Done done;
    done.decision = decision;
    done.app = scope.id;
    done.unit = scope.unit;

    // The one gate that stands between this and somebody's compositor, asked
    // before anything else and asked of every close alike. Enforce.h has the
    // five questions and why each of them is there.
    const QString refusal =
        whyNotCloseable(m_proc->cgroupRoot(), m_proc->appSlicePath(watched->uid), scope);
    if (!refusal.isEmpty()) {
        done.what = Done::What::Terminate;
        done.error = refusal;
        watched->done.append(done);
        return;
    }
    if (m_options.dryRun || !m_enforcer) {
        done.what = Done::What::Terminate;
        watched->done.append(done);
        return;
    }

    const QString key = settleKey(profile.user, scope.unit);
    const auto termed = m_termed.constFind(key);
    if (termed == m_termed.constEnd()) {
        // The polite half, and the first thing that happens to this scope.
        done.what = Done::What::Terminate;
        int signalled = 0;
        done.carriedOut = m_enforcer->terminate(scope, &signalled, &done.error);
        watched->done.append(done);
        m_termed.insert(key, now);
        // A window of nothing means there is nothing to wait for, so the write
        // follows in the same tick. It is still SIGTERM first: a process that
        // handles it and leaves in that instant leaves on its own terms.
        if (qBound(0, profile.graceSeconds, kLongestSettle) > 0)
            return;
    } else if (termed->secsTo(now) < qBound(0, profile.graceSeconds, kLongestSettle)) {
        // Still inside the window it was given. The scope is alive -- it is in
        // this cycle's list -- and that is the whole of what is happening.
        return;
    }

    // spec.md §5: the whole cgroup in one write, with no reaping order, no
    // orphan and no hunting for pids that forked while the list was being read.
    Done killed;
    killed.what = Done::What::Kill;
    killed.decision = decision;
    killed.app = scope.id;
    killed.unit = scope.unit;
    killed.carriedOut = m_enforcer->killTree(scope, &killed.error);
    watched->done.append(killed);
}

void Watch::reconcileBlocked(Cycle *cycle)
{
    // The content of the file is the answer, never a change to it. Every cycle
    // works out the whole set of accounts that should be refused right now and
    // writes exactly that -- which is what takes a name back out when the day
    // turns, when an operator grants ten minutes, when enforcement goes off, and
    // when a profile is removed and its user is not in this list at all.
    QStringList wanted;
    for (const Watched &watched : cycle->users) {
        if (!watched.logouts.isEmpty())
            wanted.append(watched.user);
    }
    wanted = sortedWithoutRepeats(wanted);

    const QString path = paths::blockedFile();
    QString error;
    QStringList have = sortedWithoutRepeats(readBlocked(path, &error));
    if (!error.isEmpty()) {
        // A `blocked` that cannot be read is a `blocked` refusing nobody --
        // `onerr=succeed`, spec.md §2 -- so this is worth saying and is not
        // worth writing over: the file may be somebody's to repair.
        cycle->blockedError = error;
        cycle->blocked = have;
        return;
    }
    cycle->blocked = have;

    if (have == wanted)
        return;
    if (m_options.dryRun) {
        // A dry run says who would have been shut out and shuts nobody out.
        for (Watched &watched : cycle->users) {
            const bool want = !watched.logouts.isEmpty();
            if (want == have.contains(watched.user))
                continue;
            Done done;
            done.what = want ? Done::What::Block : Done::What::Unblock;
            if (want)
                done.decision = watched.logouts.first();
            watched.done.append(done);
        }
        return;
    }

    if (!writeBlocked(path, wanted, &error)) {
        cycle->blockedError = error;
        return;
    }
    cycle->blocked = wanted;

    for (Watched &watched : cycle->users) {
        const bool want = !watched.logouts.isEmpty();
        if (want == have.contains(watched.user))
            continue;
        Done done;
        done.what = want ? Done::What::Block : Done::What::Unblock;
        done.carriedOut = true;
        if (want)
            done.decision = watched.logouts.first();
        watched.done.append(done);
    }
}

void Watch::endSessions(Cycle *cycle)
{
    for (Watched &watched : cycle->users) {
        if (watched.logouts.isEmpty())
            continue;
        watched.blocked = cycle->blocked.contains(watched.user);
        // Already out, and asked before anything else: there is no session to
        // end, so there is nothing here to do and nothing to refuse. The name
        // stays in the file, which is the half doing the work now -- it is what
        // turns `terminate-user` from an interruption into a logout.
        if (!watched.session)
            continue;

        Done done;
        done.what = Done::What::EndSession;
        done.decision = watched.logouts.first();

        // Never without the block. poc/findings.md round 2: `terminate-user` on
        // a machine with a tty1 autologin put the session back up in the same
        // breath, so a termination with nothing behind it is not a weaker
        // version of logging somebody out -- it is a session killed for nothing.
        const QString refusal = whyNotBlockable(paths::configDir());
        if (!refusal.isEmpty())
            done.error = refusal;
        else if (!watched.blocked)
            done.error = QStringLiteral("%1 is not in %2, so ending the session would only "
                                        "hand it back at the next login")
                             .arg(watched.user, paths::blockedFile());
        else if (m_options.dryRun || !m_enforcer)
            done.error.clear();
        else
            done.carriedOut = m_enforcer->endSessions(watched.user, &done.error);

        watched.done.append(done);
    }
}

bool Watch::worthSaying(const Watched &watched)
{
    // What the cycle looked like, as one string. Not the seconds and not the
    // time: those change every tick by construction, and a log line every tick
    // is a journal nobody can read. What is worth a line is a change of shape --
    // somebody logged in, an app opened or closed, a budget started or stopped
    // being spent, a file stopped being readable.
    QStringList acts;
    for (const Done &done : watched.done)
        acts.append(doneWhatName(done.what) + QLatin1Char(':') + done.unit);
    const QStringList parts {
        watched.enabled ? QStringLiteral("on") : QStringLiteral("off"),
        watched.account ? QStringLiteral("account") : QStringLiteral("no account"),
        watched.session ? QStringLiteral("session") : QStringLiteral("no session"),
        watched.apps.join(QLatin1Char(',')),
        QString::number(watched.unnamedScopes),
        watched.debited.join(QLatin1Char(',')),
        // What the teeth did, so that a close and a logout always get a line --
        // they are the two things this program does that take something away
        // from somebody, and neither may ever happen quietly.
        watched.blocked ? QStringLiteral("blocked") : QString(),
        acts.join(QLatin1Char(',')),
        watched.error,
    };
    const QString shape = parts.join(QLatin1Char('|'));

    // The first cycle always says something: a daemon that starts and prints
    // nothing is a daemon nobody can tell from one that failed to start.
    const auto previous = m_shape.constFind(watched.user);
    const bool changed = previous == m_shape.constEnd() || *previous != shape;
    m_shape.insert(watched.user, shape);
    return changed;
}

} // namespace omahouse
