#include "Ledger.h"
#include "Paths.h"
#include "Proc.h"
#include "Profile.h"
#include "Users.h"
#include "Version.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <algorithm>

#ifndef OMAHOUSE_VERSION
#error "OMAHOUSE_VERSION is not defined -- qmake/version.pri was not included."
#endif

using namespace omahouse;

namespace {

// 0 the run did what was asked; 1 a usage error, or an answer the run could not
// finish; 2 something it was asked about is not there.
//
// The split between 1 and 2 is the one `omafiles` draws: a file that is there
// and will not parse is a failure, and an account or a profile that does not
// exist is an answer. A script that asks `omahouse profile show julia` to find
// out whether julia has a profile has to be able to tell those apart.
constexpr int kOk = 0;
constexpr int kUsage = 1;
constexpr int kMissing = 2;

/// A budget with no limit, an amount there is no number for. One glyph, so the
/// column stays a column.
const QString kNothing = QStringLiteral("—");

struct Globals {
    bool json = false;
    bool help = false;
    bool version = false;
};

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

void fail(const QString &message)
{
    err() << message << '\n';
}

/// Something true about the run that is not its answer: there is no
/// profiles.json yet, nothing has been counted today. stderr, so that a
/// `--json` run stays one document on stdout and a human still reads the note.
void note(const QString &message)
{
    err() << message << '\n';
}

void printJson(const QJsonValue &value)
{
    const QJsonDocument document =
        value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    out() << QString::fromUtf8(document.toJson(QJsonDocument::Compact)) << '\n';
}

bool envFlag(const char *name)
{
    const QByteArray value = qgetenv(name);
    return value == "1" || value.compare("true", Qt::CaseInsensitive) == 0
        || value.compare("yes", Qt::CaseInsensitive) == 0;
}

// -- how a number of seconds is said ----------------------------------------

/// `2h00m`, `45m`, `12s`. The minutes of an hour are padded, so a column of
/// them lines up on the `h` and reads as a duration rather than as an hour of
/// the day.
QString humanDuration(int seconds)
{
    if (seconds < 0)
        seconds = 0;
    if (seconds >= 3600) {
        return QStringLiteral("%1h%2m")
            .arg(seconds / 3600)
            .arg((seconds % 3600) / 60, 2, 10, QLatin1Char('0'));
    }
    if (seconds >= 60)
        return QStringLiteral("%1m").arg(seconds / 60);
    if (seconds == 0)
        return QStringLiteral("0m");
    return QStringLiteral("%1s").arg(seconds);
}

QString humanClock(const QDateTime &when)
{
    return when.isValid() ? when.toString(QStringLiteral("HH:mm:ss")) : kNothing;
}

/// What the profile does when this budget runs out, as a sentence about the
/// user rather than as the name of an enum. spec.md §8 asks the same of the
/// studio: the screen talks about programs and minutes.
QString whenOut(OnExhausted action)
{
    switch (action) {
    case OnExhausted::Close:
        return QStringLiteral("closes");
    case OnExhausted::Logout:
        return QStringLiteral("logs out");
    case OnExhausted::Warn:
        break;
    }
    return QStringLiteral("warns");
}

// -- columns -----------------------------------------------------------------

// Padded to the widest cell, and the last column not padded at all: a scope unit
// is sixty characters wide and trailing spaces after it are noise in anything
// that reads this with a pipe.
void printTable(const QStringList &headers, const QVector<QStringList> &rows,
                const QVector<bool> &rightAligned)
{
    if (rows.isEmpty())
        return;
    QVector<int> widths;
    for (const QString &header : headers)
        widths.append(header.size());
    for (const QStringList &row : rows) {
        for (int i = 0; i < row.size() && i < widths.size(); ++i)
            widths[i] = qMax(widths.at(i), row.at(i).size());
    }

    const auto writeRow = [&](const QStringList &cells) {
        QString line;
        for (int i = 0; i < cells.size(); ++i) {
            const QString &cell = cells.at(i);
            const bool last = i == cells.size() - 1;
            if (!last) {
                line += rightAligned.value(i) ? cell.rightJustified(widths.at(i))
                                              : cell.leftJustified(widths.at(i));
                line += QStringLiteral("  ");
            } else {
                line += rightAligned.value(i) ? cell.rightJustified(widths.at(i)) : cell;
            }
        }
        // A right-aligned last column is padded on its left, so only the left
        // trim would be wrong; QString::trimmed does both, hence chop.
        while (line.endsWith(QLatin1Char(' ')))
            line.chop(1);
        out() << line << '\n';
    };

    writeRow(headers);
    for (const QStringList &row : rows)
        writeRow(row);
}

// -- reading what is on disk -------------------------------------------------

struct Profiles {
    QVector<Profile> all;
    bool missing = false;
};

/// Reads /etc/omahouse/profiles.json, or whatever OMAHOUSE_CONFIG_DIR points at.
///
/// A file that is not there is not a failure: it is a machine where nobody has
/// been put under rules yet, which is every machine before the first
/// `omahouse profile add`. A file that is there and will not parse is a failure,
/// because guessing what it meant would be fiscalising somebody by guess.
bool loadProfiles(Profiles *profiles, int *status)
{
    QString error;
    if (readProfiles(paths::profilesFile(), &profiles->all, &error, &profiles->missing))
        return true;
    fail(QStringLiteral("omahouse: %1").arg(error));
    *status = kUsage;
    return false;
}

const Profile *profileFor(const Profiles &profiles, const QString &user)
{
    for (const Profile &profile : profiles.all) {
        if (profile.user == user)
            return &profile;
    }
    return nullptr;
}

/// One day of one user. `missing` is the ordinary state of a day nobody has
/// spent yet, and comes back as an empty ledger dated that day.
bool loadLedger(const QString &user, const QDate &date, Ledger *ledger, bool *missing,
                int *status)
{
    QString error;
    if (readLedger(paths::ledgerFile(user, date), ledger, &error, missing)) {
        if (*missing) {
            ledger->user = user;
            ledger->date = date;
        }
        return true;
    }
    fail(QStringLiteral("omahouse: %1").arg(error));
    *status = kUsage;
    return false;
}

// -- what a budget looks like right now --------------------------------------

struct Balance {
    QString id;
    QString match;
    /// Below zero when the budget has no limit: it counts and never runs out.
    int limitSeconds = -1;
    int grantedSeconds = 0;
    int usedSeconds = 0;
    int leftSeconds = 0;
    OnExhausted onExhausted = OnExhausted::Warn;

