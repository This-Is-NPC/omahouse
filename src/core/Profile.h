#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace omahouse {

// The three nouns of spec.md §2 and nothing else. A profile is an account under
// rules, a rule is a (selector, verdict) pair, a budget is a daily counter with
// a selector, a limit and an action for running out.
//
// The generalisation that keeps it to three: the session is only the budget
// whose selector is `*`. There is no separate idea of "the user's time" beside
// "the app's time", and so there is no branch for it anywhere below.

enum class Verdict { Allow, Deny };
enum class OnExhausted { Warn, Close, Logout };

QString verdictName(Verdict verdict);
bool verdictFromName(const QString &name, Verdict *out);
QString onExhaustedName(OnExhausted action);
bool onExhaustedFromName(const QString &name, OnExhausted *out);

/// Whether `selector` names the app `scopeId`. `*` is every app, which is what
/// makes the session budget an ordinary budget; anything else is the id itself,
/// matched whole and case sensitively, because `org.freedesktop.Platform` and
/// `org.freedesktop.platform` are two different flatpaks as far as flatpak is
/// concerned.
///
/// An empty `scopeId` is a live scope the parser could not name, and `*` matches
/// it like anything else: the session budget is time on the machine, and 46
/// `tmux-spawn-<uuid>.scope` full of processes are somebody at the keyboard. A
/// named selector never matches it -- there is no name to match -- so a budget
/// about one app never bills it and a rule about one app never judges it, and
/// what decides it is the profile's default verdict. Under `default: deny` that
/// closes it, which is the honest reading: a thing nobody can even name is
/// certainly not on the list of what was released.
bool selectorMatches(const QString &selector, const QString &scopeId);

struct Rule {
    QString match;
    Verdict verdict = Verdict::Allow;
};

struct Budget {
    QString id;
    QString match;
    /// Zero or less is a budget with no limit: it counts, and it never runs out.
    /// That is the honest reading of a missing `dailyMinutes`, and it is worth
    /// having -- an app somebody wants a number for at the end of the day but
    /// no rule about is exactly this.
    int dailyMinutes = 0;
    OnExhausted onExhausted = OnExhausted::Warn;

    bool hasLimit() const { return dailyMinutes > 0; }
};

struct Profile {
    QString user;
    QString displayName;
    bool enabled = true;
    /// False is observation: count and report, close and log out nothing. The
    /// default for a profile that has just been written, because seeing a day of
    /// the report before switching the teeth on is what the lan house always
    /// did (spec.md §5).
    bool enforce = false;
    /// What happens to an app no rule names. `deny` makes the rules an
    /// allowlist, `allow` makes them a denylist, and it is the same engine.
    Verdict defaultVerdict = Verdict::Allow;
    /// Minutes remaining at which the user is warned, one warning per mark.
    QVector<int> warnAt;
    /// Seconds between running out and the action landing.
    int graceSeconds = 0;
    QVector<Rule> rules;
    QVector<Budget> budgets;

    /// The first rule that names `scopeId` wins; with none, the profile default
    /// decides. First and not last because the rules are read in the order the
    /// operator wrote them, and the first line about a program is the one they
    /// would point at when asked why it is allowed.
    Verdict verdictFor(const QString &scopeId) const;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Profile *out, QString *error);
};

/// The whole of /etc/omahouse/profiles.json, spec.md §4.
QJsonObject profilesToJson(const QVector<Profile> &profiles);
bool profilesFromJson(const QJsonObject &root, QVector<Profile> *out, QString *error);

bool readProfiles(const QString &path, QVector<Profile> *out, QString *error,
                  bool *missing = nullptr);
bool writeProfiles(const QString &path, const QVector<Profile> &profiles, QString *error);

} // namespace omahouse
