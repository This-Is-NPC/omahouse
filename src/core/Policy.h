#pragma once

#include "AppScope.h"
#include "Ledger.h"
#include "Profile.h"

#include <QDateTime>
#include <QVector>

namespace omahouse {

// One thing to say to the caller about one budget or one app.
//
// `kind` is what to do and `reason` is why, and both are needed: a Warn about a
// program that is not on the list and a Warn about five minutes left are the
// same instruction with two different sentences to put in the notification, and
// the core is the wrong place to write Portuguese.
struct Decision {
    enum class Kind {
        /// Say something. Never repeated: every Warn this returns is a Warn the
        /// ledger has recorded, so a caller can send it without asking whether
        /// it already did.
        Warn,
        /// Close one app scope, named by `scopeUnit`. Repeated on every tick
        /// while the scope is still alive, because SIGTERM, the grace, and
        /// cgroup.kill are a sequence the caller runs and may have to run again.
        Close,
        /// End the session. `scopeUnit` is empty: this one is about the user.
        Logout,
    };

    enum class Reason {
        /// The verdict said no. `budgetId` is empty; nothing ran out.
        Denied,
        /// A warnAt mark was crossed. `secondsLeft` is what is really left,
        /// which is at or just under the mark.
        Warning,
        /// The budget just ran out and the grace window has started.
        /// `secondsLeft` is how much of the window is left, so the caller can
        /// say "closes in twenty seconds" and know it is not acting yet.
        GraceStarted,
        /// The budget is out and the window is over.
        Exhausted,
    };

    Kind kind = Kind::Warn;
    Reason reason = Reason::Warning;
    QString budgetId;
    QString scopeUnit;
    int secondsLeft = 0;
};

struct Outcome {
    Ledger ledger;
    QVector<Decision> decisions;
};

/// One tick of the whole model: what the day now looks like, and what to do
/// about it.
///
/// Everything comes in by parameter, `now` included. That is the whole point of
/// the stage: a two hour budget is proved in microseconds by handing this
/// function a QDateTime two hours later, and no test has to touch the clock of
/// the machine it runs on. Nothing in here reads the machine, the environment or
/// the time -- `scopes` is what Proc saw and `now` is when it saw it.
///
/// The returned ledger is always dated `now`: a ledger from yesterday comes back
/// as an empty today, because spec.md §4 keeps one file per day and the balance
/// resets at the local turn of the date.
Outcome evaluate(const Profile &profile, const QVector<AppScope> &scopes, const Ledger &ledger,
                 const QDateTime &now, int tickSeconds);

} // namespace omahouse
