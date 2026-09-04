#include "House.h"

#include "AppScope.h"
#include "Catalog.h"
#include "Duration.h"
#include "Ledger.h"
#include "Paths.h"
#include "Proc.h"
#include "Profile.h"
#include "Users.h"

#include <QDate>
#include <QDir>
#include <QFileInfo>
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

QString spellExhausted(OnExhausted action)
{
    switch (action) {
    case OnExhausted::Close:
        return QStringLiteral("closes");
    case OnExhausted::Logout:
        return QStringLiteral("ends the session");
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

const Budget *budgetFor(const Profile &profile, const QString &id)
{
    for (const Budget &budget : profile.budgets) {
        if (budget.match == id)
            return &budget;
    }
    return nullptr;
}

/// The clock of one budget, as both numbers and words.
///
/// `allowanceSeconds` is the limit plus whatever an operator handed over today,
/// because a grant that did not show up in the limit would read on this window
/// as though it had gone nowhere -- and `spec.md` §7 keeps `grant` precisely so
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
        if (budget.match == QLatin1String("*"))
            continue;
        if (!ids.contains(budget.match))
            ids << budget.match;
    }

    QVariantList rows;
    for (const QString &id : std::as_const(ids)) {
        const Budget *budget = budgetFor(profile, id);
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
        // Said out loud rather than corrected. spec.md §5: the id is what the
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
        QVariantMap row = clockOf(budget, ledger, anythingMatching(scopes, budget.match));
        row.insert(QStringLiteral("kind"), QStringLiteral("budget"));
        row.insert(QStringLiteral("id"), budget.id);
        row.insert(QStringLiteral("match"), budget.match);
        row.insert(QStringLiteral("session"), session);
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
    // spec.md §1 makes the operator whoever is in wheel, and `profile add`
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
    const QDate today = QDate::currentDate();

    QVariantList people;
    QVariantMap programs;
    QVariantMap todays;
    QVariantMap catalogs;

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

        Ledger ledger;
        QString ledgerError;
        bool noLedger = false;
        if (!readLedger(paths::ledgerFile(profile.user, today), &ledger, &ledgerError, &noLedger)
            && !noLedger && error.isEmpty()) {
            error = QStringLiteral("%1's day: %2").arg(profile.user, ledgerError);
        }

        const QVariantList programRows = programsOf(profile, ledger, scopes, byId);
        const QVariantList todayRows = todayOf(profile, ledger, scopes, byId);
        programs.insert(profile.user, programRows);
        todays.insert(profile.user, todayRows);
        catalogs.insert(profile.user, catalogOf(profile, scopes, m_installed));

        QVariantMap person;
        person.insert(QStringLiteral("user"), profile.user);
        person.insert(QStringLiteral("name"), profile.displayName.isEmpty() ? profile.user
                                                                            : profile.displayName);
        person.insert(QStringLiteral("displayName"), profile.displayName);
        person.insert(QStringLiteral("enabled"), profile.enabled);
        person.insert(QStringLiteral("enforce"), profile.enforce);
        // The word the file carries is `deny`, and it is never printed. The
        // window says what it does to programs: spec.md §8 asks for programs and
        // minutes, not a form of verdicts.
        person.insert(QStringLiteral("allowlist"), profile.defaultVerdict == Verdict::Deny);
        person.insert(QStringLiteral("policy"),
                      profile.defaultVerdict == Verdict::Deny
                          ? QStringLiteral("only the listed programs run")
                          : QStringLiteral("everything runs but the listed"));
        person.insert(QStringLiteral("teeth"),
                      profile.enforce ? QStringLiteral("closing and logging out")
                                      : QStringLiteral("watching only"));
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
