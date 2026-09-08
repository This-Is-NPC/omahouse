#include "Allocation.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>

namespace omahouse {
namespace {
bool refuse(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
bool whole(const QJsonValue &v, int *out)
{
    const double n = v.toDouble(-1);
    if (!v.isDouble() || !std::isfinite(n) || n < 0 || n > 31536000 || std::floor(n) != n)
        return false;
    *out = static_cast<int>(n);
    return true;
}
}

bool validAllocation(const QJsonObject &a, QString *error)
{
    for (const QString &key : {QStringLiteral("authority"), QStringLiteral("machine")}) {
        if (!a.value(key).isString() || a.value(key).toString().isEmpty())
            return refuse(error, QStringLiteral("allocation needs %1").arg(key));
    }
    if (!a.contains(QStringLiteral("date")))
        return a.size() == 2 || refuse(error, QStringLiteral("an enrollment has only authority and machine"));
    int revision;
    if (!a.value(QStringLiteral("date")).isString()
            || !QDate::fromString(a.value(QStringLiteral("date")).toString(), Qt::ISODate).isValid()
            || !whole(a.value(QStringLiteral("revision")), &revision) || revision < 1
            || !a.value(QStringLiteral("limits")).isObject())
        return refuse(error, QStringLiteral("allocation needs date, positive revision and limits"));
    const auto limits = a.value(QStringLiteral("limits")).toObject();
    if (limits.isEmpty()) return refuse(error, QStringLiteral("allocation has no budgets"));
    for (auto it = limits.begin(); it != limits.end(); ++it) {
        int seconds;
        if (it.key().isEmpty() || !whole(it.value(), &seconds))
            return refuse(error, QStringLiteral("allocation limits must be nonnegative whole seconds"));
    }
    return true;
}

int allowanceSeconds(const Profile &profile, const Budget &budget,
                     const Ledger &ledger, const QDate &date)
{
    if (profile.allocation.isEmpty()) {
        // A session-anchored budget counts only what was handed over in the
        // sitting it is in. The grants live in the day's file and the day
        // outlasts the sitting, so counting all of them would give the next
        // person to log in the ten minutes somebody else was given.
        const int granted = budget.perSession()
            ? ledger.grantedSeconds(budget.id, ledger.sessionAnchor)
            : ledger.grantedSeconds(budget.id);
        return budget.dailyMinutes * 60 + granted;
    }
    if (profile.allocation.value(QStringLiteral("date")).toString() != date.toString(Qt::ISODate))
        return 0;
    return profile.allocation.value(QStringLiteral("limits")).toObject().value(budget.id).toInt(0);
}

bool applyAllocation(Profile *profile, const QJsonObject &d, const QDate &today, QString *error)
{
    if (profile->allocation.isEmpty())
        return refuse(error, QStringLiteral("profile must enroll before receiving an allocation"));
    if (!validAllocation(d, error)) return false;
    if (d.value(QStringLiteral("user")).toString() != profile->user
            || d.value(QStringLiteral("date")).toString() != today.toString(Qt::ISODate))
        return refuse(error, QStringLiteral("allocation names another user or day"));
    for (const QString &key : {QStringLiteral("authority"), QStringLiteral("machine")}) {
        if (d.value(key) != profile->allocation.value(key))
            return refuse(error, QStringLiteral("allocation names another %1").arg(key));
    }
    const auto limits = d.value(QStringLiteral("limits")).toObject();
    QSet<QString> expected;
    for (const auto &budget : profile->budgets) if (budget.hasLimit()) expected.insert(budget.id);
    const auto keys = limits.keys();
    if (QSet<QString>(keys.begin(), keys.end()) != expected)
        return refuse(error, QStringLiteral("allocation budgets differ from the profile"));
    if (profile->allocation.value(QStringLiteral("date")) == d.value(QStringLiteral("date"))) {
        const int oldRevision = profile->allocation.value(QStringLiteral("revision")).toInt();
        const int revision = d.value(QStringLiteral("revision")).toInt();
        if (revision < oldRevision || (revision == oldRevision && profile->allocation != d))
            return refuse(error, QStringLiteral("stale or conflicting allocation revision"));
        const auto old = profile->allocation.value(QStringLiteral("limits")).toObject();
        for (const QString &key : keys) {
            if (limits.value(key).toInt() < old.value(key).toInt())
                return refuse(error, QStringLiteral("issued portions cannot be reclaimed during the day"));
        }
    }
    profile->allocation = d;
    return true;
}

bool planAllocations(const Profile &profile, const QVector<QPair<QString, Ledger>> &days,
                     const QJsonObject &previous, const QDateTime &now,
                     QJsonObject *plan, QString *error)
{
    const QString authority = profile.allocation.value(QStringLiteral("authority")).toString();
    const QString date = now.date().toString(Qt::ISODate);
    if (authority.isEmpty() || profile.allocation.value(QStringLiteral("machine")) != QJsonValue("here")
            || days.isEmpty())
        return refuse(error, QStringLiteral("enroll the manager as here before planning"));
    QJsonArray members;
    QSet<QString> seen;
    for (const auto &day : days) {
        if (seen.contains(day.first)) return refuse(error, QStringLiteral("duplicate allocation member"));
        seen.insert(day.first);
        members.append(day.first);
        if (day.second.date != now.date() || day.second.user != profile.user
                || day.second.allocation.value(QStringLiteral("authority")).toString() != authority
                || day.second.allocation.value(QStringLiteral("machine")).toString() != day.first
                || !day.second.observedAt.isValid()
                || day.second.observedAt.secsTo(now) < -5 || day.second.observedAt.secsTo(now) > 120)
            return refuse(error, QStringLiteral("%1 needs a fresh enrolled day (at most 120s old)").arg(day.first));
        for (int seconds : day.second.seconds) {
            if (seconds < 0 || seconds > 31536000)
                return refuse(error, QStringLiteral("invalid consumption counter"));
        }
    }
    QJsonObject oldDocuments;
    int revision = 1;
    if (!previous.isEmpty()) {
        if (previous.value(QStringLiteral("authority")).toString() != authority
                || previous.value(QStringLiteral("user")).toString() != profile.user
                || previous.value(QStringLiteral("date")).toString() != date
                || previous.value(QStringLiteral("members")).toArray() != members
                || !previous.value(QStringLiteral("documents")).isObject())
            return refuse(error, QStringLiteral("allocation membership/authority is frozen for this day"));
        oldDocuments = previous.value(QStringLiteral("documents")).toObject();
        int oldRevision;
        if (previous.value("schemaVersion") != QJsonValue(1)
                || !whole(previous.value("revision"), &oldRevision) || oldRevision < 1
                || oldRevision >= 31536000 || oldDocuments.size() != days.size())
            return refuse(error, QStringLiteral("invalid reservation state; restore the manager state"));
        revision = oldRevision + 1;
        for (const auto &day : days) {
            const auto old = oldDocuments.value(day.first).toObject();
            if (!validAllocation(old, error) || old.value("authority").toString() != authority
                    || old.value("machine").toString() != day.first
                    || old.value("user").toString() != profile.user
                    || old.value("date").toString() != date
                    || old.value("revision").toInt() != oldRevision)
                return refuse(error, QStringLiteral("inconsistent issued document; restore the manager state"));
            const auto received = day.second.allocation;
            if (received.value("date").toString() == date) {
                if (received.value("revision").toInt() > oldRevision)
                    return refuse(error, QStringLiteral("machine has a newer reservation; restore the manager state"));
                const auto caps = received.value("limits").toObject();
                for (auto it = caps.begin(); it != caps.end(); ++it) {
                    if (it.value().toInt() > old.value("limits").toObject().value(it.key()).toInt(-1))
                        return refuse(error, QStringLiteral("issued credit was rolled back; restore the manager state"));
                }
            }
        }
    }
    QJsonObject documents;
    for (const auto &day : days) {
        documents.insert(day.first, QJsonObject{{"authority", authority}, {"machine", day.first},
                         {"user", profile.user}, {"date", date}, {"revision", revision},
                         {"limits", QJsonObject{}}});
    }
    bool any = false;
    for (const auto &budget : profile.budgets) {
        if (!budget.hasLimit()) continue;
        any = true;
        qint64 credit = qint64(budget.dailyMinutes) * 60;
        for (const auto &day : days) {
            for (const auto &grant : day.second.grants)
                if (grant.budget == budget.id && !grant.adjustment)
                    credit += qint64(grant.minutes) * 60;
        }
        if (credit < 0 || credit > 31536000)
            return refuse(error, QStringLiteral("household credit is outside the supported range"));
        QVector<int> portions;
        qint64 reserved = 0;
        for (const auto &day : days) {
            int portion = day.second.secondsFor(budget.id);
            if (!previous.isEmpty()) {
                const auto old = oldDocuments.value(day.first).toObject().value("limits").toObject();
                if (!old.contains(budget.id) || !whole(old.value(budget.id), &portion))
                    return refuse(error, QStringLiteral("issued plan is incomplete; do not recreate it"));
            }
            portions.append(portion);
            reserved += portion;
        }
        if (reserved > credit)
            return refuse(error, QStringLiteral("%1 has less credit than already spent/reserved; no reallocation").arg(budget.id));
        const int extra = static_cast<int>(credit - reserved);
        for (int i = 0; i < days.size(); ++i) {
            auto doc = documents.value(days.at(i).first).toObject();
            auto limits = doc.value("limits").toObject();
            limits.insert(budget.id, portions.at(i) + extra / days.size() + (i < extra % days.size() ? 1 : 0));
            doc.insert("limits", limits);
            documents.insert(days.at(i).first, doc);
        }
    }
    if (!any) return refuse(error, QStringLiteral("profile has no limited budgets"));
    // Refuse reconstructing lost reservations: a delivered plan proves there
    // was previous state, which must be recovered instead of double-spent.
    if (previous.isEmpty()) {
        for (const auto &day : days) {
            if (day.second.allocation.value("date").toString() == date)
                return refuse(error, QStringLiteral("issued reservations are missing; restore the manager state"));
        }
    }
    bool changed = previous.isEmpty();
    for (const QString &key : documents.keys()) {
        if (documents.value(key).toObject().value("limits")
                != oldDocuments.value(key).toObject().value("limits")) changed = true;
    }
    if (!changed) { *plan = previous; return true; }
    *plan = QJsonObject{{"schemaVersion", 1}, {"authority", authority}, {"user", profile.user},
                       {"date", date}, {"members", members}, {"revision", revision},
                       {"documents", documents}, {"createdAt", now.toUTC().toString(Qt::ISODate)}};
    return true;
}
}
