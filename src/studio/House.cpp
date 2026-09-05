#include "House.h"

#include "AppScope.h"
#include "Catalog.h"
#include "Duration.h"
#include "Ledger.h"
#include "Paths.h"
#include "Policy.h"
#include "Presence.h"
#include "Proc.h"
#include "Profile.h"
#include "Users.h"
#include "WebPolicy.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace omahouse {
namespace {

/// How long, in the words the CLI uses. `durationFromMinutes` is in the core
/// exactly so that `45m` on a command line and `45m` on this window are the same
/// string produced by the same code.
///
/// Under a minute is `<1m` and not `0m`: a balance that reads zero while the
/// program is still open is the window saying the time is gone when it is not,
/// and forty seconds is not nothing to somebody watching it run out.
QString spellSeconds(int seconds)
{
    if (seconds <= 0)
        return QStringLiteral("0m");
    if (seconds < 60)
        return QStringLiteral("<1m");
    return durationFromMinutes(seconds / 60);
}

/// What running out does, in the words the window prints.
///
/// `Block` is here rather than falling through to the default, and the day it
/// was not was a real defect: a site budget drawn on the today view read `only
/// warns when it runs out` while the browser was in fact refusing to open the
/// site. A switch over an enumeration whose fourth value lands in the default
/// arm is a sentence that is wrong in exactly the case somebody is looking it
/// up for.
QString spellExhausted(OnExhausted action)
{
    switch (action) {
    case OnExhausted::Close:
        return QStringLiteral("closes");
    case OnExhausted::Logout:
        return QStringLiteral("ends the session");
    case OnExhausted::Block:
        return QStringLiteral("stops opening");
    case OnExhausted::Warn:
        break;
    }
    return QStringLiteral("only warns");
}

struct Live {
    int pids = 0;
    QString exe;
    int exeCount = 0;
};

/// What is open under `id` right now, summed over every scope carrying it.
///
/// The executable reported is the one from the scope that has most of the
/// processes, because two scopes of the same program are the ordinary case --
/// two terminal windows -- and averaging two paths is not a thing that can be
/// done.
Live liveFor(const QVector<AppScope> &scopes, const QString &id)
{
    Live live;
    int best = -1;
    for (const AppScope &scope : scopes) {
        if (scope.id != id || !scope.isLive())
            continue;
        live.pids += scope.pidCount;
        if (scope.pidCount > best && !scope.dominantExe.isEmpty()) {
            best = scope.pidCount;
            live.exe = scope.dominantExe;
            live.exeCount = scope.dominantExeCount;
        }
    }
    return live;
}

bool anythingMatching(const QVector<AppScope> &scopes, const QString &selector)
{
    for (const AppScope &scope : scopes) {
        if (scope.isLive() && selectorMatches(selector, scope.id))
            return true;
    }
    return false;
}

/// The budget about `id` in that namespace, or none.
///
/// `selects` is not optional and never guessed. `org.freedesktop.Platform` is a
/// scope id with dots in it and `youtube.com` is a domain with dots in it, and
/// Profile.h says in so many words that there is no shape that tells them apart
/// — so a lookup by name alone would eventually hand the programs view a budget
/// that is about a site.
const Budget *budgetFor(const Profile &profile, const QString &id, Selects selects)
{
    for (const Budget &budget : profile.budgets) {
        if (budget.match == id && budget.selects == selects)
            return &budget;
    }
    return nullptr;
}

/// The clock of one budget, as both numbers and words.
///
/// `allowanceSeconds` is the limit plus whatever an operator handed over today,
/// because a grant that did not show up in the limit would read on this window
/// as though it had gone nowhere -- and `docs/design.md` §7 keeps `grant` precisely so
/// that an operator can add ten minutes and see it land.
QVariantMap clockOf(const Budget &budget, const Ledger &ledger, bool running)
{
    const int granted = ledger.grantedSeconds(budget.id);
    const int spent = ledger.secondsFor(budget.id);
    const int allowance = budget.hasLimit() ? budget.dailyMinutes * 60 + granted : 0;
    const int left = budget.hasLimit() ? std::max(0, allowance - spent) : 0;

    QVariantMap clock;
    clock.insert(QStringLiteral("limited"), budget.hasLimit());
    clock.insert(QStringLiteral("limitSeconds"), budget.dailyMinutes * 60);
    clock.insert(QStringLiteral("grantedSeconds"), granted);
    clock.insert(QStringLiteral("allowanceSeconds"), allowance);
    clock.insert(QStringLiteral("spentSeconds"), spent);
    clock.insert(QStringLiteral("leftSeconds"), left);
    clock.insert(QStringLiteral("limit"),
                 budget.hasLimit() ? spellSeconds(allowance) : QString());
    // The number that is written in the profile, as opposed to what today's
    // grants have made of it. The prompt that changes the limit opens with this
    // in the field: opening with the allowance would fold a grant somebody made
    // for one day into the rule for every day.
    clock.insert(QStringLiteral("daily"),
                 budget.hasLimit() ? durationFromMinutes(budget.dailyMinutes) : QString());
    clock.insert(QStringLiteral("granted"), granted > 0 ? spellSeconds(granted) : QString());
    clock.insert(QStringLiteral("spent"), spellSeconds(spent));
    clock.insert(QStringLiteral("left"), budget.hasLimit() ? spellSeconds(left) : QString());
    clock.insert(QStringLiteral("exhausted"), budget.hasLimit() && left == 0);
    clock.insert(QStringLiteral("running"), running);
    clock.insert(QStringLiteral("onExhausted"), onExhaustedName(budget.onExhausted));
    clock.insert(QStringLiteral("ending"), spellExhausted(budget.onExhausted));
    return clock;
}

QVariantMap noClock(int spentSeconds)
{
    QVariantMap clock;
    clock.insert(QStringLiteral("limited"), false);
    clock.insert(QStringLiteral("limitSeconds"), 0);
    clock.insert(QStringLiteral("grantedSeconds"), 0);
    clock.insert(QStringLiteral("allowanceSeconds"), 0);
    clock.insert(QStringLiteral("spentSeconds"), spentSeconds);
    clock.insert(QStringLiteral("leftSeconds"), 0);
    clock.insert(QStringLiteral("limit"), QString());
    clock.insert(QStringLiteral("daily"), QString());
    clock.insert(QStringLiteral("granted"), QString());
    clock.insert(QStringLiteral("spent"), spellSeconds(spentSeconds));
    clock.insert(QStringLiteral("left"), QString());
    clock.insert(QStringLiteral("exhausted"), false);
    clock.insert(QStringLiteral("running"), false);
    clock.insert(QStringLiteral("onExhausted"), QString());
    clock.insert(QStringLiteral("ending"), QString());
    return clock;
}

QVariantList programsOf(const Profile &profile, const Ledger &ledger,
                        const QVector<AppScope> &scopes,
                        const QHash<QString, DesktopApp> &catalogue)
{
    // Every program the profile has an opinion about: one named by a rule, and
    // one named by a budget without a rule. The second is legitimate -- a
    // program somebody wants a number for at the end of the day and no rule
    // about -- and leaving it off this list would be the window hiding a limit
    // that is being enforced.
    QStringList ids;
    for (const Rule &rule : profile.rules) {
        if (!ids.contains(rule.match))
            ids << rule.match;
    }
    for (const Budget &budget : profile.budgets) {
        // A site budget is not a program, and it used to land here: `youtube.com`
        // drew a row on the programs view with a verdict taken from the app
        // rules, an `x` that would have written `omahouse deny <user>
        // youtube.com`, and no way to tell it from a flatpak with dots in its
        // name. It belongs on the sites view, which is where `sitesOf` puts it.
        if (budget.match == QLatin1String("*") || budget.isSite())
            continue;
        if (!ids.contains(budget.match))
            ids << budget.match;
    }

    QVariantList rows;
    for (const QString &id : std::as_const(ids)) {
        const Budget *budget = budgetFor(profile, id, Selects::App);
        const Live live = liveFor(scopes, id);

        QVariantMap row = budget ? clockOf(*budget, ledger, live.pids > 0)
                                 : noClock(0);
        row.insert(QStringLiteral("kind"), QStringLiteral("program"));
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("budgetId"), budget ? budget->id : QString());
        row.insert(QStringLiteral("hasBudget"), budget != nullptr);
        row.insert(QStringLiteral("released"), profile.verdictFor(id) == Verdict::Allow);
        row.insert(QStringLiteral("name"), catalogue.value(id).name);
        row.insert(QStringLiteral("pids"), live.pids);
        row.insert(QStringLiteral("running"), live.pids > 0);
        row.insert(QStringLiteral("exe"), live.exe);
        row.insert(QStringLiteral("exeCount"), live.exeCount);
        // Said out loud rather than corrected. docs/design.md §5: the id is what the
        // rule matches and the executable is what is really in there, and they
        // fail in opposite places -- so an operator about to release
        // `gtk-launch` is shown the VS Code inside it, and decides.
        row.insert(QStringLiteral("disagrees"),
                   !live.exe.isEmpty() && !exeCorroboratesId(id, live.exe));
        rows.append(row);
    }
    return rows;
}

