#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

namespace omahouse {

// One day of one user, docs/design.md §4: seconds spent per budget, the time an
// operator handed over, and what the daemon already said out loud.
//
// The events are not a log kept for the report's sake. They are where the
// once-only decisions remember that they have fired: a tick is two seconds long
// and the ledger is the only thing that survives from one to the next, so
// "warn at five minutes left" without a written mark is a warning every two
// seconds for five minutes.

enum class EventKind {
    /// A warnAt mark, or -- with `minutes` at zero -- the last word before the
    /// action lands.
    Warn,
    /// A budget crossed its limit. `at` is when, and so is where the grace
    /// window is measured from.
    Exhausted,
    /// An app was refused. Named by scope unit rather than by budget.
    Denied,
};

QString eventKindName(EventKind kind);
bool eventKindFromName(const QString &name, EventKind *out);

struct Grant {
    QDateTime at;
    QString by;
    QString budget;
    int minutes = 0;
    /// Local balance adjustment, excluded from household credit.
    bool adjustment = false;
};

struct Event {
    QDateTime at;
    EventKind kind = EventKind::Warn;
    QString budget;
    /// The scope unit, for Denied. Empty otherwise.
    QString scope;
    /// The warnAt mark, for Warn: 5 is "five minutes left", 0 is the warning
    /// that comes with running out. Negative is absent.
    int minutes = -1;
};

struct Ledger {
    QString user;
    QDate date;
    /// Seconds spent, by budget id. A QMap and not a QHash: the file is written
    /// every couple of seconds and read by people, and a hash would reorder the
    /// keys between two writes that mean the same thing.
    QMap<QString, int> seconds;
    /// Seconds of the day spent in each state of presence, by the reason's name.
    ///
    /// Beside the budgets and never inside them. What omahouse bills an app is
    /// its running time -- docs/design.md §5, decided and published -- and this
    /// does not touch it: a day with two hours of `screen-off` in it has the same
    /// `seconds` it would have had if nobody had ever measured presence. It is
    /// here to be read, by `status` and by whatever counts time per site later,
    /// and the names are `src/sys`'s: this side of the line does not know what a
    /// screen is.
    ///
    /// A QMap for the same reason `seconds` is one: the file is written every
    /// couple of seconds and read by people, and a hash would shuffle the keys
    /// between two writes that mean the same thing.
    QMap<QString, int> presence;
    /// Seconds of the day the front tab of the focused browser window was on
    /// each site, by registrable domain -- docs/design.md §5.2.
    ///
    /// Beside the budgets, exactly as `presence` is, and for a stronger reason
    /// than presence has: there is no budget here at all. Nothing runs out,
    /// nothing warns and nothing closes. This is the observing stage, and the
    /// number exists to be looked at before anybody decides whether it is worth
    /// giving teeth to.
    ///
    /// A second only lands here if two things were true at once: the browser
    /// said a site was in front, and omahouse's own presence said somebody was
    /// in front of the screen. The browser spike is why the second
    /// half is not optional -- a browser reports `active` with the monitor
    /// physically off, so a meter that trusted it would run all night beside a
    /// sleeping child.
    ///
    /// A QMap for the same reason `seconds` is one: the file is written every
    /// couple of seconds and read by people.
    QMap<QString, int> sites;
    /// Seconds spent against budgets that do not reset, by budget id.
    ///
    /// **Beside `seconds` and never inside it**, and this second map is the
    /// load-bearing part of a counter that outlives the day. Everything that
    /// adds days or machines together -- `report`, `omahouse house`,
    /// `consolidate`, `collect` -- reads `seconds`. A counter that survives the
    /// turn of the date is carried into the file of every day it touches, so in
    /// `seconds` it would be counted once per day it crossed, and the report of
    /// a fortnight would show a pot spent fourteen times. Kept apart, every one
    /// of those readers goes on being right without knowing this exists.
    ///
    /// Nothing here ever goes back to zero on its own. A pot is emptied by
    /// being spent and refilled by somebody handing over more, which is the
    /// whole of what `Resets::Never` means.
    QMap<QString, int> keptSeconds;

    /// Seconds handed over to budgets that do not reset, by budget id.
    ///
    /// The other half of a pot, and it has to walk into the new day for the
    /// same reason the spending does. A grant lives in the file of the day it
    /// was made, which is right for `report` and for the household -- both add
    /// days together and would count a carried grant once per day it crossed.
    /// So the turn of the date folds the outgoing day's grants for a pot into
    /// this running total, exactly once, and reads them from here afterwards.
    ///
    /// Without it the night took the operator's decision away and left the
    /// spending it had paid for: a pot refilled at eleven came into the morning
    /// with yesterday's hour against today's smaller allowance, which is worse
    /// than losing both halves. `Resets::Never` says a pot is emptied by being
    /// spent and refilled by somebody handing over more -- this is where the
    /// second clause is kept.
    QMap<QString, int> keptGranted;

    QJsonObject allocation;
    QDateTime observedAt;
    QVector<Grant> grants;
    QVector<Event> events;

    int secondsFor(const QString &budgetId) const;
    void addSeconds(const QString &budgetId, int amount);

    /// The same two, for a budget whose counter survives the turn of the date.
    /// Which pair a budget uses is `Budget::carriesOver()`, decided by the
    /// profile and never by the shape of anything here.
    int keptSecondsFor(const QString &budgetId) const;
    void addKeptSeconds(const QString &budgetId, int amount);

    /// The same two again, for what has been handed over to a pot.
    int keptGrantedFor(const QString &budgetId) const;
    void addKeptGranted(const QString &budgetId, int amount);

    int presenceSecondsFor(const QString &reason) const;
    void addPresenceSeconds(const QString &reason, int amount);

    int siteSecondsFor(const QString &site) const;
    void addSiteSeconds(const QString &site, int amount);

    /// Minutes an operator added today, as seconds, for one budget. Grants are
    /// in the day's own file, so they expire by the file expiring.
    int grantedSeconds(const QString &budgetId) const;
    int creditedSeconds(const QString &budgetId) const;

    bool hasWarned(const QString &budgetId, int minutes) const;
    bool hasDenied(const QString &scopeUnit) const;
    /// When the budget first ran out today, or an invalid QDateTime.
    QDateTime exhaustedAt(const QString &budgetId) const;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Ledger *out, QString *error);
};

bool readLedger(const QString &path, Ledger *out, QString *error, bool *missing = nullptr);
bool writeLedger(const QString &path, const Ledger &ledger, QString *error);

} // namespace omahouse
