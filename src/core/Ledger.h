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
    QVector<Grant> grants;
    QVector<Event> events;

    int secondsFor(const QString &budgetId) const;
    void addSeconds(const QString &budgetId, int amount);

    /// Minutes an operator added today, as seconds, for one budget. Grants are
    /// in the day's own file, so they expire by the file expiring.
    int grantedSeconds(const QString &budgetId) const;

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