    bool hasLimit() const { return limitSeconds >= 0; }
    bool exhausted() const { return hasLimit() && leftSeconds <= 0; }
};

QVector<Balance> balancesOf(const Profile &profile, const Ledger &ledger)
{
    QVector<Balance> balances;
    for (const Budget &budget : profile.budgets) {
        if (budget.id.isEmpty())
            continue;
        Balance balance;
        balance.id = budget.id;
        balance.match = budget.match;
        balance.onExhausted = budget.onExhausted;
        balance.grantedSeconds = ledger.grantedSeconds(budget.id);
        balance.usedSeconds = ledger.secondsFor(budget.id);
        if (budget.hasLimit()) {
            balance.limitSeconds = budget.dailyMinutes * 60 + balance.grantedSeconds;
            balance.leftSeconds = qMax(0, balance.limitSeconds - balance.usedSeconds);
        }
        balances.append(balance);
    }
    return balances;
}

void printBalances(const QVector<Balance> &balances)
{
    QVector<QStringList> rows;
    for (const Balance &balance : balances) {
        rows.append({
            balance.id,
            balance.hasLimit() ? humanDuration(balance.limitSeconds) : kNothing,
            humanDuration(balance.usedSeconds),
            balance.hasLimit() ? humanDuration(balance.leftSeconds) : kNothing,
            balance.hasLimit() ? whenOut(balance.onExhausted) : QStringLiteral("never runs out"),
        });
    }
    printTable({QStringLiteral("BUDGET"), QStringLiteral("LIMIT"), QStringLiteral("USED"),
                QStringLiteral("LEFT"), QStringLiteral("WHEN OUT")},
               rows, {false, true, true, true, false});
}

QJsonArray balancesToJson(const QVector<Balance> &balances)
{
    QJsonArray array;
    for (const Balance &balance : balances) {
        QJsonObject object {
            {QStringLiteral("id"), balance.id},
            {QStringLiteral("match"), balance.match},
            {QStringLiteral("usedSeconds"), balance.usedSeconds},
            {QStringLiteral("grantedSeconds"), balance.grantedSeconds},
            {QStringLiteral("onExhausted"), onExhaustedName(balance.onExhausted)},
        };
        // Null and not zero for a budget with no limit: zero left is a budget
        // that has run out, and these two must never read the same.
        object.insert(QStringLiteral("limitSeconds"),
                      balance.hasLimit() ? QJsonValue(balance.limitSeconds) : QJsonValue());
        object.insert(QStringLiteral("leftSeconds"),
                      balance.hasLimit() ? QJsonValue(balance.leftSeconds) : QJsonValue());
        object.insert(QStringLiteral("exhausted"), balance.exhausted());
        array.append(object);
    }
    return array;
}

// -- status ------------------------------------------------------------------

bool byProcessesThenName(const AppScope &a, const AppScope &b)
{
    if (a.pidCount != b.pidCount)
        return a.pidCount > b.pidCount;
    return a.unit < b.unit;
}

/// What omahouse saw under app.slice and could not name, and what is in
/// session.slice where it cannot look -- spec.md §5.
///
/// Printed whether or not there is a profile, and phrased as a fact rather than
/// as an alarm. On a live graphical session the second number is never zero:
/// Hyprland's own processes are in it, and omahouse cannot tell them from an app
/// somebody started with a raw `exec`. Saying so is the point. Pretending the
/// number is a clean zero would be the lie.
void printOutOfReach(const QVector<AppScope> &unnamed, const QVector<SessionUnit> &session,
                     int sessionProcesses)
{
    if (unnamed.isEmpty() && session.isEmpty())
        return;

    // How many of them get a line of their own. Thirty tmux panes is what this
    // machine has, and a status screen that is thirty lines of uuid is a status
    // screen nobody reads; `--json` carries all of them.
    constexpr int kNamed = 5;

    out() << "\nOut of reach\n";
    if (!unnamed.isEmpty()) {
        int processes = 0;
        for (const AppScope &scope : unnamed)
            processes += scope.pidCount;
        out() << QStringLiteral("  %1 scope%2 under app.slice omahouse could not name, "
                                "holding %3 process%4:\n")
                     .arg(unnamed.size())
                     .arg(unnamed.size() == 1 ? QString() : QStringLiteral("s"))
                     .arg(processes)
                     .arg(processes == 1 ? QString() : QStringLiteral("es"));
        for (int i = 0; i < unnamed.size() && i < kNamed; ++i) {
            out() << QStringLiteral("    %1  %2\n")
                         .arg(unnamed.at(i).pidCount, 5)
                         .arg(unnamed.at(i).unit);
        }
        if (unnamed.size() > kNamed) {
            out() << QStringLiteral("    … and %1 more (--json lists them all)\n")
                         .arg(unnamed.size() - kNamed);
        }
    }
    if (!session.isEmpty()) {
        out() << QStringLiteral("  %1 process%2 in session.slice omahouse can neither count "
                                "nor close:\n")
                     .arg(sessionProcesses)
                     .arg(sessionProcesses == 1 ? QString() : QStringLiteral("es"));
        for (int i = 0; i < session.size() && i < kNamed; ++i) {
            out() << QStringLiteral("    %1  %2\n")
                         .arg(session.at(i).pidCount, 5)
                         .arg(session.at(i).unit);
        }
        if (session.size() > kNamed) {
            out() << QStringLiteral("    … and %1 more (--json lists them all)\n")
                         .arg(session.size() - kNamed);
        }
    }
    out() << "  An app started outside `uwsm app` — a raw `exec` in a keybinding, or\n"
             "  something opened from a terminal — lands in the compositor's own unit.\n"
             "  It gets no scope, so it is not counted, and closing it would take the\n"
             "  session with it. See spec.md §5.\n";
}

// The scopes whose id is not the name of what is running inside them.
//
// poc/findings.md round 4: seven `app-Hyprland-gtk\x2dlaunch-*.scope` on this
// machine, all of them VS Code, and a terminal that calls itself
// `xdg-terminal-exec`. The rule still matches the id -- that is the model, and
// `Policy::evaluate` never sees any of this -- so the only thing to do about it
// is to say it, which is the same duty spec.md §5 puts on the blind spot.

/// One id and one thing found running under it, however many scopes that was.
///
/// Grouped, because the question this answers is about the id and not about the
/// scope: this machine has seven `gtk-launch` scopes and a rule can only be
/// written about the one word they share. Ungrouped it printed seven lines
/// saying the same sentence.
struct Inside {
    QString id;
    QString exe;
    int processes = 0;
    int readProcesses = 0;
    int scopes = 0;
};

QVector<Inside> whatIsInside(const QVector<AppScope> &scopes)
{
    QVector<Inside> found;
    for (const AppScope &scope : scopes) {
        if (scope.id.isEmpty() || scope.dominantExe.isEmpty())
            continue;
        if (exeCorroboratesId(scope.id, scope.dominantExe))
            continue;
        Inside *group = nullptr;
        for (Inside &candidate : found) {
            if (candidate.id == scope.id && candidate.exe == scope.dominantExe) {
                group = &candidate;
                break;
            }
        }
        if (!group) {
            found.append(Inside {scope.id, scope.dominantExe, 0, 0, 0});
            group = &found.last();
        }
        group->processes += scope.pidCount;
        group->readProcesses += scope.dominantExeCount;
        ++group->scopes;
    }
    std::sort(found.begin(), found.end(), [](const Inside &a, const Inside &b) {
        if (a.readProcesses != b.readProcesses)
            return a.readProcesses > b.readProcesses;
        return a.id == b.id ? a.exe < b.exe : a.id < b.id;
    });
    return found;
}

void printWhatIsInside(const QVector<Inside> &found)
{
    if (found.isEmpty())
        return;

    // As many as the out of reach block names, and for the same reason: a screen
    // that is twenty lines of executable path is a screen nobody reads, and
    // --json carries all of them.
    constexpr int kNamed = 6;

    out() << "\nNot what the name says\n";
    QVector<QStringList> rows;
    for (int i = 0; i < found.size() && i < kNamed; ++i) {
        const Inside &group = found.at(i);
        rows.append({group.id, group.exe,
                     // `7 of 8`, so that a claim made from one readable process
                     // out of twenty looks like the thin claim it is.
                     QStringLiteral("%1 of %2").arg(group.readProcesses).arg(group.processes),
                     QString::number(group.scopes)});
    }
    printTable({QStringLiteral("APP"), QStringLiteral("IS RUNNING"), QStringLiteral("PROCESSES"),
                QStringLiteral("SCOPES")},
               rows, {false, false, true, true});
    if (found.size() > kNamed) {
        out() << QStringLiteral("  … and %1 more (--json lists them all)\n")
                     .arg(found.size() - kNamed);
    }
    out() << "  An app launched through a shim takes the shim's name, so the id above is\n"
             "  the launcher's and not the program's — poc/findings.md round 4 found seven\n"
             "  `gtk-launch` scopes on this machine with VS Code inside them. Rules and\n"
             "  budgets match the id, so a rule about one of these is a rule about whatever\n"
             "  it launches next. A flatpak reads the same way and is not the same case:\n"
             "  its processes really do all run /usr/bin/bwrap, and there the id is right.\n";
}

int cmdStatus(const Globals &g, const QStringList &positionals)
{
    if (positionals.size() > 1) {
        fail(QStringLiteral("status: one user at a time (try omahouse --help)"));
        return kUsage;
    }

    const QString user = positionals.value(0, currentUser());
    if (user.isEmpty()) {
        fail(QStringLiteral("status: this run has no account name of its own; "
                            "say which user to look at"));
        return kUsage;
    }
    uid_t uid = 0;
    if (!uidForUser(user, &uid)) {
        fail(QStringLiteral("status: no account named %1 on this machine").arg(user));
        return kMissing;
    }

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    const Profile *profile = profileFor(profiles, user);

    const Proc proc;
    const bool session = proc.hasSession(uid);
    QVector<AppScope> named;
    QVector<AppScope> unnamed;
    const QVector<AppScope> scopes = proc.scopesFor(uid);
    for (const AppScope &scope : scopes) {
        // An empty scope is a unit systemd has not reaped, not an app somebody
        // has open, and it is left out of both lists rather than reported as
        // either.
        if (scope.pidCount == 0)
            continue;
        (scope.id.isEmpty() ? unnamed : named).append(scope);
    }
    std::sort(named.begin(), named.end(), byProcessesThenName);
    std::sort(unnamed.begin(), unnamed.end(), byProcessesThenName);
    // What each scope is really running. One readlink per process, and only on
    // the screen an operator reads; the tick of stage 6 has no use for it.
    proc.resolveDominantExe(&named);
    proc.resolveDominantExe(&unnamed);
    const QVector<SessionUnit> sessionUnits = proc.sessionSliceUnits(uid);
    int sessionProcesses = 0;
    for (const SessionUnit &unit : sessionUnits)
        sessionProcesses += unit.pidCount;

    const QDate today = QDate::currentDate();
    Ledger ledger;
    bool ledgerMissing = false;
    QVector<Balance> balances;
    if (profile) {
        if (!loadLedger(user, today, &ledger, &ledgerMissing, &status))
            return status;
        balances = balancesOf(*profile, ledger);
    }

    if (g.json) {
        QJsonArray scopesJson;
        for (const AppScope &scope : named) {
            QJsonObject object {
                {QStringLiteral("id"), scope.id},
                {QStringLiteral("unit"), scope.unit},
                {QStringLiteral("cgroup"), scope.cgroupPath},
                {QStringLiteral("processes"), scope.pidCount},
            };
            // Null and not an empty string: "nothing in there could be read" and
            // "it is running the empty path" are different answers, and only one
            // of them exists.
            object.insert(QStringLiteral("exe"),
                          scope.dominantExe.isEmpty() ? QJsonValue()
                                                      : QJsonValue(scope.dominantExe));
            object.insert(QStringLiteral("exeProcesses"), scope.dominantExeCount);
            object.insert(QStringLiteral("exeAgrees"),
                          scope.dominantExe.isEmpty()
                              ? QJsonValue()
                              : QJsonValue(exeCorroboratesId(scope.id, scope.dominantExe)));
            if (profile)
                object.insert(QStringLiteral("verdict"), verdictName(profile->verdictFor(scope.id)));
            scopesJson.append(object);
        }
        QJsonArray unnamedJson;
        for (const AppScope &scope : unnamed) {
            QJsonObject object {
                {QStringLiteral("unit"), scope.unit},
                {QStringLiteral("cgroup"), scope.cgroupPath},
                {QStringLiteral("processes"), scope.pidCount},
            };
            // No id to agree or disagree with, and the executable is the only
            // thing anybody can say about it.
            object.insert(QStringLiteral("exe"),
                          scope.dominantExe.isEmpty() ? QJsonValue()
                                                      : QJsonValue(scope.dominantExe));
            object.insert(QStringLiteral("exeProcesses"), scope.dominantExeCount);
            unnamedJson.append(object);
        }
        QJsonArray sessionJson;
        for (const SessionUnit &unit : sessionUnits) {
            sessionJson.append(QJsonObject {
                {QStringLiteral("unit"), unit.unit},
                {QStringLiteral("cgroup"), unit.cgroupPath},
                {QStringLiteral("processes"), unit.pidCount},
            });
        }
        QJsonObject document {
            {QStringLiteral("user"), user},
            {QStringLiteral("uid"), static_cast<qint64>(uid)},
            {QStringLiteral("date"), today.toString(Qt::ISODate)},
            {QStringLiteral("session"), session},
            {QStringLiteral("profile"), profile ? QJsonValue(profile->toJson()) : QJsonValue()},
            {QStringLiteral("scopes"), scopesJson},
            {QStringLiteral("unnamed"), unnamedJson},
            {QStringLiteral("outOfReach"),
             QJsonObject {{QStringLiteral("processes"), sessionProcesses},
                          {QStringLiteral("units"), sessionJson}}},
            {QStringLiteral("budgets"), balancesToJson(balances)},
        };
        if (profile)
            document.insert(QStringLiteral("ledgerWritten"), !ledgerMissing);
        printJson(document);
        return kOk;
    }

    out() << QStringLiteral("omahouse status — %1").arg(user);
    if (profile && !profile->displayName.isEmpty())
        out() << QStringLiteral(" (%1)").arg(profile->displayName);
    out() << '\n';

    if (profile) {
        out() << QStringLiteral("%1, default %2, %3 · %4\n")
                     .arg(profile->enabled ? (profile->enforce ? QStringLiteral("enforcing")
                                                               : QStringLiteral("observing"))
                                           : QStringLiteral("disabled"),
                          verdictName(profile->defaultVerdict),
                          today.toString(Qt::ISODate),
                          ledgerMissing ? QStringLiteral("nothing counted yet today")
                                        : QStringLiteral("ledger on disk"));
    } else if (profiles.missing) {
        // Useful, and not a stack trace: the file not being there is the state
        // of every machine before the first profile is written.
        out() << QStringLiteral("No %1 on this machine, so nobody is under rules. "
                                "Scanning only.\n")
                     .arg(paths::profilesFile());
    } else {
        out() << QStringLiteral("No profile for %1 in %2. Scanning only.\n")
                     .arg(user, paths::profilesFile());
    }

    if (!session) {
        out() << QStringLiteral("\n%1 is not logged in: there is no app.slice under "
                                "user@%2.service.\n")
                     .arg(user)
                     .arg(static_cast<qulonglong>(uid));
    } else if (named.isEmpty()) {
        out() << "\nNo app scopes with anything running in them.\n";
    } else {
        out() << '\n';
        QStringList headers {QStringLiteral("APP"), QStringLiteral("PIDS")};
        QVector<bool> right {false, true};
        if (profile) {
            headers.append(QStringLiteral("VERDICT"));
            right.append(false);
        }
        headers.append(QStringLiteral("SCOPE"));
        right.append(false);

        QVector<QStringList> rows;
        for (const AppScope &scope : named) {
            QStringList row {scope.id, QString::number(scope.pidCount)};
            if (profile)
                row.append(verdictName(profile->verdictFor(scope.id)));
            row.append(scope.unit);
            rows.append(row);
        }
        printTable(headers, rows, right);
    }

    if (profile) {
        if (balances.isEmpty()) {
            out() << QStringLiteral("\nNo budgets: %1 has no time limits, only verdicts.\n")
                         .arg(user);
        } else {
            out() << '\n';
            printBalances(balances);
        }
    }

    printWhatIsInside(whatIsInside(named));
    printOutOfReach(unnamed, sessionUnits, sessionProcesses);
    return kOk;
}

// -- report ------------------------------------------------------------------

/// What one event says, in words. The core keeps the kind and the number apart
/// on purpose -- Policy.cpp says the core is the wrong place to write a sentence
/// -- so the sentence is written here.
QString eventSentence(const Event &event)
{
    switch (event.kind) {
    case EventKind::Warn:
        if (event.minutes > 0)
            return QStringLiteral("%1 minutes left").arg(event.minutes);
        return QStringLiteral("out of time");
    case EventKind::Exhausted:
        return QStringLiteral("ran out");
    case EventKind::Denied:
        break;
    }
    return QStringLiteral("not allowed: %1").arg(event.scope);
}

void printDay(const Ledger &ledger)
{
    out() << QStringLiteral("\n%1\n").arg(ledger.date.toString(Qt::ISODate));

    QVector<QStringList> rows;
    for (auto it = ledger.seconds.cbegin(); it != ledger.seconds.cend(); ++it)
        rows.append({it.key(), humanDuration(it.value())});
    if (rows.isEmpty())
        out() << "  nothing counted\n";
    else
        printTable({QStringLiteral("BUDGET"), QStringLiteral("USED")}, rows, {false, true});

    if (!ledger.grants.isEmpty()) {
        out() << "\nGRANTS\n";
        QVector<QStringList> grantRows;
        for (const Grant &grant : ledger.grants) {
            grantRows.append({humanClock(grant.at), grant.by, grant.budget,
                              QStringLiteral("+%1m").arg(grant.minutes)});
        }
        printTable({QStringLiteral("AT"), QStringLiteral("BY"), QStringLiteral("BUDGET"),
                    QStringLiteral("ADDED")},
                   grantRows, {false, false, false, true});
    }

    if (!ledger.events.isEmpty()) {
        out() << "\nEVENTS\n";
        QVector<QStringList> eventRows;
        for (const Event &event : ledger.events) {
            eventRows.append({humanClock(event.at), eventKindName(event.kind),
                              event.budget.isEmpty() ? kNothing : event.budget,
                              eventSentence(event)});
        }
        printTable({QStringLiteral("AT"), QStringLiteral("KIND"), QStringLiteral("BUDGET"),
                    QStringLiteral("WHAT")},
                   eventRows, {false, false, false, false});
    }
}

/// How far back `--since` may reach. A year and a day, so that "the whole of
/// last year" works and a typo like `--since 1970-01-01` says so instead of
/// opening twenty thousand files.
constexpr int kFurthestBack = 366;

int cmdReport(const Globals &g, const QStringList &positionals, const QString &since)
{
    if (positionals.size() != 1) {
        fail(QStringLiteral("report: which user? (try omahouse --help)"));
        return kUsage;
    }
    const QString user = positionals.first();

    const QDate today = QDate::currentDate();
    QDate from = today;
    if (!since.isEmpty()) {
        from = QDate::fromString(since, Qt::ISODate);
        if (!from.isValid()) {
            fail(QStringLiteral("report: --since wants a date like 2026-09-01, not '%1'")
                     .arg(since));
            return kUsage;
        }
        if (from > today) {
            fail(QStringLiteral("report: --since %1 is in the future").arg(since));
            return kUsage;
        }
        if (from.daysTo(today) >= kFurthestBack) {
            fail(QStringLiteral("report: --since %1 is more than %2 days back")
                     .arg(since)
                     .arg(kFurthestBack));
            return kUsage;
        }
    }

    QVector<Ledger> days;
    QMap<QString, int> totals;
    for (QDate date = from; date <= today; date = date.addDays(1)) {
        Ledger ledger;
        bool missing = false;
        int status = kOk;
        if (!loadLedger(user, date, &ledger, &missing, &status))
            return status;
        // A day with no file is a day nobody spent. It is left out rather than
        // printed as a row of zeroes, which would be a day of accounting that
        // never happened.
        if (missing)
            continue;
        for (auto it = ledger.seconds.cbegin(); it != ledger.seconds.cend(); ++it)
            totals[it.key()] += it.value();
        days.append(ledger);
    }

    if (g.json) {
        QJsonArray daysJson;
        for (const Ledger &ledger : days) {
            QJsonObject object = ledger.toJson();
            // The report is one document about one user over a range; the
            // per-file preamble would be the same words on every day of it.
            object.remove(QStringLiteral("schemaVersion"));
            object.remove(QStringLiteral("user"));
            daysJson.append(object);
        }
        QJsonObject totalsJson;
        for (auto it = totals.cbegin(); it != totals.cend(); ++it)
            totalsJson.insert(it.key(), it.value());
        printJson(QJsonObject {
            {QStringLiteral("user"), user},
            {QStringLiteral("since"), from.toString(Qt::ISODate)},
            {QStringLiteral("until"), today.toString(Qt::ISODate)},
            {QStringLiteral("days"), daysJson},
            {QStringLiteral("totals"), totalsJson},
        });
        return kOk;
    }

    if (days.isEmpty()) {
        note(QStringLiteral("omahouse: nothing counted for %1 %2; there is no %3")
                 .arg(user,
                      from == today ? QStringLiteral("today")
                                    : QStringLiteral("between %1 and %2")
                                          .arg(from.toString(Qt::ISODate),
                                               today.toString(Qt::ISODate)),
                      paths::ledgerFile(user, from)));
        return kOk;
    }

    out() << QStringLiteral("omahouse report — %1").arg(user);
    if (from != today) {
        out() << QStringLiteral(", %1 to %2")
                     .arg(from.toString(Qt::ISODate), today.toString(Qt::ISODate));
    }
    out() << '\n';

    for (const Ledger &ledger : days)
        printDay(ledger);

    if (days.size() > 1) {
        out() << "\nTOTAL\n";
        QVector<QStringList> rows;
        for (auto it = totals.cbegin(); it != totals.cend(); ++it)
            rows.append({it.key(), humanDuration(it.value())});
        printTable({QStringLiteral("BUDGET"), QStringLiteral("USED")}, rows, {false, true});
    }
    return kOk;
}

// -- profile -----------------------------------------------------------------

QString yesNo(bool value)
{
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

int cmdProfileList(const Globals &g)
{
    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;

    if (g.json) {
        QJsonArray array;
        for (const Profile &profile : profiles.all) {
            array.append(QJsonObject {
                {QStringLiteral("user"), profile.user},
                {QStringLiteral("displayName"), profile.displayName},
                {QStringLiteral("enabled"), profile.enabled},
                {QStringLiteral("enforce"), profile.enforce},
                {QStringLiteral("default"), verdictName(profile.defaultVerdict)},
                {QStringLiteral("rules"), profile.rules.size()},
                {QStringLiteral("budgets"), profile.budgets.size()},
            });
        }
        printJson(array);
        return kOk;
    }

    if (profiles.all.isEmpty()) {
        note(profiles.missing
                 ? QStringLiteral("omahouse: there is no %1 yet, so nobody is under rules")
                       .arg(paths::profilesFile())
                 : QStringLiteral("omahouse: %1 holds no profiles")
                       .arg(paths::profilesFile()));
        return kOk;
    }

    QVector<QStringList> rows;
    for (const Profile &profile : profiles.all) {
        rows.append({
            profile.user,
            profile.displayName.isEmpty() ? kNothing : profile.displayName,
            yesNo(profile.enabled),
            yesNo(profile.enforce),
            verdictName(profile.defaultVerdict),
            QString::number(profile.rules.size()),
            QString::number(profile.budgets.size()),
        });
    }
    printTable({QStringLiteral("USER"), QStringLiteral("NAME"), QStringLiteral("ENABLED"),
                QStringLiteral("ENFORCE"), QStringLiteral("DEFAULT"), QStringLiteral("RULES"),
                QStringLiteral("BUDGETS")},
               rows, {false, false, false, false, false, true, true});
    return kOk;
}

int cmdProfileShow(const Globals &g, const QStringList &positionals)
{
    if (positionals.size() != 1) {
        fail(QStringLiteral("profile show: which user? (try omahouse --help)"));
        return kUsage;
    }
    const QString user = positionals.first();

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    const Profile *profile = profileFor(profiles, user);
    if (!profile) {
        fail(profiles.missing
                 ? QStringLiteral("profile show: there is no %1 yet, so %2 has no profile")
                       .arg(paths::profilesFile(), user)
                 : QStringLiteral("profile show: no profile for %1 in %2")
                       .arg(user, paths::profilesFile()));
        return kMissing;
    }

    if (g.json) {
        printJson(profile->toJson());
        return kOk;
    }

    out() << QStringLiteral("omahouse profile — %1").arg(profile->user);
    if (!profile->displayName.isEmpty())
        out() << QStringLiteral(" (%1)").arg(profile->displayName);
    out() << "\n\n";

    QStringList marks;
    for (int mark : profile->warnAt)
        marks.append(QStringLiteral("%1m").arg(mark));

    QVector<QStringList> settings {
        {QStringLiteral("enabled"), yesNo(profile->enabled)},
        {QStringLiteral("enforce"),
         profile->enforce ? QStringLiteral("yes")
                          : QStringLiteral("no — counts and reports, closes nothing")},
        {QStringLiteral("default"), verdictName(profile->defaultVerdict)},
        {QStringLiteral("warn at"),
         marks.isEmpty() ? kNothing : marks.join(QStringLiteral(", ")) + QStringLiteral(" left")},
        {QStringLiteral("grace"), QStringLiteral("%1s").arg(profile->graceSeconds)},
    };
    printTable({QStringLiteral("SETTING"), QStringLiteral("VALUE")}, settings, {false, false});

    if (profile->rules.isEmpty()) {
        out() << QStringLiteral("\nNo rules: every app falls to the default, %1.\n")
                     .arg(verdictName(profile->defaultVerdict));
    } else {
        out() << "\nRULES\n";
        QVector<QStringList> rows;
        for (const Rule &rule : profile->rules)
            rows.append({verdictName(rule.verdict), rule.match});
        printTable({QStringLiteral("VERDICT"), QStringLiteral("APP")}, rows, {false, false});
    }

    if (profile->budgets.isEmpty()) {
        out() << "\nNo budgets: nothing is on the clock.\n";
    } else {
        out() << "\nBUDGETS\n";
        QVector<QStringList> rows;
        for (const Budget &budget : profile->budgets) {
            rows.append({budget.id, budget.match,
                         budget.hasLimit() ? humanDuration(budget.dailyMinutes * 60) : kNothing,
                         budget.hasLimit() ? whenOut(budget.onExhausted)
                                           : QStringLiteral("never runs out")});
        }
        printTable({QStringLiteral("BUDGET"), QStringLiteral("APP"), QStringLiteral("A DAY"),
                    QStringLiteral("WHEN OUT")},
                   rows, {false, false, true, false});
    }
    return kOk;
}

int cmdProfile(const Globals &g, const QStringList &positionals)
{
    const QString subcommand = positionals.value(0);
    if (subcommand.isEmpty()) {
        fail(QStringLiteral("profile: list or show? (try omahouse --help)"));
        return kUsage;
    }
    if (subcommand == QLatin1String("list")) {
        if (positionals.size() > 1) {
            fail(QStringLiteral("profile list: it takes no user; try omahouse profile show %1")
                     .arg(positionals.value(1)));
            return kUsage;
        }
        return cmdProfileList(g);
    }
    if (subcommand == QLatin1String("show"))
        return cmdProfileShow(g, positionals.mid(1));
    // The writing subcommands of spec.md §7 arrive with stage 5 of plan.md.
    // Naming them here rather than answering "unknown" tells a reader that the
    // verb exists and this build cannot do it yet.
    if (subcommand == QLatin1String("add") || subcommand == QLatin1String("remove")
        || subcommand == QLatin1String("enforce") || subcommand == QLatin1String("default")) {
        fail(QStringLiteral("profile %1: this build only reads; the verbs that write "
                            "arrive with stage 5 of plan.md")
                 .arg(subcommand));
        return kUsage;
    }
    fail(QStringLiteral("profile: unknown subcommand '%1' (try omahouse --help)")
             .arg(subcommand));
    return kUsage;
}

// -- the screen --------------------------------------------------------------

void printHelp()
{
    out() << "omahouse " OMAHOUSE_VERSION R"( — house rules for the accounts on this machine

Usage: omahouse <command> [options]

Reading:
  status [user]            live app scopes, what they are, and what is left
                           without a profile it scans and reports, nothing else
  report <user>            the day's ledger
         [--since <date>]  every day from that one, and a total
  profile list             who is under rules
  profile show <user>      the rules, the budgets and what happens when they end

Globals:
  --json, -j               one JSON document on stdout   (OMAHOUSE_JSON)
  --version, -V
  --help, -h

Files:
  /etc/omahouse/profiles.json          who is under rules   (OMAHOUSE_CONFIG_DIR)
  /var/lib/omahouse/<user>/<date>.json the day's ledger     (OMAHOUSE_STATE_DIR)
  /sys/fs/cgroup                       the scopes           (OMAHOUSE_CGROUP_ROOT)

Nothing here writes. The verbs that configure a profile arrive with stage 5 of
plan.md, and `watch`, which is the one that counts and closes, with stage 6.

Exit: 0 it answered, 1 a usage error or a file it could not read, 2 no such
account or no such profile.
)";
}

int dispatch(const Globals &g, const QStringList &args)
{
    // Stripped before the verb's own parse, the way the globals were: the word
    // after an option that takes one is that option's value.
    QString since;
    QStringList rest;
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--since")) {
            if (i + 1 >= args.size()) {
                fail(QStringLiteral("--since wants a date like 2026-09-01"));
                return kUsage;
            }
            since = args.at(++i);
            continue;
        }
        rest.append(args.at(i));
    }

    for (const QString &arg : rest) {
        if (arg.startsWith(QLatin1Char('-'))) {
            fail(QStringLiteral("omahouse: unknown option '%1' (try omahouse --help)").arg(arg));
            return kUsage;
        }
    }

    const QString verb = rest.value(0);
    const QStringList positionals = rest.mid(1);

    if (verb == QLatin1String("status")) {
        if (!since.isEmpty()) {
            fail(QStringLiteral("status: --since is for report; status is about now"));
            return kUsage;
        }
        return cmdStatus(g, positionals);
    }
    if (verb == QLatin1String("report"))
        return cmdReport(g, positionals, since);
    if (verb == QLatin1String("profile")) {
        if (!since.isEmpty()) {
            fail(QStringLiteral("profile: --since is for report"));
            return kUsage;
        }
        return cmdProfile(g, positionals);
    }
    // The verbs spec.md §7 declares and later stages of plan.md build. Named,
    // rather than met with "unknown command", because they are not typos.
    if (verb == QLatin1String("allow") || verb == QLatin1String("deny")
        || verb == QLatin1String("limit") || verb == QLatin1String("grant")) {
        fail(QStringLiteral("%1: this build only reads; the verbs that write arrive with "
                            "stage 5 of plan.md")
                 .arg(verb));
        return kUsage;
    }
    if (verb == QLatin1String("watch")) {
        fail(QStringLiteral("watch: the daemon arrives with stage 6 of plan.md"));
        return kUsage;
    }
    fail(QStringLiteral("omahouse: unknown command '%1' (try omahouse --help)").arg(verb));
    return kUsage;
}

