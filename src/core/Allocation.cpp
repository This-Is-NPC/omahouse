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
            || !a.value(QStringLiteral("house")).isObject())
        return refuse(error, QStringLiteral("allocation needs date, positive revision and house"));
    const auto house = a.value(QStringLiteral("house")).toObject();
    if (house.isEmpty()) return refuse(error, QStringLiteral("allocation has no budgets"));
    for (auto it = house.begin(); it != house.end(); ++it) {
        int credit = 0;
        int elsewhere = 0;
        int counted = 0;
        const auto one = it.value().toObject();
        // `counted` is required and not defaulted, and that is the difference
        // between a field and a hole. Absent it would be read as a zero, which
        // is the shape that says *the household has counted none of this
        // machine's grants* -- so a document written by something that does not
        // know about the field hands over every grant this machine has ever
        // made, a second time, silently.
        if (it.key().isEmpty() || !it.value().isObject()
                || !whole(one.value(QStringLiteral("credit")), &credit)
                || !whole(one.value(QStringLiteral("elsewhere")), &elsewhere)
                || !whole(one.value(QStringLiteral("counted")), &counted)) {
            return refuse(error, QStringLiteral("a household budget is a credit, an elsewhere "
                                               "and a counted, all nonnegative whole seconds"));
        }
    }
    return true;
}

int spentSeconds(const Budget &budget, const Ledger &ledger)
{
    return budget.carriesOver() ? ledger.keptSecondsFor(budget.id)
                                : ledger.secondsFor(budget.id);
}

