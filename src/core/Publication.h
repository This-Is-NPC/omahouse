#pragma once
#include "Profile.h"
#include <QJsonValue>
namespace omahouse
{
enum class Publication
{
    NotPaired,
    NeverPublished,
    UpToDate,
    Behind,
    ChangedThere
};
// Policy shared between machines, excluding stamps and local runtime allocation.
QJsonObject publicationRules(const Profile& profile);
QString publicationName(Publication state);
QString publicationSaid(Publication state);
struct Published
{
    QDateTime publishedAt;
    Profile profile;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject&, Published*, QString* error);
};
struct Collected
{
    QString machine;
    bool collected = false;
    bool has = false;
    Profile profile;
    QJsonValue observed() const
    {
        return has ? QJsonValue(profile.toJson()) : QJsonValue(QJsonValue::Null);
    }
};
// An explicit merge acknowledges one observed version and one chosen policy.
// It never advances the publication baseline, which only a successful push can do.
struct Resolution
{
    QJsonValue observed;
    QJsonObject draft;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject&, Resolution*, QString* error);
};
Publication publicationOf(const Profile& draft, const Published* published,
    const Collected& collected, bool paired, const Resolution* resolved = nullptr);
struct PublicationRow
{
    QString machine;
    Publication state;
};
struct Standing
{
    bool unresolved = false;
    QStringList changedOn;
};
Standing standingOf(const QVector<PublicationRow>& rows);
QString publicationSummary(const QVector<PublicationRow>& rows);
} // namespace omahouse
