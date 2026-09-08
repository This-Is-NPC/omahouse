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

int Ledger::keptSecondsFor(const QString &budgetId) const
{
    return keptSeconds.value(budgetId, 0);
}

void Ledger::addKeptSeconds(const QString &budgetId, int amount)
{
    if (budgetId.isEmpty() || amount == 0)
        return;
    keptSeconds[budgetId] = keptSecondsFor(budgetId) + amount;
}

int Ledger::keptGrantedFor(const QString &budgetId) const
{
    return keptGranted.value(budgetId, 0);
}

void Ledger::addKeptGranted(const QString &budgetId, int amount)
{
    if (budgetId.isEmpty() || amount == 0)
        return;
    keptGranted[budgetId] = keptGrantedFor(budgetId) + amount;
}

int Ledger::presenceSecondsFor(const QString &reason) const
{
    return presence.value(reason, 0);
}

void Ledger::addPresenceSeconds(const QString &reason, int amount)
{
    if (reason.isEmpty() || amount == 0)
        return;
    presence[reason] = presenceSecondsFor(reason) + amount;
}

int Ledger::siteSecondsFor(const QString &site) const
{
    return sites.value(site, 0);
}

void Ledger::addSiteSeconds(const QString &site, int amount)
{
    if (site.isEmpty() || amount == 0)
        return;
    sites[site] = siteSecondsFor(site) + amount;
}

int Ledger::grantedSeconds(const QString &budgetId) const
{
    int total = 0;
    for (const Grant &grant : grants) {
        if (grant.budget == budgetId && grant.minutes != 0)
            total += grant.minutes * 60;
    }
    return total;
}