QVariantList todayOf(const Profile &profile, const Ledger &ledger,
                     const QVector<AppScope> &scopes,
                     const QHash<QString, DesktopApp> &catalogue)
{
    QVector<Budget> ordered = profile.budgets;
    // The session first, whatever order it was written in: it is the answer to
    // "how much is left today", and the rest of the list is the breakdown.
    std::stable_sort(ordered.begin(), ordered.end(), [](const Budget &a, const Budget &b) {
        const bool sessionA = a.match == QLatin1String("*");
        const bool sessionB = b.match == QLatin1String("*");
        return sessionA != sessionB ? sessionA : false;
    });

    QVariantList rows;
    for (const Budget &budget : std::as_const(ordered)) {
        const bool session = budget.match == QLatin1String("*");
        // A site budget is never `running now`. What would make it so is the
        // domain in the front tab, and that is `/run/user/<uid>/omahouse/focus`,
        // 0600 in the fiscalised account's own runtime directory — this window
        // is neither that account nor root, so it cannot look, and it says
        // nothing rather than answering from the app scopes, where a name that
        // happened to match would be a coincidence and not a browser.
        QVariantMap row = clockOf(budget, ledger,
                                  !budget.isSite() && anythingMatching(scopes, budget.match));
        row.insert(QStringLiteral("kind"), QStringLiteral("budget"));
        row.insert(QStringLiteral("id"), budget.id);
        row.insert(QStringLiteral("match"), budget.match);
        row.insert(QStringLiteral("session"), session);
        // Which namespace the id is in, carried on the row because the verb that
        // changes it differs: `limit --budget` for an app and `limit --site` for
        // a domain, and the CLI refuses the wrong one rather than guessing.
        row.insert(QStringLiteral("site"), budget.isSite());
        const QString named = catalogue.value(budget.match).name;
        row.insert(QStringLiteral("name"),
                   session ? QStringLiteral("the whole day")
                           : (named.isEmpty() ? budget.match : named));
        rows.append(row);
    }

    // The day's log under the balances, in one column, so the keyboard walks the
    // whole view with j and k rather than needing a key to get into a panel.
    struct Line {
        QDateTime at;
        QString kind;
        QString text;
    };
    QVector<Line> lines;
    for (const Grant &grant : ledger.grants) {
        lines.append({grant.at, QStringLiteral("grant"),
                      QStringLiteral("%1 handed over %2 of %3")
                          .arg(grant.by.isEmpty() ? QStringLiteral("somebody") : grant.by,
                               durationFromMinutes(grant.minutes), grant.budget)});
    }
    for (const Event &event : ledger.events) {
        QString text;
        switch (event.kind) {
        case EventKind::Warn:
            text = event.minutes > 0
                ? QStringLiteral("%1: %2 left, and they were told")
                      .arg(event.budget, durationFromMinutes(event.minutes))
                : QStringLiteral("%1: out of time, and they were told").arg(event.budget);
            break;
        case EventKind::Exhausted:
            text = QStringLiteral("%1: ran out").arg(event.budget);
            break;
        case EventKind::Denied:
            text = QStringLiteral("%1: not on the list, and was closed").arg(event.scope);
            break;
        }
        lines.append({event.at, eventKindName(event.kind), text});
    }
    std::sort(lines.begin(), lines.end(),
              [](const Line &a, const Line &b) { return a.at > b.at; });

    for (const Line &line : std::as_const(lines)) {
        QVariantMap row;
        row.insert(QStringLiteral("kind"), QStringLiteral("event"));
        row.insert(QStringLiteral("event"), line.kind);
        row.insert(QStringLiteral("id"), line.kind);
        row.insert(QStringLiteral("at"), line.at.toString(QStringLiteral("HH:mm")));
        row.insert(QStringLiteral("text"), line.text);
        rows.append(row);
    }
    return rows;
}

