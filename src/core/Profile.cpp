#include "AppScope.h"
#include "Profile.h"
#include "Allocation.h"

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

/// A budget's `match`: the list of names it is about, however many.
///
/// A bare string is refused rather than read as a list of one. Two spellings of
/// one value is a reader with a branch in it, a writer with a choice to make and
/// a case to pin that they agree, and what was buying all three was not
/// rewriting files that do not exist.
///
/// Also refused: a list with nothing in it, and a list with something that is
/// not a name in it. A budget that matches nothing is a clock nobody can spend
/// and nobody can see is unspendable, and repairing it into one that matches
/// everything would be the worst possible guess.
bool wantsNames(const QJsonObject &object, const QString &key, const QString &what,
                QStringList *value, QString *error)
{
    const QJsonValue found = object.value(key);
    if (!found.isArray()) {
        if (error) {
            *error = found.isUndefined() || found.isNull()
                ? QStringLiteral("%1 has no %2").arg(what, key)
                : QStringLiteral("%1 has a %2 that is not a list of names").arg(what, key);
        }
        return false;
    }
    QStringList names;
    for (const QJsonValue &entry : found.toArray()) {
        if (!entry.isString()) {
            if (error)
                *error = QStringLiteral("%1 has a %2 list with something that is not a name in "
                                        "it").arg(what, key);
            return false;
        }
        names.append(entry.toString());
    }
    if (names.isEmpty()) {
        if (error)
            *error = QStringLiteral("%1 has an empty %2, so it is about nothing")
                         .arg(what, key);
        return false;
    }
    *value = names;
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

QString resetsName(Resets resets)
{
    return resets == Resets::Never ? QStringLiteral("never") : QStringLiteral("daily");
}

bool resetsFromName(const QString &name, Resets *out)
{
    if (name == QStringLiteral("daily")) {
        *out = Resets::Daily;
        return true;
    }
    if (name == QStringLiteral("never")) {
        *out = Resets::Never;
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

bool selectorMatches(const QString &selector, const QString &scopeId, const QString &exePath)
{
    if (selectorMatches(selector, scopeId))
        return true;
    // `*` has already matched above; anything else that got here is a name, and
    // a name needs something to compare against.
    if (selector.isEmpty() || exePath.isEmpty())
        return false;
    // The gate. `exeCorroboratesId` answers true for an empty id, so a scope
    // nothing can name closes this door on its own and never reaches the line
    // below -- which is the rule Profile.h states and this must not quietly
    // undo.
    if (exeCorroboratesId(scopeId, exePath))
        return false;
    return exeCorroboratesId(selector, exePath);
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
    return verdictFor(scopeId, QString());
}

Verdict Profile::verdictFor(const QString &scopeId, const QString &exePath) const
{
    for (const Rule &rule : rules) {
        if (selectorMatches(rule.match, scopeId, exePath))
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
        QJsonObject object{{QStringLiteral("id"), budget.id}};
        // Always a list, however many names are in it.
        //
        // It was a plain string for one name and a list for two, and the reason
        // written here was not rewriting the profiles.json files that exist.
        // There are none: nothing is released and every file is on a machine
        // that can be rebuilt. So the second spelling bought nothing and cost a
        // reader that handles two types, a writer that chooses between them and
        // a case pinning that they agree. One shape.
        QJsonArray names;
        for (const QString &name : budget.match)
            names.append(name);
        object.insert(QStringLiteral("match"), names);
        // Written only when it is `site`. Absent is `app`, and this is not
        // about old files: it is a field most budgets have no opinion about,
        // and a default written into every one of them is noise in a file
        // people read.
        if (budget.isSite())
            object.insert(QStringLiteral("kind"), selectsName(budget.selects));
        // Written only when it is `never`, for the reason `kind` is: `daily` is
        // what a budget means when it says nothing, and saying it anyway would
        // put a word on every budget on every machine to no end.
        if (budget.carriesOver())
            object.insert(QStringLiteral("resets"), resetsName(budget.resets));
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
    if (!allocation.isEmpty())
        object.insert(QStringLiteral("allocation"), allocation);
    // Absent until something writes it, which is the same reason `kind` and
    // `resets` are absent: a profile nobody has changed has nothing to say
    // here, and `"writtenBy": ""` would be a field claiming an answer it does
    // not have.
    if (!writtenBy.isEmpty())
        object.insert(QStringLiteral("writtenBy"), writtenBy);
    if (writtenAt.isValid()) {
        object.insert(QStringLiteral("writtenAt"),
                      writtenAt.toOffsetFromUtc(writtenAt.offsetFromUtc())
                          .toString(Qt::ISODate));
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
    const QString named = profile.isForAnybody()
        ? QStringLiteral("the profile for anybody")
        : QStringLiteral("the profile of %1").arg(profile.user);
    if (object.contains(QStringLiteral("allocation"))) {
        if (!object.value(QStringLiteral("allocation")).isObject()
                || !validAllocation(object.value(QStringLiteral("allocation")).toObject(), error))
            return false;
        profile.allocation = object.value(QStringLiteral("allocation")).toObject();
    }

    if (!wantsString(object, QStringLiteral("writtenBy"), named, &profile.writtenBy, false,
                     error))
        return false;
    // Absent in every profiles.json written before this existed, which is every
    // one on a machine today. Present and unreadable is refused rather than
    // dropped: a stamp that silently became "never" would make the profile that
    // has it lose every tiebreak against one that does not.
    const QJsonValue stamped = object.value(QStringLiteral("writtenAt"));
    if (!stamped.isUndefined() && !stamped.isNull()) {
        if (!stamped.isString()) {
            if (error)
                *error = QStringLiteral("%1 has a non-string writtenAt").arg(named);
            return false;
        }
        profile.writtenAt = QDateTime::fromString(stamped.toString(), Qt::ISODate);
        if (!profile.writtenAt.isValid()) {
            if (error) {
                *error = QStringLiteral("%1 has a writtenAt that is not a date and time: %2")
                             .arg(named, stamped.toString());
            }
            return false;
        }
    }

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
        if (!wantsNames(entry, QStringLiteral("match"), named, &budget.match, error))
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
        QString resetsText;
        if (!wantsString(entry, QStringLiteral("resets"), named, &resetsText, false, error))
            return false;
        if (!resetsText.isEmpty() && !resetsFromName(resetsText, &budget.resets)) {
            if (error) {
                *error = QStringLiteral("%1 has a budget with an unknown resets %2; it is "
                                        "daily or never")
                             .arg(named, resetsText);
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
        // The one rule that is about *whose* profile this is, refused where the
        // file is read for the reason every refusal here is: a budget that reads
        // back out of `profile show` and never does what it says is worse than
        // one that was turned away.
        //
        // A pot is emptied by being spent and refilled by somebody handing over
        // more. On a login several people share, the first of them empties it
        // and the second sits down to a spent clock with no midnight coming --
        // because `never` is what took the midnight away. Whoever wants a
        // shared machine limited wants `daily`, and that is what this says.
        if (profile.isForAnybody() && budget.carriesOver()) {
            if (error) {
                *error = QStringLiteral("%1 has a budget %2 that never resets, and a profile "
                                        "for anybody is shared: the first person to sit down "
                                        "would empty it and nobody would ever refill it. Use "
                                        "daily.")
                             .arg(named, budget.id);
            }
            return false;
        }
        profile.budgets.append(budget);
    }

    *out = profile;
    return true;
}

QJsonObject Profile::withoutTheStamp() const
{
    QJsonObject bare = toJson();
    bare.remove(QStringLiteral("writtenBy"));
    bare.remove(QStringLiteral("writtenAt"));
    return bare;
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