int Ledger::creditedSeconds(const QString &budgetId) const
{
    int total = 0;
    for (const Grant &grant : grants) {
        if (grant.budget == budgetId && !grant.adjustment)
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
        QJsonObject entry{
            {QStringLiteral("at"), isoWithOffset(grant.at)},
            {QStringLiteral("by"), grant.by},
            {QStringLiteral("budget"), grant.budget},
            {QStringLiteral("minutes"), grant.minutes},
        };
        if (grant.adjustment)
            entry.insert(QStringLiteral("kind"), QStringLiteral("adjustment"));
        grantArray.append(entry);
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

    QJsonObject document{
        {QStringLiteral("schemaVersion"), kLedgerSchema},
        {QStringLiteral("user"), user},
        {QStringLiteral("date"), date.toString(Qt::ISODate)},
        {QStringLiteral("budgets"), budgetObject},
        {QStringLiteral("grants"), grantArray},
        {QStringLiteral("events"), eventArray},
    };

    // The rule for the three optional objects below, said once here.
    //
    // Each is written only when it has something to say. Not to keep older
    // ledgers readable -- an empty object would be read fine -- but because
    // this file is rewritten every couple of seconds for as long as the machine
    // is on. `"presence": {}, "sites": {}, "kept": {}` on a machine that has
    // none of the three is three lies about what was measured, on every tick,
    // forever.
    //
    // It is worth being precise about what this rule is *not*, because a
    // neighbouring one was removed for being the other thing: writing a field
    // only when it means something is design. Reading two spellings of one
    // value, as a budget's `match` did, was compatibility wearing design's
    // clothes, and it went the moment somebody asked which files it was for.
    if (!presence.isEmpty()) {
        QJsonObject presenceObject;
        for (auto it = presence.constBegin(); it != presence.constEnd(); ++it)
            presenceObject.insert(it.key(), it.value());
        document.insert(QStringLiteral("presence"), presenceObject);
    }

    // The rule above, for a machine with no browser extension on it.
    if (!sites.isEmpty()) {
        QJsonObject siteObject;
        for (auto it = sites.constBegin(); it != sites.constEnd(); ++it)
            siteObject.insert(it.key(), it.value());
        document.insert(QStringLiteral("sites"), siteObject);
    }
    // The rule above, for profiles with no budget that outlives the day.
    if (!keptSeconds.isEmpty()) {
        QJsonObject kept;
        for (auto it = keptSeconds.constBegin(); it != keptSeconds.constEnd(); ++it)
            kept.insert(it.key(), it.value());
        document.insert(QStringLiteral("kept"), kept);
    }
    // And again for what was handed over to those budgets, which is written
    // apart from `kept` because the two are a decision and an observation and
    // one of them is allowed to be larger than the other.
    if (!keptGranted.isEmpty()) {
        QJsonObject granted;
        for (auto it = keptGranted.constBegin(); it != keptGranted.constEnd(); ++it)
            granted.insert(it.key(), it.value());
        document.insert(QStringLiteral("keptGranted"), granted);
    }
    if (!allocation.isEmpty()) document.insert(QStringLiteral("allocation"), allocation);
    if (observedAt.isValid()) document.insert(QStringLiteral("observedAt"), isoWithOffset(observedAt));
    return document;
}

bool Ledger::fromJson(const QJsonObject &object, Ledger *out, QString *error)
{
    const QString what = QStringLiteral("a ledger");
    if (!checkSchemaVersion(object, kLedgerSchema, what, error))
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

    if (object.contains(QStringLiteral("allocation"))) {
        if (!object.value(QStringLiteral("allocation")).isObject()) {
            if (error) *error = QStringLiteral("allocation must be an object");
            return false;
        }
        ledger.allocation = object.value(QStringLiteral("allocation")).toObject();
    }
    if (object.contains(QStringLiteral("observedAt"))
            && !wantsDateTime(object, QStringLiteral("observedAt"), what, &ledger.observedAt, error))
        return false;

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

    // Absent in every ledger written before presence was measured, which is
    // every ledger already on the machines this ships to. Absent is empty and
    // never an error.
    const QJsonValue presenceValue = object.value(QStringLiteral("presence"));
    if (!presenceValue.isUndefined() && !presenceValue.isObject()) {
        if (error)
            *error = QStringLiteral("a ledger has a presence field that is not an object");
        return false;
    }
    const QJsonObject presenceObject = presenceValue.toObject();
    for (auto it = presenceObject.constBegin(); it != presenceObject.constEnd(); ++it) {
        if (!it.value().isDouble()) {
            if (error) {
                *error = QStringLiteral("the ledger of %1 has a non-numeric presence count "
                                        "for %2")
                             .arg(ledger.user, it.key());
            }
            return false;
        }
        ledger.presence.insert(it.key(), it.value().toInt());
    }

    // Absent in every ledger written before the browser was measured, and
    // absent for good on a machine with no extension. Absent is empty and never
    // an error.
    const QJsonValue sitesValue = object.value(QStringLiteral("sites"));
    if (!sitesValue.isUndefined() && !sitesValue.isObject()) {
        if (error)
            *error = QStringLiteral("a ledger has a sites field that is not an object");
        return false;
    }
    const QJsonObject siteObject = sitesValue.toObject();
    for (auto it = siteObject.constBegin(); it != siteObject.constEnd(); ++it) {
        if (!it.value().isDouble()) {
            if (error) {
                *error = QStringLiteral("the ledger of %1 has a non-numeric site count "
                                        "for %2")
                             .arg(ledger.user, it.key());
            }
            return false;
        }
        ledger.sites.insert(it.key(), it.value().toInt());
    }

    // Absent in every ledger written before a budget could outlive the day,
    // which is every ledger on the machines this ships to.
    const QJsonValue keptValue = object.value(QStringLiteral("kept"));
    if (!keptValue.isUndefined() && !keptValue.isObject()) {
        if (error)
            *error = QStringLiteral("a ledger has a kept field that is not an object");
        return false;
    }
    const QJsonObject kept = keptValue.toObject();
    for (auto it = kept.constBegin(); it != kept.constEnd(); ++it) {
        if (!it.value().isDouble()) {
            if (error) {
                *error = QStringLiteral("the ledger of %1 has a non-numeric kept count for %2")
                             .arg(ledger.user, it.key());
            }
            return false;
        }
        ledger.keptSeconds.insert(it.key(), it.value().toInt());
    }

    const QJsonValue grantedValue = object.value(QStringLiteral("keptGranted"));
    if (!grantedValue.isUndefined() && !grantedValue.isObject()) {
        if (error)
            *error = QStringLiteral("a ledger has a keptGranted field that is not an object");
        return false;
    }
    const QJsonObject granted = grantedValue.toObject();
    for (auto it = granted.constBegin(); it != granted.constEnd(); ++it) {
        if (!it.value().isDouble()) {
            if (error) {
                *error = QStringLiteral("the ledger of %1 has a non-numeric kept grant for %2")
                             .arg(ledger.user, it.key());
            }
            return false;
        }
        ledger.keptGranted.insert(it.key(), it.value().toInt());
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
        if (entry.contains(QStringLiteral("kind"))) {
            if (entry.value(QStringLiteral("kind")) != QJsonValue(QStringLiteral("adjustment"))) {
                if (error) *error = QStringLiteral("a grant has an unknown kind");
                return false;
            }
            grant.adjustment = true;
        }
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