// -- the sites ---------------------------------------------------------------
//
// docs/design.md §11 for the rules, §5.2 for the minutes, §5.3 for the limit
// that acts. Three things the CLI has done for a while and this window did not
// know existed, which broke the promise §8 makes and `everyCommandIsBothAKeyAndAChip`
// proves: an action that lives only in the CLI escapes that proof, because the
// window never learns there is anything to prove about it.
//
// A view of its own, and not rows folded into the programs view. The CLI made
// the same call for the same reason and says so where `web` is implemented:
// taking a program off somebody's list and changing what every browser on the
// machine will open are different enough acts that they should not share a
// word — and here they cannot share a list either, because every command on the
// row differs. `x` on a program writes `omahouse deny`; the nearest thing on a
// site is `omahouse web allow`, which is not a removal at all.

/// Whether the machine's one policy file stops `domain` opening right now.
///
/// Asked of the composed policy and never of one profile, because the composed
/// one is what the browser reads. `*` in the blocklist is `--only-listed` from
/// somebody, and then the allowlist is the whole of what still opens.
bool machineBlocks(const ChromiumPolicy &policy, const QString &domain)
{
    if (policy.blocklist.contains(domain))
        return true;
    if (!policy.blocklist.contains(QStringLiteral("*")))
        return false;
    return !policy.allowlist.contains(domain);
}

