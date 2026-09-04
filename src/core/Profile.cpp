#include "Profile.h"

#include "Json.h"

#include <QJsonArray>
#include <QJsonValue>

namespace omahouse {

namespace {

const QString kEverything = QStringLiteral("*");

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

// Told apart the way omafiles tells them apart: leaving a field out and writing
// the wrong kind of thing in it send a reader looking in two different places. A
// permissive conversion would read `"enforce": "yes"` as false and hand somebody
// a profile with no teeth that says it has them.
bool wantsBool(const QJsonObject &object, const QString &key, const QString &what,
               bool *value, QString *error)
{
    const QJsonValue found = object.value(key);
    if (found.isUndefined() || found.isNull())
        return true;
    if (!found.isBool()) {
        if (error)
            *error = QStringLiteral("%1 has a non-boolean %2").arg(what, key);
        return false;
    }
    *value = found.toBool();
    return true;
}

bool wantsInt(const QJsonObject &object, const QString &key, const QString &what, int *value,
              QString *error)
{
    const QJsonValue found = object.value(key);
    if (found.isUndefined() || found.isNull())
        return true;
    if (!found.isDouble()) {
        if (error)
            *error = QStringLiteral("%1 has a non-numeric %2").arg(what, key);
        return false;
    }
    *value = found.toInt();
    return true;
}

} // namespace

QString verdictName(Verdict verdict)
{
    return verdict == Verdict::Deny ? QStringLiteral("deny") : QStringLiteral("allow");
}

bool verdictFromName(const QString &name, Verdict *out)
{
    if (name == QStringLiteral("allow")) {
        *out = Verdict::Allow;
        return true;
    }
    if (name == QStringLiteral("deny")) {
        *out = Verdict::Deny;
        return true;
    }
    return false;
}

QString onExhaustedName(OnExhausted action)
{
    switch (action) {
    case OnExhausted::Close:
        return QStringLiteral("close");
    case OnExhausted::Logout:
        return QStringLiteral("logout");
    case OnExhausted::Warn:
        break;
    }
    return QStringLiteral("warn");
}

bool onExhaustedFromName(const QString &name, OnExhausted *out)
{
    if (name == QStringLiteral("warn")) {
        *out = OnExhausted::Warn;
        return true;
    }
    if (name == QStringLiteral("close")) {
        *out = OnExhausted::Close;
        return true;
    }
    if (name == QStringLiteral("logout")) {
        *out = OnExhausted::Logout;
        return true;
    }
    return false;
}

bool selectorMatches(const QString &selector, const QString &scopeId)
{
    // Everything is everything, and a scope nothing could name is still
    // something. This is the whole of the id requirement: it lives here, on the
    // half of the question that needs a name, and not on whether the scope is
    // alive.
    if (selector == kEverything)
        return true;
    return !scopeId.isEmpty() && selector == scopeId;
}

Verdict Profile::verdictFor(const QString &scopeId) const
{
    for (const Rule &rule : rules) {
        if (selectorMatches(rule.match, scopeId))
            return rule.verdict;
    }
    return defaultVerdict;
}

QJsonObject Profile::toJson() const
{
    QJsonArray warnArray;
    for (int mark : warnAt)
        warnArray.append(mark);

    QJsonArray ruleArray;
    for (const Rule &rule : rules) {
        ruleArray.append(QJsonObject{
            {QStringLiteral("match"), rule.match},
            {QStringLiteral("verdict"), verdictName(rule.verdict)},
        });
    }

    QJsonArray budgetArray;
    for (const Budget &budget : budgets) {
        QJsonObject object{
            {QStringLiteral("id"), budget.id},
            {QStringLiteral("match"), budget.match},
        };
        // A budget with no limit leaves the field out rather than writing a
        // zero: zero minutes reads like "no time at all", which is the opposite
        // of what it means here.
        if (budget.hasLimit())
            object.insert(QStringLiteral("dailyMinutes"), budget.dailyMinutes);
        object.insert(QStringLiteral("onExhausted"), onExhaustedName(budget.onExhausted));
        budgetArray.append(object);
    }

    return QJsonObject{
        {QStringLiteral("user"), user},
        {QStringLiteral("displayName"), displayName},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("enforce"), enforce},
        {QStringLiteral("default"), verdictName(defaultVerdict)},
        {QStringLiteral("warnAt"), warnArray},
        {QStringLiteral("grace"), graceSeconds},
        {QStringLiteral("rules"), ruleArray},
        {QStringLiteral("budgets"), budgetArray},
    };
}

