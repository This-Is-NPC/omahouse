#pragma once

#include "AppScope.h"
#include "Ledger.h"
#include "Profile.h"

#include <QDateTime>
#include <QStringList>
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
        /// Stop one site opening, named by `site`. `scopeUnit` is empty: a site
        /// is not a cgroup, and this is the whole reason it is its own kind
        /// rather than a `Close` with a domain in it.
        ///
        /// Repeated on every tick the budget is still out, exactly as `Close`
        /// is, and for a better reason: the caller does not remember this. It
        /// works out from today's ledger which sites should not open right now
        /// and makes the browser's policy say exactly that -- so the turn of the
        /// day, a grant, and `enforce --off` each let a site back through
        /// without anything having to remember to undo anything. That is the
        /// same discipline `/etc/omahouse/blocked` keeps in docs/design.md §2,
        /// and it is why this is re-derived rather than stored.
        Block,
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
    /// The registrable domain a `Block` is about, and empty for everything else.
    /// The budget's selector and not its id: a budget called `web` matching `*`
    /// is a limit on browsing at all, and what the browser has to be told is the
    /// `*`.
    QString site;
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
/// as an empty today, because docs/design.md §4 keeps one file per day and the balance
/// resets at the local turn of the date.
///
/// `siteInFront` is the second observation, beside `scopes`: the registrable
/// domain the browser said was in the front tab, **already crossed with
/// presence** by the caller, so an empty string means "bill no site this tick"
/// and covers every way that can be true at once -- no extension, no browser, a
/// stale file, a dark screen, somebody else's session in front. This function
/// does not know what a screen is and must not learn: the browser spike
/// §5 is why the crossing exists, and it is made of a kernel attribute and
/// logind, neither of which belongs on this side of the line.
///
/// It is last, and it has a default, so that the whole of the app half reads
/// exactly as it always did. A caller with nothing to say about the browser is
/// not a caller that has forgotten to answer -- it is a machine with no
/// extension on it, which is most of them.
Outcome evaluate(const Profile &profile, const QVector<AppScope> &scopes, const Ledger &ledger,
                 const QDateTime &now, int tickSeconds,
                 const QString &siteInFront = QString(),
                 const QStringList &alsoFurniture = QStringList());

/// `date` started from the day `previous` belongs to.
///
/// The turn of the date, on its own, because two callers need it and they must
/// not disagree about what survives a night. `evaluate` runs it on a ledger
/// handed to it from another day -- a session open across midnight. The other
/// is whoever reads a day off the disk: the loop builds the path from the date
/// it is asked about, so the first look at a new day meets no file at all, and
/// the turn has to be applied to the last file that does exist. Left to
/// `evaluate` alone a pot went back to full every night, and `resets: never`
/// meant nothing on a machine that had been switched off.
///
/// What survives is what a pot is made of and nothing else: seconds spent,
/// seconds handed over. The profile is here to say which budgets those are, and
/// for no other reason.
Ledger carryInto(const QDate &date, const Ledger &previous, const Profile &profile);

} // namespace omahouse