/// The sites this profile has anything to say or to count about.
///
/// Three sources, in the order somebody reads them: the rules the operator
/// wrote, in the order they wrote them; the domains a limit was set on; and the
/// domains today's ledger counted minutes against, longest first. The third is
/// why this view is worth having even on a profile with no web rules at all —
/// it is the day's browsing, which until now had no screen in this window.
///
/// Nothing here is a guess about what the browser did. A blocked site produces
/// no event of any kind: policy blocking happens inside Chromium and reports
/// nothing out, and docs/design.md §11 says so plainly. So there is no `tried 4
/// times` on any row and there cannot be one.
QVariantList sitesOf(const Profile &profile, const Ledger &ledger,
                     const ChromiumPolicy &machine, const QSet<QString> &outOfTime)
{
    const QString everything = QStringLiteral("*");

    QStringList domains;
    const auto mention = [&domains, &everything](const QString &domain) {
        if (domain.isEmpty() || domain == everything || domains.contains(domain))
            return;
        domains << domain;
    };
    for (const Rule &rule : profile.web.rules)
        mention(rule.match);
    for (const Budget &budget : profile.budgets) {
        if (budget.isSite())
            mention(budget.match);
    }
    QVector<QPair<int, QString>> counted;
    for (auto it = ledger.sites.constBegin(); it != ledger.sites.constEnd(); ++it)
        counted.append({it.value(), it.key()});
    std::sort(counted.begin(), counted.end(), [](const auto &a, const auto &b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    for (const auto &one : std::as_const(counted))
        mention(one.second);

    QVariantList rows;
    for (const QString &domain : std::as_const(domains)) {
        const Budget *budget = budgetFor(profile, domain, Selects::Site);
        const int onIt = ledger.siteSecondsFor(domain);

        QVariantMap row = budget ? clockOf(*budget, ledger, false) : noClock(onIt);
        row.insert(QStringLiteral("kind"), QStringLiteral("site"));
        row.insert(QStringLiteral("id"), domain);
        row.insert(QStringLiteral("name"), domain);
        row.insert(QStringLiteral("budgetId"), budget ? budget->id : QString());
        row.insert(QStringLiteral("hasBudget"), budget != nullptr);
        // The minutes in front of that tab today, which exist whether or not
        // anything limits them: docs/design.md §5.2 counts every domain that was
        // ever in front, and the observing stage is the point of it.
        row.insert(QStringLiteral("todaySeconds"), onIt);
        row.insert(QStringLiteral("today"), onIt > 0 ? spellSeconds(onIt) : QString());

        bool named = false;
        Verdict wanted = profile.web.defaultVerdict;
        for (const Rule &rule : profile.web.rules) {
            if (rule.match == domain) {
                named = true;
                wanted = rule.verdict;
                break;
            }
        }
        const bool spent = outOfTime.contains(domain);
        const bool blocked = machineBlocks(machine, domain);
        const bool asked = profile.web.saysAnything() && wanted == Verdict::Deny;

        row.insert(QStringLiteral("named"), named);
        row.insert(QStringLiteral("asked"), asked);
        row.insert(QStringLiteral("blocked"), blocked);
        row.insert(QStringLiteral("outOfTime"), spent);
        // Blocked, but not by anything on this profile. The composition rule of
        // WebPolicy.h seen from the row it lands on: the most restrictive wins,
        // and there is no precedence, so an `open` here can be overruled — and
        // being overruled silently is the one thing that would make the rule
        // dishonest.
        row.insert(QStringLiteral("overruled"), blocked && !asked && !spent);
        row.insert(QStringLiteral("state"),
                   blocked ? QStringLiteral("does not open") : QStringLiteral("opens"));

        QString note;
        if (spent) {
            note = QStringLiteral("out of time today — it opens again at the turn of the day, "
                                  "or when more time is handed over");
        } else if (blocked && !asked) {
            note = QStringLiteral("another profile blocks it, and the most restrictive of the "
                                  "two is what the machine does");
        } else if (asked) {
            // Before the clock and not after it. A site this profile blocks
            // outright and also has a limit on does not open at all, and
            // `stops opening when the time is up` would be a sentence about
            // minutes that are never going to be spent.
            note = QStringLiteral("blocked here, and so are its subdomains");
        } else if (budget && budget->hasLimit()) {
            note = QStringLiteral("%1 when the time is up")
                       .arg(spellExhausted(budget->onExhausted));
        } else if (named && machine.blocklist.isEmpty()) {
            // The trap `.temp/spike-extension.md` §7 measured one policy over,
            // and the sentence `omahouse web allow` prints for it: an allowlist
            // with nothing blocked beside it is inert.
            note = QStringLiteral("on the allowed list — which blocks nothing on its own, "
                                  "because nothing is blocked yet");
        } else if (named) {
            note = QStringLiteral("allowed through what is blocked");
        } else if (onIt > 0) {
            note = QStringLiteral("no rule and no clock — the minutes are counted and nothing "
                                  "else");
        } else {
            note = QStringLiteral("no rule and no clock");
        }
        row.insert(QStringLiteral("note"), note);
        rows.append(row);
    }
    return rows;
}

/// The day's presence, in the words `omahouse status` uses: `1h10m using,
/// 40m screen-off`.
///
/// Read out of the ledger and never measured here. docs/design.md §5.1 puts
/// presence in `status`, in the journal line and in the day's ledger, and the
/// ledger is the copy a window may have: it is 0644 and already on disk, where
/// the live answer is a `loginctl` fork and a walk of `/sys/class/drm` twice a
/// second in a program that is only looking.
///
/// It is on the sites view because that is the one number it explains. An app is
/// billed for running, screen or no screen — §5.1 says so and this window must
/// not imply otherwise — but a site is billed only where the browser and the
/// screen agree, so `25m on youtube.com` on an afternoon somebody remembers as
/// longer is answered by `40m screen-off` on the line above it.
QString spellPresence(const Ledger &ledger)
{
    QStringList parts;
    for (auto it = ledger.presence.constBegin(); it != ledger.presence.constEnd(); ++it)
        parts.append(QStringLiteral("%1 %2").arg(spellSeconds(it.value()), it.key()));
    return parts.join(QStringLiteral(", "));
}

QVariantList catalogOf(const Profile &profile, const QVector<AppScope> &scopes,
                       const QVector<DesktopApp> &installed)
{
    QVariantList rows;
    QSet<QString> seen;

    const auto append = [&](const QString &id, const QString &name, const QString &exec) {
        if (id.isEmpty() || seen.contains(id))
            return;
        seen.insert(id);
        const Live live = liveFor(scopes, id);
        const QString evidence = live.exe.isEmpty() ? exec : live.exe;
        QVariantMap row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("name"), name.isEmpty() ? id : name);
        row.insert(QStringLiteral("exe"), evidence);
        row.insert(QStringLiteral("exeCount"), live.exeCount);
        row.insert(QStringLiteral("running"), live.pids > 0);
        row.insert(QStringLiteral("pids"), live.pids);
        row.insert(QStringLiteral("disagrees"),
                   !evidence.isEmpty() && !exeCorroboratesId(id, evidence));
        row.insert(QStringLiteral("listed"), profile.verdictFor(id) == Verdict::Allow);
        rows.append(row);
    };

    QHash<QString, DesktopApp> byId;
    for (const DesktopApp &app : installed)
        byId.insert(app.id, app);

    // What is open right now goes first, and it is the part of this list that is
    // not a guess: a scope exists, it has that id, and the operator can see what
    // is inside it. The `.desktop` entries under it are the programs that would
    // get one when they are next launched.
    QStringList runningIds;
    for (const AppScope &scope : scopes) {
        if (scope.isLive() && !runningIds.contains(scope.id))
            runningIds << scope.id;
    }
    std::sort(runningIds.begin(), runningIds.end());
    for (const QString &id : std::as_const(runningIds))
        append(id, byId.value(id).name, byId.value(id).exec);
    for (const DesktopApp &app : installed)
        append(app.id, app.name, app.exec);
    return rows;
}

/// Whose machine this window is reading: the account that ran it, or the one
/// `$OMAHOUSE_AS` names.
///
/// The door exists for the pictures in docs/img. Two of the things this window
/// draws cannot be reached from one account: the subject face is what somebody
/// who is not in wheel sees, and the operator's own name is printed across the
/// header of every frame -- so a generator without this would need a second
/// account to run as, and would still write a different file on every machine
/// because the header says `howl` on one and `ana` on the next. `mise run shots`
/// reads as `root` for the operator face and as `nobody` for the subject one,
/// and gets the same bytes anywhere.
///
/// It grants nothing, and cannot. `House` only reads, and everything it reads is
/// world readable by design -- profiles.json is 0644 and the cgroup tree is
/// public, which is the whole reason the read path does not go through pkexec.
/// The write path never asks it who anybody is: `Admin` runs `pkexec omahouse`,
/// under the real uid, and polkit and the CLI's own refusals decide. So the most
/// this can do is draw a window with chips on it whose every press is refused,
/// which is a worse window and not a bigger privilege.
QString readingAs()
{
    const QString named = qEnvironmentVariable("OMAHOUSE_AS");
    return named.isEmpty() ? currentUser() : named;
}

} // namespace