bool Profile::fromJson(const QJsonObject &object, Profile *out, QString *error)
{
    Profile profile;
    const QString what = QStringLiteral("a profile");

    if (!wantsString(object, QStringLiteral("user"), what, &profile.user, true, error))
        return false;
    if (profile.user.isEmpty()) {
        if (error)
            *error = QStringLiteral("a profile has an empty user");
        return false;
    }
    const QString named = QStringLiteral("the profile of %1").arg(profile.user);

    if (!wantsString(object, QStringLiteral("displayName"), named, &profile.displayName, false,
                     error))
        return false;
    if (!wantsBool(object, QStringLiteral("enabled"), named, &profile.enabled, error))
        return false;
    if (!wantsBool(object, QStringLiteral("enforce"), named, &profile.enforce, error))
        return false;
    if (!wantsInt(object, QStringLiteral("grace"), named, &profile.graceSeconds, error))
        return false;

    // A missing `default` is allow, and deliberately not deny. Guessing deny
    // for a half-written profile locks somebody out of their own machine, which
    // is the failure mode `onerr=succeed` on the PAM line of spec.md §2 exists
    // to refuse; guessing allow leaves a profile that counts and does not bite.
    QString defaultName;
    if (!wantsString(object, QStringLiteral("default"), named, &defaultName, false, error))
        return false;
    if (!defaultName.isEmpty() && !verdictFromName(defaultName, &profile.defaultVerdict)) {
        if (error)
            *error = QStringLiteral("%1 has an unknown default verdict %2").arg(named, defaultName);
        return false;
    }

    const QJsonValue warnValue = object.value(QStringLiteral("warnAt"));
    if (!warnValue.isUndefined() && !warnValue.isArray()) {
        if (error)
            *error = QStringLiteral("%1 has a non-list warnAt").arg(named);
        return false;
    }
    for (const QJsonValue &value : warnValue.toArray()) {
        if (!value.isDouble()) {
            if (error)
                *error = QStringLiteral("%1 has a non-numeric warnAt mark").arg(named);
            return false;
        }
        profile.warnAt.append(value.toInt());
    }

    const QJsonValue rulesValue = object.value(QStringLiteral("rules"));
    if (!rulesValue.isUndefined() && !rulesValue.isArray()) {
        if (error)
            *error = QStringLiteral("%1 has a non-list rules").arg(named);
        return false;
    }
    for (const QJsonValue &value : rulesValue.toArray()) {
        if (!value.isObject()) {
            if (error)
                *error = QStringLiteral("%1 has a rule that is not an object").arg(named);
            return false;
        }
        const QJsonObject entry = value.toObject();
        Rule rule;
        if (!wantsString(entry, QStringLiteral("match"), named, &rule.match, true, error))
            return false;
        QString verdictText;
        if (!wantsString(entry, QStringLiteral("verdict"), named, &verdictText, true, error))
            return false;
        if (!verdictFromName(verdictText, &rule.verdict)) {
            if (error) {
                *error = QStringLiteral("%1 has a rule with an unknown verdict %2")
                             .arg(named, verdictText);
            }
            return false;
        }
        profile.rules.append(rule);
    }

    const QJsonValue budgetsValue = object.value(QStringLiteral("budgets"));
    if (!budgetsValue.isUndefined() && !budgetsValue.isArray()) {
        if (error)
            *error = QStringLiteral("%1 has a non-list budgets").arg(named);
        return false;
    }
    for (const QJsonValue &value : budgetsValue.toArray()) {
        if (!value.isObject()) {
            if (error)
                *error = QStringLiteral("%1 has a budget that is not an object").arg(named);
            return false;
        }
        const QJsonObject entry = value.toObject();
        Budget budget;
        if (!wantsString(entry, QStringLiteral("id"), named, &budget.id, true, error))
            return false;
        if (!wantsString(entry, QStringLiteral("match"), named, &budget.match, true, error))
            return false;
        if (!wantsInt(entry, QStringLiteral("dailyMinutes"), named, &budget.dailyMinutes, error))
            return false;
        QString actionText;
        if (!wantsString(entry, QStringLiteral("onExhausted"), named, &actionText, false, error))
            return false;
        if (!actionText.isEmpty() && !onExhaustedFromName(actionText, &budget.onExhausted)) {
            if (error) {
                *error = QStringLiteral("%1 has a budget with an unknown onExhausted %2")
                             .arg(named, actionText);
            }
            return false;
        }
        profile.budgets.append(budget);
    }

    *out = profile;
    return true;
}

QJsonObject profilesToJson(const QVector<Profile> &profiles)
{
    QJsonArray array;
    for (const Profile &profile : profiles)
        array.append(profile.toJson());
    return QJsonObject{
        {QStringLiteral("schemaVersion"), kSchemaVersion},
        {QStringLiteral("profiles"), array},
    };
}

bool profilesFromJson(const QJsonObject &root, QVector<Profile> *out, QString *error)
{
    if (!checkSchemaVersion(root, QStringLiteral("profiles.json"), error))
        return false;

    const QJsonValue value = root.value(QStringLiteral("profiles"));
    if (!value.isArray()) {
        if (error)
            *error = QStringLiteral("profiles.json has no profiles list");
        return false;
    }

    QVector<Profile> profiles;
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isObject()) {
            if (error)
                *error = QStringLiteral("profiles.json has a profile that is not an object");
            return false;
        }
        Profile profile;
        if (!Profile::fromJson(entry.toObject(), &profile, error))
            return false;
        profiles.append(profile);
    }
    *out = profiles;
    return true;
}

bool readProfiles(const QString &path, QVector<Profile> *out, QString *error, bool *missing)
{
    QJsonObject root;
    bool absent = false;
    if (!readJsonObject(path, &root, error, &absent))
        return false;
    if (missing)
        *missing = absent;
    if (absent) {
        // No file is a machine where nobody has been given rules yet, and that
        // is a legible state rather than a failure.
        *out = {};
        return true;
    }
    return profilesFromJson(root, out, error);
}

bool writeProfiles(const QString &path, const QVector<Profile> &profiles, QString *error)
{
    return writeJsonAtomically(path, profilesToJson(profiles), error);
}

} // namespace omahouse
