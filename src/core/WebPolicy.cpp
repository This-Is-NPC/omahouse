#include "WebPolicy.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>

namespace omahouse {

namespace {

const QString kEverything = QStringLiteral("*");

QJsonArray toArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values)
        array.append(value);
    return array;
}

} // namespace

QJsonObject ChromiumPolicy::toJson() const
{
    QJsonObject object;
    // Empty keys are left out rather than written as empty lists. An operator
    // reading this file has to be able to see what it does by what is in it, and
    // `"URLAllowlist": []` is a line that answers a question nobody asked.
    if (!blocklist.isEmpty())
        object.insert(QStringLiteral("URLBlocklist"), toArray(blocklist));
    if (!allowlist.isEmpty())
        object.insert(QStringLiteral("URLAllowlist"), toArray(allowlist));
    // 1 is "incognito is not available". 0 would be "incognito is available and
    // this file insists on it", which is a thing omahouse has never been asked
    // to say -- see the note on `Web` in Profile.h.
    if (incognitoDenied)
        object.insert(QStringLiteral("IncognitoModeAvailability"), 1);
    return object;
}

ChromiumPolicy chromiumPolicyFor(const QVector<Profile> &profiles)
{
    ChromiumPolicy policy;

    QVector<const Profile *> speaking;
    for (const Profile &profile : profiles) {
        if (!profile.enabled || !profile.web.saysAnything())
            continue;
        speaking.append(&profile);
    }
    if (speaking.isEmpty())
        return policy;

    // Every domain any profile has an opinion about, once, in an order that
    // depends on the set and not on who was read first.
    QStringList mentioned;
    QSet<QString> seen;
    bool everythingBlocked = false;
    for (const Profile *profile : speaking) {
        if (profile->web.defaultVerdict == Verdict::Deny)
            everythingBlocked = true;
        if (profile->web.incognitoStated && profile->web.incognito == Verdict::Deny)
            policy.incognitoDenied = true;
        for (const Rule &rule : profile->web.rules) {
            if (rule.match == kEverything) {
                // `web block <user> '*'` is `--only-listed` written the other
                // way round, and it has to mean the same thing rather than
                // reaching the file as a literal `*` twice.
                if (rule.verdict == Verdict::Deny)
                    everythingBlocked = true;
                continue;
            }
            if (!seen.contains(rule.match)) {
                seen.insert(rule.match);
                mentioned.append(rule.match);
            }
        }
    }
    std::sort(mentioned.begin(), mentioned.end());

    if (everythingBlocked)
        policy.blocklist.append(kEverything);

    for (const QString &domain : mentioned) {
        // The composition rule of the header, and it is one line because it has
        // to be readable as one sentence: allowed here only if allowed by
        // everybody who has a say.
        bool allowedByAll = true;
        for (const Profile *profile : speaking) {
            if (profile->web.verdictFor(domain) == Verdict::Deny) {
                allowedByAll = false;
                break;
            }
        }
        // A blocked domain is never also allowlisted, and that is not tidiness.
        // Chromium's own precedence gives the allowlist the tie, so a domain in
        // both lists is a domain that opens -- which would turn "the most
        // restrictive wins" into its opposite in exactly the case the rule
        // exists for.
        (allowedByAll ? policy.allowlist : policy.blocklist).append(domain);
    }

    // An allowlist with nothing blocked beside it is inert, and a file holding
    // only inert keys is worse than no file: it makes a machine look managed
    // when it is not, and it survives `omahouse web allow` of the last blocked
    // site as a leftover nobody can account for. So it is dropped, and
    // `needed()` goes false with it -- which is what makes taking back the last
    // block put the machine back where it started.
    //
    // This is not a guess about Chromium's behaviour. `.temp/spike-extension.md`
    // §7 measured the same trap one policy over: a `NativeMessagingAllowlist`
    // with no blocklist beside it let through exactly the host it was meant to
    // keep out. The allowlist is only ever an exception carved out of a
    // blocklist. `omahouse web allow` says so in the line it prints.
    if (policy.blocklist.isEmpty())
        policy.allowlist.clear();
    return policy;
}

} // namespace omahouse