House::House(QObject *parent)
    : QObject(parent)
    , m_user(readingAs())
{
    refresh();

    // Two seconds, which is the daemon's own tick: a balance drawn faster than
    // it is written would only be redrawing the same number.
    m_tick.setInterval(2000);
    connect(&m_tick, &QTimer::timeout, this, &House::refresh);
    m_tick.start();

    // And the file itself, so an `omahouse allow` typed in a terminal shows up
    // here at once rather than up to two seconds later. Both signals re-take the
    // watches first: profiles.json is written by tmp + rename, and a watch
    // follows the old file out.
    const auto rearm = [this] {
        const QString file = paths::profilesFile();
        if (!m_watcher.files().isEmpty())
            m_watcher.removePaths(m_watcher.files());
        if (QFile::exists(file))
            m_watcher.addPath(file);
        refresh();
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, rearm);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, rearm);
    const QString directory = QFileInfo(paths::profilesFile()).absolutePath();
    if (QDir(directory).exists())
        m_watcher.addPath(directory);
    if (QFile::exists(paths::profilesFile()))
        m_watcher.addPath(paths::profilesFile());

    // A program installed while the window is open shows up in the picker,
    // without the picker having been the reason to read the whole of
    // /usr/share/applications on every tick.
    const QStringList desktops = applicationDirs();
    for (const QString &directory : desktops) {
        if (QDir(directory).exists())
            m_desktops.addPath(directory);
    }
    connect(&m_desktops, &QFileSystemWatcher::directoryChanged, this, [this] {
        m_installedStale = true;
        refresh();
    });
}

