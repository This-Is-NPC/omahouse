#include "Kind.h"

#include "Json.h"

namespace omahouse {
namespace {

const QLatin1String kAlone("alone");
const QLatin1String kManager("manager");
const QLatin1String kManaged("managed");

} // namespace

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::Alone:   return kAlone;
    case Kind::Manager: return kManager;
    case Kind::Managed: return kManaged;
    }
    return kAlone;
}

bool kindFromName(const QString &name, Kind *out)
{
    if (name == kAlone) {
        *out = Kind::Alone;
        return true;
    }
    if (name == kManager) {
        *out = Kind::Manager;
        return true;
    }
    if (name == kManaged) {
        *out = Kind::Managed;
        return true;
    }
    return false;
}

QString kindSaid(Kind kind)
{
    switch (kind) {
    case Kind::Alone:
        // Said as a complete state and not as a missing one. One computer under
        // rules is the whole product working.
        return QStringLiteral("on its own, and nothing is missing");
    case Kind::Manager:
        return QStringLiteral("the household's console: it holds the list of "
                              "computers and adds up their days");
    case Kind::Managed:
        return QStringLiteral("managed from another computer, and still enforcing "
                              "its own rules on its own");
    }
    return QString();
}

QJsonObject ThisMachine::toJson() const
{
    QJsonObject object {
        {QStringLiteral("schemaVersion"), kSchemaVersion},
        {QStringLiteral("kind"), kindName(kind)},
    };
    if (!name.isEmpty())
        object.insert(QStringLiteral("name"), name);
    if (!managedBy.isEmpty())
        object.insert(QStringLiteral("managedBy"), managedBy);
    if (since.isValid())
        object.insert(QStringLiteral("since"), since.toString(Qt::ISODate));
    return object;
}

bool ThisMachine::fromJson(const QJsonObject &object, ThisMachine *out, QString *error)
{
    if (!checkSchemaVersion(object, QStringLiteral("machine.json"), error))
        return false;

    ThisMachine machine;
    const QString kind = object.value(QStringLiteral("kind")).toString();
    // Refused rather than defaulted. A kind nobody here understands is a file
    // from a build that knew something this one does not, and quietly reading it
    // as `alone` would drop a machine out of a household it is really in.
    if (!kindFromName(kind, &machine.kind)) {
        *error = QStringLiteral("machine.json calls this machine '%1', which is "
                                "not alone, manager or managed")
                         .arg(kind);
        return false;
    }
    machine.name = object.value(QStringLiteral("name")).toString();
    machine.managedBy = object.value(QStringLiteral("managedBy")).toString();

    const QString since = object.value(QStringLiteral("since")).toString();
    if (!since.isEmpty()) {
        machine.since = QDateTime::fromString(since, Qt::ISODate);
        if (!machine.since.isValid()) {
            *error = QStringLiteral("machine.json has '%1' where a date belongs")
                             .arg(since);
            return false;
        }
    }

    // A managed machine with nobody managing it is a half-written link, and the
    // verbs that follow would look for a manager that is not named anywhere.
    if (machine.kind == Kind::Managed && machine.managedBy.isEmpty()) {
        *error = QStringLiteral("machine.json says this machine is managed and "
                                "names no manager");
        return false;
    }

    *out = machine;
    return true;
}

bool readThisMachine(const QString &path, ThisMachine *out, QString *error,
                     bool *missing)
{
    QJsonObject root;
    bool absent = false;
    if (!readJsonObject(path, &root, error, &absent))
        return false;
    if (missing)
        *missing = absent;
    if (absent) {
        // No file is a machine on its own, which is most of them.
        *out = ThisMachine();
        return true;
    }
    return ThisMachine::fromJson(root, out, error);
}

bool writeThisMachine(const QString &path, const ThisMachine &machine, QString *error)
{
    return writeJsonAtomically(path, machine.toJson(), error);
}

} // namespace omahouse
