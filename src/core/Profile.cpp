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

// The rules of the app half and the rules of the web half are the same pair
// written the same way, so they are read and written by one routine. Two copies
// of this would be two chances for `verdict` to be spelled differently in the
// two halves of one file.
QJsonArray rulesToJson(const QVector<Rule> &rules)
{
    QJsonArray array;
    for (const Rule &rule : rules) {
        array.append(QJsonObject{
            {QStringLiteral("match"), rule.match},
            {QStringLiteral("verdict"), verdictName(rule.verdict)},
        });
    }
    return array;
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

/// The `rules` list of either half of a profile. `what` names the half in the
/// message, because "has a rule with an unknown verdict" is only useful to
/// somebody who is told which list said it.
bool rulesFromJson(const QJsonObject &object, const QString &named, const QString &what,
                   QVector<Rule> *out, QString *error)
{
    const QJsonValue value = object.value(QStringLiteral("rules"));
    if (!value.isUndefined() && !value.isNull() && !value.isArray()) {
        if (error)
            *error = QStringLiteral("%1 has a non-list %2").arg(named, what);
        return false;
    }
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isObject()) {
            if (error)
                *error = QStringLiteral("%1 has a %2 entry that is not an object").arg(named, what);
            return false;
        }
        const QJsonObject fields = entry.toObject();
        Rule rule;
        if (!wantsString(fields, QStringLiteral("match"), named, &rule.match, true, error))
            return false;
        QString verdictText;
        if (!wantsString(fields, QStringLiteral("verdict"), named, &verdictText, true, error))
            return false;
        if (!verdictFromName(verdictText, &rule.verdict)) {
            if (error) {
                *error = QStringLiteral("%1 has a %2 entry with an unknown verdict %3")
                             .arg(named, what, verdictText);
            }
            return false;
        }
        out->append(rule);
    }
    return true;
}

/// Reads a verdict field that is allowed to be missing. `stated` is what tells
/// "said allow" from "said nothing", which for `incognito` are two different
/// instructions -- see the note on `Web` in the header.
bool wantsVerdict(const QJsonObject &object, const QString &key, const QString &named,
                  Verdict *value, bool *stated, QString *error)
{
    QString text;
    if (!wantsString(object, key, named, &text, false, error))
        return false;
    if (text.isEmpty())
        return true;
    if (!verdictFromName(text, value)) {
        if (error)
            *error = QStringLiteral("%1 has an unknown %2 verdict %3").arg(named, key, text);
        return false;
    }
    if (stated)
        *stated = true;
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
    case OnExhausted::Block:
        return QStringLiteral("block");
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
    if (name == QStringLiteral("block")) {
        *out = OnExhausted::Block;
        return true;
    }
    return false;
}

QString selectsName(Selects selects)
{
    return selects == Selects::Site ? QStringLiteral("site") : QStringLiteral("app");
}

bool selectsFromName(const QString &name, Selects *out)
{
    if (name == QStringLiteral("app")) {
        *out = Selects::App;
        return true;
    }
    if (name == QStringLiteral("site")) {
        *out = Selects::Site;
        return true;
    }
    return false;
}

