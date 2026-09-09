#include "Fleet.h"

#include "Allocation.h"
#include "Json.h"

#include <QJsonArray>

namespace omahouse {

namespace {
QString textOf(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}
} // namespace

QJsonObject Machine::toJson() const
{
    QJsonObject object {{QStringLiteral("name"), name}};
    // Written only when they say something. A machine that has not been paired
    // yet should read as a name and not as a name with two empty strings
    // hanging off it -- the file is edited by hand and every key in it is a
    // question somebody has to answer.
    if (!nodeId.isEmpty())
        object.insert(QStringLiteral("nodeId"), nodeId);
    if (!endpoint.isEmpty())
        object.insert(QStringLiteral("endpoint"), endpoint);
    if (addedAt.isValid())
        object.insert(QStringLiteral("addedAt"), addedAt.toString(Qt::ISODate));
    return object;
}

bool Machine::fromJson(const QJsonObject &object, Machine *out, QString *error)
{
    Machine machine;
    machine.name = textOf(object, QStringLiteral("name"));
    if (machine.name.isEmpty()) {
        if (error)
            *error = QStringLiteral("machines.json has a machine with no name");
        return false;
    }
    machine.nodeId = textOf(object, QStringLiteral("nodeId"));
    machine.endpoint = textOf(object, QStringLiteral("endpoint"));
    const QString stamp = textOf(object, QStringLiteral("addedAt"));
    if (!stamp.isEmpty()) {
        machine.addedAt = QDateTime::fromString(stamp, Qt::ISODate);
        if (!machine.addedAt.isValid()) {
            if (error)
                *error = QStringLiteral("%1 has an addedAt that is not a date: %2")
                             .arg(machine.name, stamp);
            return false;
        }
    }
    *out = machine;
    return true;
}

QJsonObject machinesToJson(const QVector<Machine> &machines)
{
    QJsonArray array;
    for (const Machine &machine : machines)
        array.append(machine.toJson());
    return QJsonObject {
        {QStringLiteral("schemaVersion"), kMachinesSchema},
        {QStringLiteral("machines"), array},
    };
}

bool machinesFromJson(const QJsonObject &root, QVector<Machine> *out, QString *error)
{
    if (!checkSchemaVersion(root, kMachinesSchema, QStringLiteral("machines.json"), error))
        return false;

    const QJsonValue value = root.value(QStringLiteral("machines"));
    if (!value.isArray()) {
        if (error)
            *error = QStringLiteral("machines.json has no machines list");
        return false;
    }

    QVector<Machine> machines;
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isObject()) {
            if (error)
                *error = QStringLiteral("machines.json has a machine that is not an object");
            return false;
        }
        Machine machine;
        if (!Machine::fromJson(entry.toObject(), &machine, error))
            return false;
        // Two machines of one name is a household that cannot say which one it
        // means, and every verb here takes the name. Refused where the file is
        // read rather than left for a verb to trip over later.
        if (indexOfMachine(machines, machine.name) >= 0) {
            if (error)
                *error = QStringLiteral("machines.json names %1 twice").arg(machine.name);
            return false;
        }
        machines.append(machine);
    }
    *out = machines;
    return true;
}

bool readMachines(const QString &path, QVector<Machine> *out, QString *error, bool *missing)
{
    QJsonObject root;
    bool absent = false;
    if (!readJsonObject(path, &root, error, &absent))
        return false;
    if (missing)
        *missing = absent;
    if (absent) {
        *out = {};
        return true;
    }
    return machinesFromJson(root, out, error);
}

bool writeMachines(const QString &path, const QVector<Machine> &machines, QString *error)
{
    return writeJsonAtomically(path, machinesToJson(machines), error);
}

int indexOfMachine(const QVector<Machine> &machines, const QString &name)
{
    for (int i = 0; i < machines.size(); ++i) {
        if (machines.at(i).name == name)
            return i;
    }
    return -1;
}


QVector<HouseBudget> consolidate(const Profile &profile,
                                 const QVector<QPair<QString, Ledger>> &days)
{
    QVector<HouseBudget> house;
    for (const Budget &budget : profile.budgets) {
        if (budget.id.isEmpty())
            continue;
        HouseBudget total;
        total.id = budget.id;
        // Only operator credit changes the household total. Local adjustments
        // made by leave must not feed back into the next consolidation.
        int granted = 0;
        for (const auto &day : days) {
            // Whichever counter this budget spends. Safe here and nowhere that
            // adds *days* together: this function is handed one day per
            // machine, so a pot's running total is read once. `report` walks a
            // range and must go on reading the daily map alone, because a pot
            // is carried into the file of every day it touches and would be
            // counted once per day there.
            const int seconds = spentSeconds(budget, day.second);
            granted += givenTo(budget, day.second);
            total.spent.append(Contribution {day.first, seconds});
            total.totalSeconds += seconds;
        }
        // `givenTo` and not today's grants, so that a pot's capacity here is
        // everything it has ever been handed and not only what somebody handed
        // it since midnight. It is the same number the statement carries, out
        // of the same function, because a household total that disagreed with
        // what the machines were told would be two answers to one question.
        if (budget.hasLimit())
            total.limitSeconds = qMax(0, budget.dailyMinutes * 60 + granted);
        house.append(total);
    }
    return house;
}

} // namespace omahouse
