#include "PublicationStore.h"
#include "Json.h"
#include "Paths.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
namespace omahouse
{
bool readCollected(const QString& machine, const QString& user, Collected* out, QString* error)
{
    Collected said;
    said.machine = machine;
    QJsonObject document;
    bool missing = false;
    if (!readJsonObject(paths::elsewhereProfileFile(machine, user), &document, error, &missing))
        return false;
    if (!missing)
    {
        QVector<Profile> profiles;
        if (!profilesFromJson(document, &profiles, error))
            return false;
        if (profiles.size() > 1 || (!profiles.isEmpty() && profiles.first().user != user))
        {
            *error = QStringLiteral("%1 sent a profile belonging to another account").arg(machine);
            return false;
        }
        said.collected = true;
        said.has = !profiles.isEmpty();
        if (said.has)
            said.profile = profiles.first();
    }
    *out = said;
    return true;
}
bool readPublished(
    const QString& machine, const QString& user, Published* out, bool* missing, QString* error)
{
    QJsonObject document;
    if (!readJsonObject(paths::publishedProfileFile(machine, user), &document, error, missing))
        return false;
    if (*missing)
        return true;
    if (!Published::fromJson(document, out, error))
        return false;
    if (out->profile.user != user)
    {
        *error = "published profile belongs to another account";
        return false;
    }
    return true;
}
bool publicationRows(
    const Profile& draft, const QVector<Machine>& fleet, QVector<PublicationRow>* rows, QString* error)
{
    rows->clear();
    for (const auto& machine : fleet)
    {
        Collected collected;
        Published published;
        bool missing = false;
        if (!readCollected(machine.name, draft.user, &collected, error)
            || !readPublished(machine.name, draft.user, &published, &missing, error))
            return false;
        QJsonObject decision;
        bool noDecision = false;
        Resolution resolved;
        if (!readJsonObject(paths::resolvedProfileFile(machine.name, draft.user), &decision, error,
                &noDecision))
            return false;
        if (!noDecision && !Resolution::fromJson(decision, &resolved, error))
            return false;
        rows->append({ machine.name,
            publicationOf(draft, missing ? nullptr : &published, collected, machine.reachable(),
                noDecision ? nullptr : &resolved) });
    }
    return true;
}
bool fileCollected(
    const QString& machine, const QString& user, const QByteArray& raw, QString* error)
{
    QJsonParseError parsing;
    const auto document = QJsonDocument::fromJson(raw, &parsing);
    if (!document.isObject())
    {
        *error = "what came in is not a profile: " + parsing.errorString();
        return false;
    }
    QVector<Profile> profiles;
    if (!profilesFromJson(document.object(), &profiles, error))
        return false;
    if (profiles.size() > 1)
    {
        *error = "a collection takes one profile, or none";
        return false;
    }
    if (!profiles.isEmpty() && profiles.first().user != user)
    {
        *error = QStringLiteral("that profile belongs to %1, and it was offered as %2's")
                     .arg(profiles.first().user, user);
        return false;
    }
    const auto path = paths::elsewhereProfileFile(machine, user);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        *error = "cannot create collection directory";
        return false;
    }
    return writeJsonAtomically(path, profilesToJson(profiles), error);
}
bool resolveCollected(const QString& machine, const QString& user, const Collected& collected,
    const Profile& draft, QString* error)
{
    const auto path = paths::resolvedProfileFile(machine, user);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        *error = "cannot create resolution directory";
        return false;
    }
    return writeJsonAtomically(
        path, Resolution { collected.observed(), publicationRules(draft) }.toJson(), error);
}
}