bool actionFits(Selects selects, OnExhausted action)
{
    switch (action) {
    case OnExhausted::Warn:
        return true;
    case OnExhausted::Block:
        return selects == Selects::Site;
    case OnExhausted::Close:
    case OnExhausted::Logout:
        return selects == Selects::App;
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

Verdict Web::verdictFor(const QString &domain) const
{
    for (const Rule &rule : rules) {
        if (selectorMatches(rule.match, domain))
            return rule.verdict;
    }
    return defaultVerdict;
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

    const QJsonArray ruleArray = rulesToJson(rules);

    QJsonArray budgetArray;
    for (const Budget &budget : budgets) {
        QJsonObject object{
            {QStringLiteral("id"), budget.id},
            {QStringLiteral("match"), budget.match},
        };
        // Written only when it is `site`. Absent is `app`, which is every budget
        // written before sites had one, and putting `"kind": "app"` into all of
        // them would rewrite every profiles.json there is to say what it already
        // said -- the same discipline `presence` and `sites` keep in the ledger.
        if (budget.isSite())
            object.insert(QStringLiteral("kind"), selectsName(budget.selects));
        // A budget with no limit leaves the field out rather than writing a
        // zero: zero minutes reads like "no time at all", which is the opposite
        // of what it means here.
        if (budget.hasLimit())
            object.insert(QStringLiteral("dailyMinutes"), budget.dailyMinutes);
        object.insert(QStringLiteral("onExhausted"), onExhaustedName(budget.onExhausted));
        budgetArray.append(object);
    }

    QJsonObject object{
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

    // Written only when it says something. A `"web": {}` on every profile ever
    // created would make "this account has no web rules" and "this account had
    // its web rules taken away" two different-looking files that mean the same
    // thing, and docs/design.md §11 turns on those two being one state.
    if (web.saysAnything()) {
        QJsonObject webObject{
            {QStringLiteral("default"), verdictName(web.defaultVerdict)},
            {QStringLiteral("rules"), rulesToJson(web.rules)},
        };
        if (web.incognitoStated)
            webObject.insert(QStringLiteral("incognito"), verdictName(web.incognito));
        object.insert(QStringLiteral("web"), webObject);
    }
    return object;
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
    // is the failure mode `onerr=succeed` on the PAM line of docs/design.md §2 exists
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

    if (!rulesFromJson(object, named, QStringLiteral("rules"), &profile.rules, error))
        return false;

    // The web half, docs/design.md §11. Absent is the ordinary state of every
    // profile ever written before this existed, so it is not a failure and not
    // a reason to guess: the profile simply asks the browser for nothing.
    const QJsonValue webValue = object.value(QStringLiteral("web"));
    if (!webValue.isUndefined() && !webValue.isNull()) {
        if (!webValue.isObject()) {
            if (error)
                *error = QStringLiteral("%1 has a web that is not an object").arg(named);
            return false;
        }
        const QJsonObject webObject = webValue.toObject();
        const QString webNamed = QStringLiteral("the web rules of %1").arg(profile.user);
        if (!wantsVerdict(webObject, QStringLiteral("default"), webNamed,
                          &profile.web.defaultVerdict, nullptr, error))
            return false;
        if (!wantsVerdict(webObject, QStringLiteral("incognito"), webNamed,
                          &profile.web.incognito, &profile.web.incognitoStated, error))
            return false;
        if (!rulesFromJson(webObject, webNamed, QStringLiteral("rules"), &profile.web.rules,
                           error))
            return false;
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
        // Read before `onExhausted`, because what a budget may do when it runs
        // out depends on what it is a budget for.
        QString kindText;
        if (!wantsString(entry, QStringLiteral("kind"), named, &kindText, false, error))
            return false;
        if (!kindText.isEmpty() && !selectsFromName(kindText, &budget.selects)) {
            if (error) {
                *error = QStringLiteral("%1 has a budget with an unknown kind %2; it is app "
                                        "or site")
                             .arg(named, kindText);
            }
            return false;
        }
        // A site budget's default action is to block, an app's is to warn.
        // Different defaults because they are the honest reading of a budget
        // written without one: an app budget with no action named is the
        // observing stage docs/design.md §5 describes, and there is no observing
        // stage left for sites -- §5.2 was it, and a site given a limit is
        // somebody asking for the stage after.
        if (budget.isSite())
            budget.onExhausted = OnExhausted::Block;
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
        // Refused, and not quietly repaired into something that would run. A
        // `close` on a site names no cgroup and a `block` on an app names no
        // domain, so either one is an instruction with nothing on the other end
        // of it -- and a rule that reads back out of `profile show` and never
        // fires is the exact failure §11 refuses when it refuses a precedence.
        if (!actionFits(budget.selects, budget.onExhausted)) {
            if (error) {
                *error = QStringLiteral("%1 has a budget %2 about a %3 whose onExhausted is "
                                        "%4, and %4 is not something that can happen to a %3")
                             .arg(named, budget.id, selectsName(budget.selects),
                                  onExhaustedName(budget.onExhausted));
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
