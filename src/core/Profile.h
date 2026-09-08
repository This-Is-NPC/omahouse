#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace omahouse {

// The three nouns of docs/design.md §2 and nothing else. A profile is an account under
// rules, a rule is a (selector, verdict) pair, a budget is a daily counter with
// a selector, a limit and an action for running out.
//
// The generalisation that keeps it to three: the session is only the budget
// whose selector is `*`. There is no separate idea of "the user's time" beside
// "the app's time", and so there is no branch for it anywhere below.

enum class Verdict { Allow, Deny };
enum class OnExhausted { Warn, Close, Logout, Block };

/// What a budget's selector is a name for.
///
/// The one field that had to be added for a site to have a budget, and it is
/// here rather than in a second list because the ambiguity is real and cannot be
/// resolved by looking at the string: `org.freedesktop.Platform` is a scope id
/// with dots in it and `youtube.com` is a domain with dots in it, and there is
/// no shape that tells them apart. `web.rules` gets away without this because it
/// is a separate list; `budgets` is one list, and a budget that guessed which
/// namespace it was in would eventually guess wrong about somebody's flatpak.
///
/// Absent is `app`, and `app` is not written into the file -- docs/design.md §4's
/// discipline for `presence` and `sites`, for the same reason: writing
/// `"kind": "app"` into every budget on every machine would rewrite every
/// profile there is to say what they already said.
enum class Selects { App, Site };

QString verdictName(Verdict verdict);
bool verdictFromName(const QString &name, Verdict *out);
QString onExhaustedName(OnExhausted action);
bool onExhaustedFromName(const QString &name, OnExhausted *out);
QString selectsName(Selects selects);
bool selectsFromName(const QString &name, Selects *out);

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

/// The same question asked of a scope that has an executable read out of it,
/// which is the only way a program opened from the Omarchy menu can be named.
///
/// The menu launches everything through `gtk-launch`, so systemd names the scope
/// after the shim and every entry collapses into that one id. A rule about the
/// program cannot match it, and under `default: deny` the program is closed
/// before its window appears -- with the notification accusing the launcher.
///
/// So the executable is allowed to answer, under two conditions that are the
/// whole of the care this needs:
///
///  - **Only where the id is not already the name of what is running.** An id
///    the executable corroborates is a scope that has said what it is, and
///    letting a second name in there would make the path a selector of its own:
///    `chromium` would be matched by a rule about anything living near it on the
///    disk.
///  - **Never instead of the id.** This only ever adds a match. That is what
///    keeps the flatpak case right -- `org.freedesktop.Platform` is the correct
///    name and `/usr/bin/bwrap` is the executable of every flatpak alike, so a
///    rule naming the flatpak goes on matching it and bwrap overrules nothing.
///
/// An empty `exePath` is nobody having looked, and having no opinion is not an
/// allowance: the scope is judged by its id exactly as before. An empty
/// `scopeId` is a scope nothing can name, and a named selector never matches
/// one -- an executable read out of a `tmux-spawn` scope would otherwise be a
/// hole through an allowlist that nothing in the profile names.
bool selectorMatches(const QString &selector, const QString &scopeId, const QString &exePath);

struct Rule {
    QString match;
    Verdict verdict = Verdict::Allow;
};

/// The web half of a profile -- docs/design.md §11.
///
/// Deliberately the same three parts the app half has: a default verdict, a
/// list of `(selector, verdict)` rules, and nothing else. A site is named by its
/// domain instead of by a scope id, and that is the whole of the difference. The
/// engine that reads it is `chromiumPolicyFor` in WebPolicy.h, and it is as pure
/// as `Policy::evaluate`.
///
/// `incognito` is the one field with no counterpart on the app side, and it is a
/// three-state on purpose: said `allow`, said `deny`, or not said at all. Not
/// said means omahouse asks the browser for nothing, which is not the same thing
/// as asking it to keep incognito switched on -- writing
/// `IncognitoModeAvailability: 0` would override whatever else on the machine
/// had turned it off, and omahouse being *more* permissive than it was asked to
/// be is the one direction this project never takes by default.
struct Web {
    /// What happens to a site no rule names. `deny` makes the rules the list of
    /// what may be opened; `allow` makes them the list of what may not. Absent
    /// is `allow`, for the reason docs/design.md §4 gives about the app default:
    /// the half-written profile that does not bite is the recoverable one.
    Verdict defaultVerdict = Verdict::Allow;
    bool incognitoStated = false;
    Verdict incognito = Verdict::Allow;
    QVector<Rule> rules;

    /// The first rule that names `domain` wins, and with none the default
    /// decides -- the same order, and for the same reason, as `verdictFor`.
    Verdict verdictFor(const QString &domain) const;