/// A write that failed is not a run that succeeded: stdout is a buffer, and the
/// last line of a report can land in it and the flush that empties it fail.
int finished(int status)
{
    out().flush();
    err().flush();
    if (out().status() == QTextStream::Ok)
        return status;
    fail(QStringLiteral("omahouse: write error"));
    return status == kOk ? 1 : status;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("omahouse"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OMAHOUSE_VERSION));

    Globals g;
    g.json = envFlag("OMAHOUSE_JSON");

    QStringList rest;
    const QStringList args = QCoreApplication::arguments().mid(1);
    for (const QString &arg : args) {
        if (arg == QLatin1String("--json") || arg == QLatin1String("-j"))
            g.json = true;
        else if (arg == QLatin1String("--help") || arg == QLatin1String("-h"))
            g.help = true;
        else if (arg == QLatin1String("--version") || arg == QLatin1String("-V"))
            g.version = true;
        else
            rest.append(arg);
    }

    if (g.version) {
        out() << "omahouse " << omahouseVersion() << '\n';
        return finished(kOk);
    }
    // Asking for help is not a usage error. Running with no verb is: a script
    // that got here by accident has to be able to tell the two apart.
    if (g.help) {
        printHelp();
        return finished(kOk);
    }
    if (rest.isEmpty()) {
        printHelp();
        return finished(kUsage);
    }

    return finished(dispatch(g, rest));
}
