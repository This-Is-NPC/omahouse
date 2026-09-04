#include "Watch.h"

#include "Paths.h"
#include "Users.h"

#include <algorithm>

namespace omahouse {

namespace {

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

/// The id of the scope a decision names, or an empty string.
QString appOfUnit(const QVector<AppScope> &scopes, const QString &unit)
{
    if (unit.isEmpty())
        return {};
    for (const AppScope &scope : scopes) {
        if (scope.unit == unit)
            return scope.id;
    }
    return {};
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

Watch::Watch(const Proc *proc, Notifier *notifier, const Options &options)
    : m_proc(proc)
    , m_notifier(notifier)
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
        uid_t uid = 0;
        watched.account = uidForUser(profile.user, &uid);
        watched.uid = uid;
        if (watched.account && watched.enabled)
            observe(profile, &watched, now);

        watched.worthSaying = worthSaying(watched);
        cycle.users.append(watched);
    }
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
    if (!watched->session)
        return;

    const QVector<AppScope> scopes = m_proc->scopesFor(watched->uid);
    QStringList apps;
    for (const AppScope &scope : scopes) {
        if (scope.isLive())
            apps.append(scope.id);
    }
    watched->apps = sortedWithoutRepeats(apps);

    // The day's own file, spec.md §4. Read by the date of `now` and not by a
    // date the loop is holding on to, so the turn of midnight simply starts
    // reading and writing tomorrow's file.
    const QString path = paths::ledgerFile(profile.user, now.date());
    Ledger before;
    bool missing = false;
    QString error;
    if (!readLedger(path, &before, &error, &missing)) {
        // A ledger that is there and will not parse is not a day to start over:
        // counting from zero on top of a file somebody could still repair is how
        // an afternoon disappears. It is said and skipped, and the other users
        // go on being counted.
        watched->error = error;
        return;
    }
    if (missing) {
        before.user = profile.user;
        before.date = now.date();
    }

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

    for (const Decision &decision : outcome.decisions) {
        if (decision.kind != Decision::Kind::Warn) {
            // plan.md stage 7, and testing.md's box. Recorded and stepped over:
            // in ordinary use it does not even come up, because a profile is
            // born observing and the core emits neither kind without `enforce`.
            watched->notYet.append(decision);
            continue;
        }
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
}

bool Watch::worthSaying(const Watched &watched)
{
    // What the cycle looked like, as one string. Not the seconds and not the
    // time: those change every tick by construction, and a log line every tick
    // is a journal nobody can read. What is worth a line is a change of shape --
    // somebody logged in, an app opened or closed, a budget started or stopped
    // being spent, a file stopped being readable.
    const QStringList parts {
        watched.enabled ? QStringLiteral("on") : QStringLiteral("off"),
        watched.account ? QStringLiteral("account") : QStringLiteral("no account"),
        watched.session ? QStringLiteral("session") : QStringLiteral("no session"),
        watched.apps.join(QLatin1Char(',')),
        watched.debited.join(QLatin1Char(',')),
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
