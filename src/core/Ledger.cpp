#include "Ledger.h"

#include "Json.h"

#include <QJsonArray>
#include <QJsonValue>

namespace omahouse {

namespace {

/// ISO 8601 with the offset always written, as in docs/design.md §4's
/// `2026-09-03T19:12:04-03:00`. Qt leaves the offset off a local-time value,
/// and a stamp with no offset is a stamp that means a different instant either
/// side of a DST change -- which a day of accounting will eventually straddle.
QString isoWithOffset(const QDateTime &when)
{
    if (!when.isValid())
        return {};
    return when.toOffsetFromUtc(when.offsetFromUtc()).toString(Qt::ISODate);
}

bool wantsDateTime(const QJsonObject &object, const QString &key, const QString &what,
                   QDateTime *value, QString *error)
{
    const QJsonValue found = object.value(key);
    if (!found.isString()) {
        if (error)
            *error = QStringLiteral("%1 has no %2").arg(what, key);
        return false;
    }
    const QDateTime parsed = QDateTime::fromString(found.toString(), Qt::ISODate);
    if (!parsed.isValid()) {
        if (error) {
            *error = QStringLiteral("%1 has an unreadable %2: %3")
                         .arg(what, key, found.toString());
        }
        return false;
    }
    *value = parsed;
    return true;
}

bool wantsString(const QJsonObject &object, const QString &key, const QString &what,
                 QString *value, bool required, QString *error)
{
    const QJsonValue found = object.value(key);
    if (found.isUndefined() || found.isNull()) {
        if (!required)
            return true;
        if (error)
            *error = QStringLiteral("%1 has no %2").arg(what, key);
        return false;
    }
    if (!found.isString()) {
        if (error)
            *error = QStringLiteral("%1 has a non-string %2").arg(what, key);
        return false;
    }
    *value = found.toString();
    return true;
}

} // namespace

QString eventKindName(EventKind kind)
{
    switch (kind) {
    case EventKind::Exhausted:
        return QStringLiteral("exhausted");
    case EventKind::Denied:
        return QStringLiteral("denied");
    case EventKind::Warn:
        break;
    }
    return QStringLiteral("warn");
}

bool eventKindFromName(const QString &name, EventKind *out)
{
    if (name == QStringLiteral("warn")) {
        *out = EventKind::Warn;
        return true;
    }
    if (name == QStringLiteral("exhausted")) {
        *out = EventKind::Exhausted;
        return true;
    }
    if (name == QStringLiteral("denied")) {
        *out = EventKind::Denied;
        return true;
    }
    return false;
}

int Ledger::secondsFor(const QString &budgetId) const
{
    return seconds.value(budgetId, 0);
}

void Ledger::addSeconds(const QString &budgetId, int amount)
{
    if (budgetId.isEmpty() || amount == 0)
        return;
    seconds[budgetId] = secondsFor(budgetId) + amount;
}

int Ledger::grantedSeconds(const QString &budgetId) const
{
    int total = 0;
    for (const Grant &grant : grants) {
        if (grant.budget == budgetId && grant.minutes > 0)
            total += grant.minutes * 60;
    }
    return total;
}

bool Ledger::hasWarned(const QString &budgetId, int minutes) const
{
    for (const Event &event : events) {
        if (event.kind == EventKind::Warn && event.budget == budgetId && event.minutes == minutes)
            return true;
    }
    return false;
}

bool Ledger::hasDenied(const QString &scopeUnit) const
{
    for (const Event &event : events) {
        if (event.kind == EventKind::Denied && event.scope == scopeUnit)
            return true;
    }
    return false;
}

QDateTime Ledger::exhaustedAt(const QString &budgetId) const
{
    for (const Event &event : events) {
        if (event.kind == EventKind::Exhausted && event.budget == budgetId)
            return event.at;
    }
    return {};
}

QJsonObject Ledger::toJson() const
{
    QJsonObject budgetObject;
    for (auto it = seconds.constBegin(); it != seconds.constEnd(); ++it)
        budgetObject.insert(it.key(), it.value());

    QJsonArray grantArray;
    for (const Grant &grant : grants) {
        grantArray.append(QJsonObject{
            {QStringLiteral("at"), isoWithOffset(grant.at)},
            {QStringLiteral("by"), grant.by},
            {QStringLiteral("budget"), grant.budget},
            {QStringLiteral("minutes"), grant.minutes},
        });
    }

    QJsonArray eventArray;
    for (const Event &event : events) {
        QJsonObject object{
            {QStringLiteral("at"), isoWithOffset(event.at)},
            {QStringLiteral("kind"), eventKindName(event.kind)},
        };
        if (!event.budget.isEmpty())
            object.insert(QStringLiteral("budget"), event.budget);
        if (!event.scope.isEmpty())
            object.insert(QStringLiteral("scope"), event.scope);
        if (event.minutes >= 0)
            object.insert(QStringLiteral("minutes"), event.minutes);
        eventArray.append(object);
    }

    return QJsonObject{
        {QStringLiteral("schemaVersion"), kSchemaVersion},
        {QStringLiteral("user"), user},
        {QStringLiteral("date"), date.toString(Qt::ISODate)},
        {QStringLiteral("budgets"), budgetObject},
        {QStringLiteral("grants"), grantArray},
        {QStringLiteral("events"), eventArray},
    };
}

bool Ledger::fromJson(const QJsonObject &object, Ledger *out, QString *error)
{
    const QString what = QStringLiteral("a ledger");
    if (!checkSchemaVersion(object, what, error))
        return false;

    Ledger ledger;
    if (!wantsString(object, QStringLiteral("user"), what, &ledger.user, true, error))
        return false;

    QString dateText;
    if (!wantsString(object, QStringLiteral("date"), what, &dateText, true, error))
        return false;
    ledger.date = QDate::fromString(dateText, Qt::ISODate);
    if (!ledger.date.isValid()) {
        if (error)
            *error = QStringLiteral("a ledger has an unreadable date: %1").arg(dateText);
        return false;
    }

    const QJsonValue budgetsValue = object.value(QStringLiteral("budgets"));
    if (!budgetsValue.isUndefined() && !budgetsValue.isObject()) {
        if (error)
            *error = QStringLiteral("a ledger has a budgets field that is not an object");
        return false;
    }
    const QJsonObject budgetObject = budgetsValue.toObject();
    for (auto it = budgetObject.constBegin(); it != budgetObject.constEnd(); ++it) {
        if (!it.value().isDouble()) {
            if (error) {
                *error = QStringLiteral("the ledger of %1 has a non-numeric count for %2")
                             .arg(ledger.user, it.key());
            }
            return false;
        }
        ledger.seconds.insert(it.key(), it.value().toInt());
    }

    const QJsonValue grantsValue = object.value(QStringLiteral("grants"));
    if (!grantsValue.isUndefined() && !grantsValue.isArray()) {
        if (error)
            *error = QStringLiteral("a ledger has a grants field that is not a list");
        return false;
    }
    for (const QJsonValue &value : grantsValue.toArray()) {
        if (!value.isObject()) {
            if (error)
                *error = QStringLiteral("a ledger has a grant that is not an object");
            return false;
        }
        const QJsonObject entry = value.toObject();
        Grant grant;
        if (!wantsDateTime(entry, QStringLiteral("at"), QStringLiteral("a grant"), &grant.at,
                           error))
            return false;
        if (!wantsString(entry, QStringLiteral("by"), QStringLiteral("a grant"), &grant.by, false,
                         error))
            return false;
        if (!wantsString(entry, QStringLiteral("budget"), QStringLiteral("a grant"), &grant.budget,
                         true, error))
            return false;
        const QJsonValue minutes = entry.value(QStringLiteral("minutes"));
        if (!minutes.isDouble()) {
            if (error)
                *error = QStringLiteral("a grant has a non-numeric minutes");
            return false;
        }
        grant.minutes = minutes.toInt();
        ledger.grants.append(grant);
    }

    const QJsonValue eventsValue = object.value(QStringLiteral("events"));
    if (!eventsValue.isUndefined() && !eventsValue.isArray()) {
        if (error)
            *error = QStringLiteral("a ledger has an events field that is not a list");
        return false;
    }
    for (const QJsonValue &value : eventsValue.toArray()) {
        if (!value.isObject()) {
            if (error)
                *error = QStringLiteral("a ledger has an event that is not an object");
            return false;
        }
        const QJsonObject entry = value.toObject();
        Event event;
        if (!wantsDateTime(entry, QStringLiteral("at"), QStringLiteral("an event"), &event.at,
                           error))
            return false;
        QString kindText;
        if (!wantsString(entry, QStringLiteral("kind"), QStringLiteral("an event"), &kindText, true,
                         error))
            return false;
        // An event kind nobody here knows is not skipped. The kinds are the
        // memory of what has already been said, so reading a file and dropping
        // the parts of it this code does not recognise is how a user gets warned
        // twice, or closed without warning.
        if (!eventKindFromName(kindText, &event.kind)) {
            if (error)
                *error = QStringLiteral("a ledger has an unknown event kind %1").arg(kindText);
            return false;
        }
        if (!wantsString(entry, QStringLiteral("budget"), QStringLiteral("an event"), &event.budget,
                         false, error))
            return false;
        if (!wantsString(entry, QStringLiteral("scope"), QStringLiteral("an event"), &event.scope,
                         false, error))
            return false;
        const QJsonValue minutes = entry.value(QStringLiteral("minutes"));
        if (!minutes.isUndefined()) {
            if (!minutes.isDouble()) {
                if (error)
                    *error = QStringLiteral("an event has a non-numeric minutes");
                return false;
            }
            event.minutes = minutes.toInt();
        }
        ledger.events.append(event);
    }

    *out = ledger;
    return true;
}

bool readLedger(const QString &path, Ledger *out, QString *error, bool *missing)
{
    QJsonObject root;
    bool absent = false;
    if (!readJsonObject(path, &root, error, &absent))
        return false;
    if (missing)
        *missing = absent;
    if (absent) {
        // The first tick of a day meets no file, and that is a day with nothing
        // spent yet rather than a fault. `evaluate` will stamp the date on it.
        *out = {};
        return true;
    }
    return Ledger::fromJson(root, out, error);
}

bool writeLedger(const QString &path, const Ledger &ledger, QString *error)
{
    return writeJsonAtomically(path, ledger.toJson(), error);
}

} // namespace omahouse