    /// Whether this profile asks the machine for anything at all.
    ///
    /// A `web` with no rules, the default verdict it was born with and nothing
    /// said about incognito is a profile that has never been given a web rule,
    /// or one whose last rule was just taken away. Both have to read the same,
    /// because that is what makes `omahouse web allow` of the last blocked site
    /// take the policy file off the machine instead of leaving an empty one --
    /// docs/design.md §11.
    bool saysAnything() const
    {
        return !rules.isEmpty() || defaultVerdict == Verdict::Deny || incognitoStated;
    }
};

struct Budget {
    QString id;
    /// Every name this one budget is about, in the order they were written.
    ///
    /// A list rather than a name, because one program is not always one id. A
    /// single Chromium window on real Omarchy produces two scopes -- `chromium`,
    /// holding the child processes, and `org.chromium.Chromium`, holding the
    /// one that owns the window -- and two budgets of 45 minutes each is not a
    /// browser limited to 45 minutes. It is two clocks that run together, and
    /// whoever writes only one of them leaves half the browser with no limit at
    /// all and no way to see that from the file.
    ///
    /// It is spent once per tick however many of its names are open, which is
    /// the same rule as before: `anyLiveScopeMatches` asks whether *anything*
    /// matches, and 21 processes in one Chromium never spent 21 seconds either.
    ///
    /// Written as a plain string when there is one of them, and only then as an
    /// array. That is docs/design.md §4's discipline for `kind`, `presence` and
    /// `sites`: a file that already says what it means is not rewritten to say
    /// it differently.
    QStringList match;
    /// Whether `match` is a list of app scope ids or of registrable domains.
    ///
    /// The whole of what a site budget adds to the model. Everything else about
    /// it -- the daily limit, the grants, `warnAt`, `grace`, the notification,
    /// the ledger remembering what was already said -- is the machinery an app
    /// budget already had, unchanged and unbranched. What differs is the two
    /// ends: which observation spends it (a live scope, or the site in the front
    /// tab crossed with presence) and what happens when it runs out.
    Selects selects = Selects::App;
    /// Zero or less is a budget with no limit: it counts, and it never runs out.
    /// That is the honest reading of a missing `dailyMinutes`, and it is worth
    /// having -- an app somebody wants a number for at the end of the day but
    /// no rule about is exactly this.
    int dailyMinutes = 0;
    OnExhausted onExhausted = OnExhausted::Warn;

    bool hasLimit() const { return dailyMinutes > 0; }
    bool isSite() const { return selects == Selects::Site; }
    /// The budget whose selector is everything -- docs/design.md §2's whole
    /// argument for there being no separate idea of "the user's time". A list
    /// containing `*` is that budget whatever else is beside it, because `*`
    /// already matches everything the other names would.
    bool isSession() const { return match.contains(QStringLiteral("*")); }
};

/// Whether an action is one this kind of budget can carry out.
///
/// A site is not a cgroup and an app is not a domain, so `close` and `logout`
/// mean nothing about a site and `block` means nothing about an app. Checked
/// where the file is read, rather than left for the engine to work around,
/// because a profile that names an action nobody will perform is a rule an
/// operator wrote and can still read back and that silently does not happen --
/// the failure docs/design.md §11 refuses for web precedence, refused here for
/// the same reason. `warn` fits both: it is the observing stage of either.
bool actionFits(Selects selects, OnExhausted action);

struct Profile {
    QString user;
    QString displayName;
    bool enabled = true;
    /// False is observation: count and report, close and log out nothing. The
    /// default for a profile that has just been written, because seeing a day of
    /// the report before switching the teeth on is what the lan house always
    /// did (docs/design.md §5).
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
    /// The sites, docs/design.md §11. Written into the file only when it says
    /// something, so a profile that was never given a web rule looks exactly
    /// like one whose last web rule was taken back.
    Web web;
    /// Explicit opt-in to exclusive daily portions, bound to one authority and
    /// logical machine. Empty retains standalone behavior.
    QJsonObject allocation;

    /// The first rule that names `scopeId` wins; with none, the profile default
    /// decides. First and not last because the rules are read in the order the
    /// operator wrote them, and the first line about a program is the one they
    /// would point at when asked why it is allowed.
    Verdict verdictFor(const QString &scopeId) const;

    /// The same, for a scope whose executable was read. See `selectorMatches`
    /// above for what the second name is allowed to do and what it is not.
    Verdict verdictFor(const QString &scopeId, const QString &exePath) const;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Profile *out, QString *error);
};

/// The whole of /etc/omahouse/profiles.json, docs/design.md §4.
QJsonObject profilesToJson(const QVector<Profile> &profiles);
bool profilesFromJson(const QJsonObject &root, QVector<Profile> *out, QString *error);

bool readProfiles(const QString &path, QVector<Profile> *out, QString *error,
                  bool *missing = nullptr);
bool writeProfiles(const QString &path, const QVector<Profile> &profiles, QString *error);

} // namespace omahouse
