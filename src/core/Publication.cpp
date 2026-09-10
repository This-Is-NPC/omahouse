#include "Publication.h"
#include "Json.h"
namespace omahouse
{
QJsonObject publicationRules(const Profile& profile)
{
    auto rules = profile.withoutTheStamp();
    rules.remove(QStringLiteral("allocation"));
    return rules;
}
QString publicationName(Publication state)
{
    switch (state)
    {
    case Publication::NotPaired:
        return QStringLiteral("not paired");
    case Publication::NeverPublished:
        return QStringLiteral("never published");
    case Publication::UpToDate:
        return QStringLiteral("up to date");
    case Publication::Behind:
        return QStringLiteral("behind");
    case Publication::ChangedThere:
        return QStringLiteral("changed there");
    }
    return { };
}
QString publicationSaid(Publication state)
{
    return publicationName(state);
}
QJsonObject Published::toJson() const
{
    return { { "schemaVersion", kPublicationSchema },
        { "publishedAt", publishedAt.toUTC().toString(Qt::ISODateWithMs) },
        { "profile", profile.toJson() } };
}
bool Published::fromJson(const QJsonObject& object, Published* out, QString* error)
{
    if (!checkSchemaVersion(object, kPublicationSchema, "published profile", error))
        return false;
    Published result;
    result.publishedAt
        = QDateTime::fromString(object.value("publishedAt").toString(), Qt::ISODateWithMs);
    if (!result.publishedAt.isValid())
    {
        if (error)
            *error = "published profile: invalid publishedAt";
        return false;
    }
    if (!Profile::fromJson(object.value("profile").toObject(), &result.profile, error))
        return false;
    *out = result;
    return true;
}
QJsonObject Resolution::toJson() const
{
    return { { "schemaVersion", kPublicationSchema }, { "observed", observed },
        { "draft", draft } };
}
bool Resolution::fromJson(const QJsonObject& object, Resolution* out, QString* error)
{
    if (!checkSchemaVersion(object, kPublicationSchema, "profile resolution", error))
        return false;
    if ((!object.value("observed").isObject() && !object.value("observed").isNull())
        || !object.value("draft").isObject())
    {
        if (error)
            *error = "profile resolution: invalid observed or draft";
        return false;
    }
    *out = { object.value("observed"), object.value("draft").toObject() };
    return true;
}
Publication publicationOf(const Profile& draft, const Published* published,
    const Collected& collected, bool paired, const Resolution* resolved)
{
    if (!paired)
        return Publication::NotPaired;
    const auto rules = publicationRules(draft);
    const bool changed = collected.collected
        && (published ? (!collected.has
                            || publicationRules(collected.profile)
                                != publicationRules(published->profile))
                      : (collected.has && publicationRules(collected.profile) != rules));
    if (changed)
    {
        if (!resolved || resolved->observed != collected.observed() || resolved->draft != rules)
            return Publication::ChangedThere;
        // Keeping a draft that equals the last publication still needs a push:
        // the machine is running the version the operator just rejected.
        if (!collected.has || publicationRules(collected.profile) != rules)
            return Publication::Behind;
    }
    if (!published)
        return Publication::NeverPublished;
    return rules == publicationRules(published->profile) ? Publication::UpToDate
                                                         : Publication::Behind;
}
Standing standingOf(const QVector<PublicationRow>& rows)
{
    Standing result;
    for (const auto& row : rows)
        if (row.state == Publication::ChangedThere)
            result.changedOn.append(row.machine);
    result.unresolved = !result.changedOn.isEmpty();
    return result;
}
QString publicationSummary(const QVector<PublicationRow>& rows)
{
    const auto standing = standingOf(rows);
    if (standing.unresolved)
        return QStringLiteral("unresolved (%1)").arg(standing.changedOn.join(", "));
    int behind = 0;
    bool never = false;
    int paired = 0;
    for (const auto& row : rows)
    {
        paired += row.state != Publication::NotPaired;
        behind += row.state == Publication::Behind;
        never |= row.state == Publication::NeverPublished;
    }
    if (behind)
        return QStringLiteral("%1 behind").arg(behind);
    return never || paired == 0 ? QStringLiteral("never") : QStringLiteral("up to date");
}
} // namespace omahouse