int allowanceSeconds(const Profile &profile, const Budget &budget,
                     const Ledger &ledger, const QDate &date)
{
    if (profile.allocation.isEmpty()) {
        // A pot's refills are in two places and both of them count: the ones
        // made today, still in today's grants, and the ones every night since
        // has folded into the running total. A daily budget has only the first,
        // because nothing of its is ever folded.
        return budget.dailyMinutes * 60 + ledger.grantedSeconds(budget.id)
            + (budget.carriesOver() ? ledger.keptGrantedFor(budget.id) : 0);
    }
    // What an operator has handed over here today, which the household has not
    // folded into its credit yet. Local adjustments are not in it -- `leave` is
    // refused on an enrolled profile, and this is that same rule at the place
    // the number is worked out.
    const int handedOver = ledger.creditedSeconds(budget.id);
    if (profile.allocation.value(QStringLiteral("date")).toString() != date.toString(Qt::ISODate)) {
        // A day the household has said nothing about. That used to be nothing
        // at all, and nothing an operator could type changed it -- which is the
        // worst moment for the one verb that exists so somebody can hand over
        // ten minutes with the manager unreachable.
        //
        // So it is exactly what somebody decided to hand over, and no daily
        // number: the household's allowance is the household's to give, and a
        // machine that has lost contact must not start issuing it. Nothing here
        // can leak time, because the only thing that raises it is an operator.
        return handedOver;
    }
    // The household's credit, less what the other computers have already spent
    // of it. What is left over here is then `allowance - spent here`, which is
    // the household's balance -- so an hour is an hour wherever the person
    // sits, and not a quota per machine.
    //
    // Two numbers and not one pre-baked cap, because they are different kinds
    // of fact and they go stale differently. `credit` is a decision and has a
    // correct current version; `elsewhere` is an observation and only grows. A
    // report that arrives late under-states `elsewhere`, so this machine allows
    // a little too much rather than too little -- bounded by how often the
    // household reports, which is the number an operator sets.
    const auto one = profile.allocation.value(QStringLiteral("house")).toObject()
                         .value(budget.id).toObject();
    //
    // And a third: how much of *this machine's own* credit the household has
    // already folded into `credit`. What an operator has handed over here since
    // is added on top, so a grant is ten minutes the instant it is typed rather
    // than at the next plan -- and it is ten minutes once, because the moment
    // the household counts it, `counted` rises by the same six hundred that
    // `credit` did and the sum does not move. By subtraction and never by
    // comparing a grant's clock against a statement's.
    //
    // It makes this machine briefly more generous than the household knows,
    // exactly as a late report of `elsewhere` does, and bounded by the same
    // thing: how often the household reports.
    const int fresh = qMax(0, handedOver - one.value(QStringLiteral("counted")).toInt(0));
    return qMax(0, one.value(QStringLiteral("credit")).toInt(0)
                       - one.value(QStringLiteral("elsewhere")).toInt(0) + fresh);
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
    const auto house = d.value(QStringLiteral("house")).toObject();
    QSet<QString> expected;
    for (const auto &budget : profile->budgets) if (budget.hasLimit()) expected.insert(budget.id);
    const auto keys = house.keys();
    if (QSet<QString>(keys.begin(), keys.end()) != expected)
        return refuse(error, QStringLiteral("allocation budgets differ from the profile"));
    if (profile->allocation.value(QStringLiteral("date")) == d.value(QStringLiteral("date"))) {
        const int oldRevision = profile->allocation.value(QStringLiteral("revision")).toInt();
        const int revision = d.value(QStringLiteral("revision")).toInt();
        if (revision < oldRevision || (revision == oldRevision && profile->allocation != d))
            return refuse(error, QStringLiteral("stale or conflicting allocation revision"));
        const auto old = profile->allocation.value(QStringLiteral("house")).toObject();
        for (const QString &key : keys) {
            // `elsewhere` is what other computers have spent, and consumption
            // adds up: a report saying they spent less than the last one is a
            // report going backwards, and honouring it would hand this machine
            // the same minutes twice. The same rule `collect` keeps.
            //
            // `credit` is free to move either way. It is a decision, and an
            // operator who lowers the daily number at four in the afternoon has
            // lowered it -- refusing that would be the household unable to take
            // back what it gave.
            if (house.value(key).toObject().value(QStringLiteral("elsewhere")).toInt()
                    < old.value(key).toObject().value(QStringLiteral("elsewhere")).toInt())
                return refuse(error, QStringLiteral("household consumption moved backwards"));
            // And `counted` the same way, for the same reason pointing the
            // other way: a statement that unlearns what it has counted from
            // here hands this machine every grant it has made a second time.
            if (house.value(key).toObject().value(QStringLiteral("counted")).toInt()
                    < old.value(key).toObject().value(QStringLiteral("counted")).toInt())
                return refuse(error, QStringLiteral("the household unlearned what it counted"));
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
            if (received.value("date").toString() == date
                    && received.value("revision").toInt() > oldRevision)
                return refuse(error, QStringLiteral("machine has a newer statement; restore the manager state"));
        }
    }
    QJsonObject documents;
    for (const auto &day : days) {
        documents.insert(day.first, QJsonObject{{"authority", authority}, {"machine", day.first},
                         {"user", profile.user}, {"date", date}, {"revision", revision},
                         {"house", QJsonObject{}}});
    }
    bool any = false;
    for (const auto &budget : profile.budgets) {
        if (!budget.hasLimit()) continue;
        any = true;
        // What the household has to spend today: the profile's own number plus
        // every grant an operator made anywhere. Local adjustments are left out
        // -- `leave` is a machine correcting its own balance and must not feed
        // back into the household's.
        qint64 credit = qint64(budget.dailyMinutes) * 60;
        qint64 spent = 0;
        for (const auto &day : days) {
            credit += day.second.creditedSeconds(budget.id);
            spent += day.second.secondsFor(budget.id);
        }
        if (credit < 0 || credit > 31536000)
            return refuse(error, QStringLiteral("household credit is outside the supported range"));
        if (spent < 0 || spent > 31536000)
            return refuse(error, QStringLiteral("household consumption is outside the supported range"));

        // No division. Every machine is told the same credit and what the
        // *others* have spent of it, so each of them works out the same balance
        // and an hour is an hour wherever the person sits.
        //
        // What this gives up is exclusivity: two computers both told there are
        // thirty minutes left can both start spending them. That is bounded by
        // how often the household reports and by nothing else, which is why the
        // reporting cadence stopped being a tuning knob and became the thing
        // that holds the sum together.
        for (const auto &day : days) {
            auto doc = documents.value(day.first).toObject();
            auto house = doc.value("house").toObject();
            const qint64 elsewhere = spent - day.second.secondsFor(budget.id);
            // What this machine's own grants contributed to `credit`, so it
            // can tell the ones already counted from the ones it has made
            // since. Taken from the very day the credit was summed out of, so
            // the two cannot disagree.
            const qint64 counted = day.second.creditedSeconds(budget.id);
            house.insert(budget.id,
                         QJsonObject{{"credit", static_cast<int>(credit)},
                                     {"elsewhere", static_cast<int>(elsewhere)},
                                     {"counted", static_cast<int>(counted)}});
            doc.insert("house", house);
            documents.insert(day.first, doc);
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
        if (documents.value(key).toObject().value("house")
                != oldDocuments.value(key).toObject().value("house")) changed = true;
    }
    if (!changed) { *plan = previous; return true; }
    *plan = QJsonObject{{"schemaVersion", 1}, {"authority", authority}, {"user", profile.user},
                       {"date", date}, {"members", members}, {"revision", revision},
                       {"documents", documents}, {"createdAt", now.toUTC().toString(Qt::ISODate)}};
    return true;
}
}