QString House::reach() const
{
    // Joined with spaces and wrapped by the label, where the CLI prints one
    // fragment per line: `webPolicyReach` is broken for a terminal that wraps by
    // hand, and this is the one caller that does not.
    return webPolicyReach().join(QLatin1Char(' '));
}

void House::reload()
{
    // The programs too. The window calls this the moment a write comes back,
    // and the readiest reason for somebody to reach for the reload is that they
    // have just installed the thing they came here to release.
    m_installedStale = true;
    refresh();
}

void House::refresh()
{
    QString error;
    bool missing = false;
    QVector<Profile> profiles;
    if (!readProfiles(paths::profilesFile(), &profiles, &error, &missing) && !missing)
        error = QStringLiteral("profiles: %1").arg(error);
    else
        error.clear();

    // Nobody chooses the face on screen, and there is no switch to flip.
    //
    // docs/design.md §1 makes the operator whoever is in wheel, and `profile add`
    // refuses to write a profile for one of them -- so the two answers cannot
    // both be true for the same account by any route this program offers. Wheel
    // is asked first anyway, because if a machine ever did have both, the reply
    // that lets somebody act is the one they came for.
    QString why;
    const bool administers = isAdministrator(m_user, &why);
    QString face = administers ? QStringLiteral("operator") : QStringLiteral("subject");
    QString reason = administers ? why : QStringLiteral("not under rules");

    QVector<Profile> visible;
    for (const Profile &profile : std::as_const(profiles)) {
        if (administers || profile.user == m_user)
            visible.append(profile);
    }
    for (const Profile &profile : std::as_const(visible)) {
        if (profile.user == m_user)
            reason = QStringLiteral("under rules");
    }

    if (m_installedStale) {
        m_installed = installedApps();
        m_installedStale = false;
    }
    QHash<QString, DesktopApp> byId;
    for (const DesktopApp &app : std::as_const(m_installed))
        byId.insert(app.id, app);

    const Proc proc;
    const QDateTime when = QDateTime::currentDateTime();
    const QDate today = when.date();

    // The day of every profile, and not only of the visible ones.
    //
    // What the browser really does about a site is the composition of *all* the
    // profiles — WebPolicy.h, the most restrictive wins, no precedence — and the
    // sites that have run out today are worked out afresh from each day's
    // ledger, exactly as the daemon does it. A subject reading their own sites
    // view has to be told the truth about what will open, and that answer is
    // not derivable from their own profile alone. Nothing of anybody else's is
    // published by it: the rows are this profile's domains, and the sentence a
    // row carries names no other account.
    QHash<QString, Ledger> days;
    for (const Profile &profile : std::as_const(profiles)) {
        Ledger ledger;
        QString ledgerError;
        bool noLedger = false;
        const bool read = readLedger(paths::ledgerFile(profile.user, today), &ledger,
                                     &ledgerError, &noLedger);
        // A day that could not be read is only worth saying out loud about
        // somebody this face is allowed to see. Naming another household
        // member's file at somebody who cannot see their profile would be the
        // window publishing the one thing `visible` exists to withhold.
        if (!read && !noLedger && error.isEmpty()
            && (administers || profile.user == m_user)) {
            error = QStringLiteral("%1's day: %2").arg(profile.user, ledgerError);
        }
        days.insert(profile.user, ledger);
    }

    QStringList outOfTime;
    for (const Profile &profile : std::as_const(profiles)) {
        if (!profile.enabled)
            continue;
        for (const Decision &decision :
             evaluate(profile, {}, days.value(profile.user), when, 0).decisions) {
            if (decision.kind == Decision::Kind::Block && !decision.site.isEmpty()
                && !outOfTime.contains(decision.site)) {
                outOfTime.append(decision.site);
            }
        }
    }
    outOfTime.sort();
    const ChromiumPolicy machine = chromiumPolicyFor(profiles, outOfTime);
    const QSet<QString> spent(outOfTime.cbegin(), outOfTime.cend());

    QVariantList people;
    QVariantMap programs;
    QVariantMap todays;
    QVariantMap catalogs;
    QVariantMap sites;

    for (const Profile &profile : std::as_const(visible)) {
        uid_t uid = 0;
        const bool exists = uidForUser(profile.user, &uid);

        QVector<AppScope> scopes;
        int blind = 0;
        int unnamed = 0;
        bool online = false;
        if (exists) {
            online = proc.hasSession(uid);
            scopes = proc.scopesFor(uid);
            proc.resolveDominantExe(&scopes);
            for (const AppScope &scope : std::as_const(scopes)) {
                if (scope.id.isEmpty() && scope.pidCount > 0)
                    unnamed += scope.pidCount;
            }
            blind = proc.sessionSliceProcesses(uid);
        }

        const Ledger ledger = days.value(profile.user);

        const QVariantList programRows = programsOf(profile, ledger, scopes, byId);
        const QVariantList todayRows = todayOf(profile, ledger, scopes, byId);
        const QVariantList siteRows = sitesOf(profile, ledger, machine, spent);
        programs.insert(profile.user, programRows);
        todays.insert(profile.user, todayRows);
        sites.insert(profile.user, siteRows);
        catalogs.insert(profile.user, catalogOf(profile, scopes, m_installed));

        QVariantMap person;
        person.insert(QStringLiteral("user"), profile.user);
        person.insert(QStringLiteral("name"), profile.displayName.isEmpty() ? profile.user
                                                                            : profile.displayName);
        person.insert(QStringLiteral("displayName"), profile.displayName);
        person.insert(QStringLiteral("enabled"), profile.enabled);
        person.insert(QStringLiteral("enforce"), profile.enforce);
        // The word the file carries is `deny`, and it is never printed. The
        // window says what it does to programs: docs/design.md §8 asks for programs and
        // minutes, not a form of verdicts.
        person.insert(QStringLiteral("allowlist"), profile.defaultVerdict == Verdict::Deny);
        person.insert(QStringLiteral("policy"),
                      profile.defaultVerdict == Verdict::Deny
                          ? QStringLiteral("only the listed programs run")
                          : QStringLiteral("everything runs but the listed"));
        person.insert(QStringLiteral("teeth"),
                      profile.enforce ? QStringLiteral("closing and logging out")
                                      : QStringLiteral("watching only"));
        // The sites, in the same shape the two lines above give the programs:
        // what the rules do, said as what happens to sites rather than as a
        // verdict and a default — docs/design.md §8, the same rule that turns
        // `default: deny` into "only the listed programs run".
        person.insert(QStringLiteral("onlyListedSites"),
                      profile.web.defaultVerdict == Verdict::Deny);
        person.insert(QStringLiteral("sitePolicy"),
                      profile.web.defaultVerdict == Verdict::Deny
                          ? QStringLiteral("only the listed sites open")
                          : QStringLiteral("every site opens except the blocked ones"));
        // Three states and not two. Not said at all is not the same as said
        // `allow`: Profile.h refuses to write `IncognitoModeAvailability: 0`,
        // because omahouse being *more* permissive than it was asked to be is
        // the one direction this project never takes by default. The window has
        // to be able to draw the difference or the chip would lie about what it
        // had done.
        person.insert(QStringLiteral("incognitoStated"), profile.web.incognitoStated);
        person.insert(QStringLiteral("incognitoDenied"),
                      profile.web.incognitoStated && profile.web.incognito == Verdict::Deny);
        person.insert(QStringLiteral("incognito"),
                      !profile.web.incognitoStated
                          ? QStringLiteral("nothing said about incognito")
                          : profile.web.incognito == Verdict::Deny
                              ? QStringLiteral("incognito windows do not open")
                              : QStringLiteral("incognito windows open"));
        person.insert(QStringLiteral("siteCount"), siteRows.size());
        // The day's presence, out of the ledger, for the line the sites view
        // draws over its numbers — docs/design.md §5.1.
        person.insert(QStringLiteral("presenceToday"), spellPresence(ledger));

        person.insert(QStringLiteral("exists"), exists);
        person.insert(QStringLiteral("online"), online);
        person.insert(QStringLiteral("programCount"), programRows.size());
        person.insert(QStringLiteral("budgetCount"), profile.budgets.size());
        person.insert(QStringLiteral("blindProcesses"), blind);
        person.insert(QStringLiteral("unnamedProcesses"), unnamed);

        // The session budget, lifted out, because it is the one number somebody
        // opening this window is looking for.
        QVariantMap session;
        for (const QVariant &row : todayRows) {
            const QVariantMap map = row.toMap();
            if (map.value(QStringLiteral("session")).toBool()) {
                session = map;
                break;
            }
        }
        person.insert(QStringLiteral("hasSessionBudget"), !session.isEmpty());
        person.insert(QStringLiteral("session"), session);
        people.append(person);
    }

    QVariantMap snapshot;
    snapshot.insert(QStringLiteral("people"), people);
    snapshot.insert(QStringLiteral("programs"), programs);
    snapshot.insert(QStringLiteral("sites"), sites);
    snapshot.insert(QStringLiteral("today"), todays);
    snapshot.insert(QStringLiteral("catalog"), catalogs);

    if (snapshot == m_snapshot && face == m_face && reason == m_faceReason && error == m_error)
        return;
    m_snapshot = snapshot;
    m_face = face;
    m_faceReason = reason;
    m_error = error;
    emit changed();
}

} // namespace omahouse
