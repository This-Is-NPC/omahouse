#include "Duration.h"
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
    /// The whole command line after the program name, kept so that a run that
    /// needs root can print the same line back with `pkexec` in front of it.
    QStringList commandLine;
};

// Every option any verb takes, parsed once and handed to the verb that was
// asked for.
//
// Parsed in one place and not per verb, because the word after an option that
// takes one is that option's value and not a positional -- `--name Júlia` is one
// thing -- and a verb that has to know that before it can find its own user
// argument is a verb that has to parse the whole line anyway.
//
// `given` is what keeps an option from being quietly ignored by a verb that has
// no use for it: `omahouse deny julia code --limit 45m` reads as a limit
// somebody asked for and did not get, so it is a usage error rather than a
// silently dropped word.
struct Options {
    QString since;
    QString name;
    QString limit;
    QString session;
    QString budget;
    bool createUser = false;
    bool keepAccount = false;
    bool on = false;
    bool off = false;
    bool allow = false;
    bool deny = false;
    QStringList given;
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

// -- the command line --------------------------------------------------------

/// Splits the arguments into the options they contain and everything else.
///
/// `--name Júlia` and `--name=Júlia` are the same thing, because both are typed.
/// An option with no value left on the line is a usage error naming what it
/// wanted, rather than an empty string that would go on to mean "no name".
bool parseOptions(const QStringList &args, Options *options, QStringList *positionals)
{
    struct Value {
        const char *name;
        QString Options::*field;
        const char *wants;
    };
    static const Value values[] = {
        {"--since", &Options::since, "a date like 2026-09-01"},
        {"--name", &Options::name, "a name, like \"Júlia\""},
        {"--limit", &Options::limit, "a length of time, like 45m"},
        {"--session", &Options::session, "a length of time, like 2h"},
        {"--budget", &Options::budget, "an id and a length of time, like minecraft=45m"},
    };
    struct Flag {
        const char *name;
        bool Options::*field;
    };
    static const Flag flags[] = {
        {"--create-user", &Options::createUser}, {"--keep-account", &Options::keepAccount},
        {"--on", &Options::on},                  {"--off", &Options::off},
        {"--allow", &Options::allow},            {"--deny", &Options::deny},
    };

    for (int i = 0; i < args.size(); ++i) {
        const QString argument = args.at(i);
        bool handled = false;

        for (const Value &value : values) {
            const QString name = QString::fromLatin1(value.name);
            if (argument == name) {
                if (i + 1 >= args.size()) {
                    fail(QStringLiteral("%1 wants %2").arg(name, QString::fromLatin1(value.wants)));
                    return false;
                }
                options->*(value.field) = args.at(++i);
            } else if (argument.startsWith(name + QLatin1Char('='))) {
                options->*(value.field) = argument.mid(name.size() + 1);
            } else {
                continue;
            }
            if ((options->*(value.field)).isEmpty()) {
                fail(QStringLiteral("%1 wants %2").arg(name, QString::fromLatin1(value.wants)));
                return false;
            }
            options->given.append(name);
            handled = true;
            break;
        }
        if (handled)
            continue;

        for (const Flag &flag : flags) {
            if (argument != QLatin1String(flag.name))
                continue;
            options->*(flag.field) = true;
            options->given.append(argument);
            handled = true;
            break;
        }
        if (handled)
            continue;

        if (argument.startsWith(QLatin1Char('-'))) {
            fail(QStringLiteral("omahouse: unknown option '%1' (try omahouse --help)")
                     .arg(argument));
            return false;
        }
        positionals->append(argument);
    }
    return true;
}

/// Refuses an option that belongs to another verb instead of ignoring it.
bool onlyTheseOptions(const Options &options, const QStringList &allowed, const QString &verb)
{
    for (const QString &given : options.given) {
        if (allowed.contains(given))
            continue;
        fail(QStringLiteral("%1 takes no %2 (try omahouse --help)").arg(verb, given));
        return false;
    }
    return true;
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

// -- writing -----------------------------------------------------------------
//
// Everything below changes /etc/omahouse/profiles.json or the day's ledger, and
// every one of them goes through the core's atomic write: a reader of either
// file sees the whole of the last change or the whole of the one before it, and
// never half of a write that a power cut ended.
//
// Two doors to get in without root, and they are the same two the reading verbs
// use: $OMAHOUSE_CONFIG_DIR and $OMAHOUSE_STATE_DIR. A run pointed at a tree of
// its own writes there and asks nobody's permission; a run pointed at the
// machine's own files is writing what a root daemon reads, and that one is
// root's. That is the whole of the privilege rule, and it is what lets the end
// to end suite create, edit and read a profile as an ordinary user.

/// A command line as somebody would type it again, for the `pkexec` suggestion.
QString retype(const QStringList &args)
{
    QStringList quoted;
    for (const QString &arg : args) {
        if (arg.isEmpty() || arg.contains(QLatin1Char(' ')))
            quoted.append(QStringLiteral("'%1'").arg(arg));
        else
            quoted.append(arg);
    }
    return quoted.join(QLatin1Char(' '));
}

/// Whether this run may write `path`, with the refusal spelled out.
///
/// Checked before anything is opened, so that the answer is a sentence about
/// privilege and not the errno of a temporary file that could not be created.
/// `omafiles` earned this the same way: a raw `Permission denied` names the
/// thing that failed and not the thing to do about it.
bool mayWrite(const QString &verb, const QString &path, bool theSystems, const Globals &g)
{
    if (runningAsRoot() || !theSystems)
        return true;
    fail(QStringLiteral("%1: writing %2 needs root, and this run is %3.")
             .arg(verb, path, currentUser()));
    fail(QStringLiteral("%1  try:  pkexec omahouse %2")
             .arg(QString(verb.size(), QLatin1Char(' ')), retype(g.commandLine)));
    return false;
}

bool saveProfiles(const QString &verb, const QVector<Profile> &profiles)
{
    QString error;
    if (writeProfiles(paths::profilesFile(), profiles, &error))
        return true;
    fail(QStringLiteral("%1: %2").arg(verb, error));
    if (!runningAsRoot()) {
        fail(QStringLiteral("%1  that file belongs to root; try pkexec")
                 .arg(QString(verb.size(), QLatin1Char(' '))));
    }
    return false;
}

/// The profile of `user` to change, or a refusal that says how to make one.
Profile *profileToChange(const QString &verb, Profiles *profiles, const QString &user,
                         int *status)
{
    for (Profile &profile : profiles->all) {
        if (profile.user == user)
            return &profile;
    }
    fail(QStringLiteral("%1: %2 has no profile; omahouse profile add %2 makes one")
             .arg(verb, user));
    *status = kMissing;
    return nullptr;
}

/// The rule about `id`, made at the end of the list if there is none.
///
/// Changed in place rather than appended to, because the first rule that names
/// an app is the one that wins: two lines about `firefox` would leave the second
/// one dead, and an operator who typed `deny` last would go on being surprised.
Rule *ruleFor(Profile *profile, const QString &id)
{
    for (Rule &rule : profile->rules) {
        if (rule.match == id)
            return &rule;
    }
    profile->rules.append(Rule {id, Verdict::Allow});
    return &profile->rules.last();
}

Budget *budgetFor(Profile *profile, const QString &id)
{
    for (Budget &budget : profile->budgets) {
        if (budget.id == id)
            return &budget;
    }
    return nullptr;
}

/// One line of what a verb did, on stdout, and the profile itself under --json.
int wrote(const Globals &g, const Profile &profile, const QString &line)
{
    if (g.json)
        printJson(profile.toJson());
    else
        out() << line << '\n';
    return kOk;
}

/// What an id would really let in, said to whoever is about to let it in.
///
/// spec.md §5: an app launched through a shim takes the shim's name, so a rule
/// about `gtk-launch` is a rule about whatever gtk-launch launches next. This
/// warns and does not refuse. It may be exactly what somebody meant -- and the
/// same reading calls a flatpak a shim, because every flatpak on a machine runs
/// /usr/bin/bwrap -- so the CLI says what is in there and lets the operator
/// decide. Nothing is invented: with no session open under that id there is no
/// evidence, and with no evidence there is nothing to say.
void warnAboutWhatIsInside(const QString &user, const QString &id)
{
    uid_t uid = 0;
    if (!uidForUser(user, &uid))
        return;

    const Proc proc;
    QVector<AppScope> mine;
    for (const AppScope &scope : proc.scopesFor(uid)) {
        if (scope.id == id && scope.pidCount > 0)
            mine.append(scope);
    }
    proc.resolveDominantExe(&mine);
    const QVector<Inside> found = whatIsInside(mine);
    if (found.isEmpty())
        return;

    int scopes = 0;
    int width = 0;
    for (const Inside &group : found) {
        scopes += group.scopes;
        width = qMax(width, group.exe.size());
    }
    note(QStringLiteral("omahouse: `%1` is not the name of one program on this machine.")
             .arg(id));
    note(QStringLiteral("  %1 has %2 scope%3 open under that id, and what is running inside "
                        "them is:")
             .arg(user)
             .arg(scopes)
             .arg(scopes == 1 ? QString() : QStringLiteral("s")));
    for (const Inside &group : found) {
        note(QStringLiteral("      %1  %2 of %3 processes in %4 scope%5")
                 .arg(group.exe.leftJustified(width))
                 .arg(group.readProcesses)
                 .arg(group.processes)
                 .arg(group.scopes)
                 .arg(group.scopes == 1 ? QString() : QStringLiteral("s")));
    }
    note(QStringLiteral("  An app launched through a shim takes the shim's name, so this rule"));
    note(QStringLiteral("  is about whatever `%1` launches next, and not about one program.")
             .arg(id));
    note(QStringLiteral("  It was written anyway — it may be what you meant, and a flatpak"));
    note(QStringLiteral("  reads exactly the same way. To take it back:"));
    note(QStringLiteral("      omahouse deny %1 %2").arg(user, id));
}

int cmdProfileAdd(const Globals &g, const QStringList &positionals, const Options &options)
{
    if (positionals.size() != 1) {
        fail(QStringLiteral("profile add: which user? (try omahouse --help)"));
        return kUsage;
    }
    const QString user = positionals.first();
    if (!mayWrite(QStringLiteral("profile add"), paths::profilesFile(),
                  paths::configDirIsTheSystems(), g))
        return kUsage;

    // spec.md §1: the operator is whoever is in wheel. A profile for one of them
    // is somebody fiscalising themselves by accident, and the person who could
    // undo it is the same person it would be imposed on -- so it is refused
    // rather than warned about, which is the one place this stage refuses.
    QString why;
    if (isAdministrator(user, &why)) {
        fail(QStringLiteral("profile add: %1 %2, and an administrator does not fiscalise "
                            "themselves by accident.")
                 .arg(user, why));
        fail(QStringLiteral("             Take the account out of wheel first, or write the "
                            "profile for somebody else."));
        return kUsage;
    }

    uid_t uid = 0;
    const bool exists = uidForUser(user, &uid);
    if (options.createUser) {
        if (exists) {
            note(QStringLiteral("omahouse: %1 already has an account; --create-user had "
                                "nothing to do")
                     .arg(user));
        } else {
            QString error;
            if (!createAccount(user, &error)) {
                fail(QStringLiteral("profile add: %1").arg(error));
                return kUsage;
            }
            note(QStringLiteral("omahouse: %1 -m %2").arg(useraddProgram(), user));
        }
    } else if (!exists) {
        // Written anyway, and said out loud. A profile can be written before the
        // account is -- and can outlive it, which is why `report` does not ask
        // either -- so refusing here would be refusing a legitimate order. The
        // note is what makes a typo a thing somebody notices.
        note(QStringLiteral("omahouse: there is no account named %1 on this machine yet. "
                            "The profile is written all the same; --create-user makes the "
                            "account.")
                 .arg(user));
    }

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    if (profileFor(profiles, user)) {
        fail(QStringLiteral("profile add: %1 already has a profile; omahouse profile show %1")
                 .arg(user));
        return kUsage;
    }

    Profile profile;
    profile.user = user;
    profile.displayName = options.name;
    profile.enabled = true;
    // spec.md §5: a new profile observes. It counts and reports and closes
    // nothing, because seeing a day of the report before switching the teeth on
    // is what the lan house always did.
    profile.enforce = false;
    // And it allows, which is spec.md §4's reading of a missing `default`: a
    // profile written by halves that counts without biting is recoverable, and
    // one that denies everything locks somebody out of their own machine.
    profile.defaultVerdict = Verdict::Allow;
    // The marks of §4's example. A profile with no warnAt closes without a word,
    // and §6 says nobody is cut off cold.
    profile.warnAt = {10, 5, 1};
    profile.graceSeconds = 20;
    profiles.all.append(profile);

    if (!saveProfiles(QStringLiteral("profile add"), profiles.all))
        return kUsage;

    if (g.json) {
        printJson(profile.toJson());
        return kOk;
    }
    out() << QStringLiteral("%1: profile written to %2\n").arg(user, paths::profilesFile());
    out() << QStringLiteral("  observing — it counts and reports and closes nothing. Watch a "
                            "day of\n"
                            "  `omahouse report %1`, then `omahouse profile enforce %1 --on`.\n")
                 .arg(user);
    out() << QStringLiteral("  every app is allowed: `omahouse profile default %1 --deny` turns "
                            "the\n"
                            "  rules into a list of what is allowed instead.\n")
                 .arg(user);
    return kOk;
}

int cmdProfileRemove(const Globals &g, const QStringList &positionals, const Options &options)
{
    if (positionals.size() != 1) {
        fail(QStringLiteral("profile remove: which user? (try omahouse --help)"));
        return kUsage;
    }
    const QString user = positionals.first();
    if (!mayWrite(QStringLiteral("profile remove"), paths::profilesFile(),
                  paths::configDirIsTheSystems(), g))
        return kUsage;

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;

    Profile removed;
    bool found = false;
    QVector<Profile> kept;
    for (const Profile &profile : profiles.all) {
        if (profile.user == user && !found) {
            removed = profile;
            found = true;
            continue;
        }
        kept.append(profile);
    }
    if (!found) {
        fail(QStringLiteral("profile remove: no profile for %1 in %2")
                 .arg(user, paths::profilesFile()));
        return kMissing;
    }
    if (!saveProfiles(QStringLiteral("profile remove"), kept))
        return kUsage;

    // The account is left where it is, always. spec.md §7 spells the flag as
    // `--keep-account`, which reads as though dropping it would delete the
    // account; this build never runs `userdel`. Deleting somebody's account and
    // their home directory because their screen time was taken off the books is
    // a loss nothing here can undo, and `--create-user` is already the one verb
    // plan.md says may not even be exercised outside the VM. So the flag says
    // out loud what happens either way, and the note is what keeps it honest.
    if (!options.keepAccount && !g.json) {
        note(QStringLiteral("omahouse: the account %1 is untouched — omahouse never runs "
                            "userdel. Pass --keep-account to say so and lose this note.")
                 .arg(user));
    }
    // The days already counted stay too: a report is evidence, and it outlives
    // the rules it was collected under.
    return wrote(g, removed,
                 QStringLiteral("%1: profile removed. %2 is untouched, and so is everything "
                                "counted so far.")
                     .arg(user, paths::userStateDir(user)));
}

int cmdProfileEnforce(const Globals &g, const QStringList &positionals, const Options &options)
{
    if (positionals.size() != 1) {
        fail(QStringLiteral("profile enforce: which user? (try omahouse --help)"));
        return kUsage;
    }
    if (options.on == options.off) {
        fail(QStringLiteral("profile enforce: --on or --off, and one of them"));
        return kUsage;
    }
    const QString user = positionals.first();
    if (!mayWrite(QStringLiteral("profile enforce"), paths::profilesFile(),
                  paths::configDirIsTheSystems(), g))
        return kUsage;

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    Profile *profile = profileToChange(QStringLiteral("profile enforce"), &profiles, user,
                                       &status);
    if (!profile)
        return status;

    profile->enforce = options.on;
    if (!saveProfiles(QStringLiteral("profile enforce"), profiles.all))
        return kUsage;
    return wrote(g, *profile,
                 options.on
                     ? QStringLiteral("%1: enforcing — budgets now close and log out.").arg(user)
                     : QStringLiteral("%1: observing — it counts and reports, and closes "
                                      "nothing.")
                           .arg(user));
}

int cmdProfileDefault(const Globals &g, const QStringList &positionals, const Options &options)
{
    if (positionals.size() != 1) {
        fail(QStringLiteral("profile default: which user? (try omahouse --help)"));
        return kUsage;
    }
    if (options.allow == options.deny) {
        fail(QStringLiteral("profile default: --allow or --deny, and one of them"));
        return kUsage;
    }
    const QString user = positionals.first();
    if (!mayWrite(QStringLiteral("profile default"), paths::profilesFile(),
                  paths::configDirIsTheSystems(), g))
        return kUsage;

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    Profile *profile = profileToChange(QStringLiteral("profile default"), &profiles, user,
                                       &status);
    if (!profile)
        return status;

    profile->defaultVerdict = options.deny ? Verdict::Deny : Verdict::Allow;
    if (!saveProfiles(QStringLiteral("profile default"), profiles.all))
        return kUsage;
    // The same engine, said the way spec.md §8 asks the studio to say it: a list
    // of programs, and not a form of verdicts.
    return wrote(g, *profile,
                 options.deny
                     ? QStringLiteral("%1: only what is on the list runs. %2 rule%3 on it.")
                           .arg(user)
                           .arg(profile->rules.size())
                           .arg(profile->rules.size() == 1 ? QString() : QStringLiteral("s"))
                     : QStringLiteral("%1: everything runs except what is denied. %2 rule%3.")
                           .arg(user)
                           .arg(profile->rules.size())
                           .arg(profile->rules.size() == 1 ? QString() : QStringLiteral("s")));
}

int cmdProfile(const Globals &g, const QStringList &positionals, const Options &options)
{
    const QString subcommand = positionals.value(0);
    if (subcommand.isEmpty()) {
        fail(QStringLiteral("profile: add, remove, enforce, default, list or show? "
                            "(try omahouse --help)"));
        return kUsage;
    }
    if (subcommand == QLatin1String("list")) {
        if (!onlyTheseOptions(options, {}, QStringLiteral("profile list")))
            return kUsage;
        if (positionals.size() > 1) {
            fail(QStringLiteral("profile list: it takes no user; try omahouse profile show %1")
                     .arg(positionals.value(1)));
            return kUsage;
        }
        return cmdProfileList(g);
    }
    if (subcommand == QLatin1String("show")) {
        if (!onlyTheseOptions(options, {}, QStringLiteral("profile show")))
            return kUsage;
        return cmdProfileShow(g, positionals.mid(1));
    }
    if (subcommand == QLatin1String("add")) {
        if (!onlyTheseOptions(options, {QStringLiteral("--name"), QStringLiteral("--create-user")},
                              QStringLiteral("profile add")))
            return kUsage;
        return cmdProfileAdd(g, positionals.mid(1), options);
    }
    if (subcommand == QLatin1String("remove")) {
        if (!onlyTheseOptions(options, {QStringLiteral("--keep-account")},
                              QStringLiteral("profile remove")))
            return kUsage;
        return cmdProfileRemove(g, positionals.mid(1), options);
    }
    if (subcommand == QLatin1String("enforce")) {
        if (!onlyTheseOptions(options, {QStringLiteral("--on"), QStringLiteral("--off")},
                              QStringLiteral("profile enforce")))
            return kUsage;
        return cmdProfileEnforce(g, positionals.mid(1), options);
    }
    if (subcommand == QLatin1String("default")) {
        if (!onlyTheseOptions(options, {QStringLiteral("--allow"), QStringLiteral("--deny")},
                              QStringLiteral("profile default")))
            return kUsage;
        return cmdProfileDefault(g, positionals.mid(1), options);
    }
    fail(QStringLiteral("profile: unknown subcommand '%1' (try omahouse --help)")
             .arg(subcommand));
    return kUsage;
}

// -- allow, deny, limit, grant -----------------------------------------------

int cmdRule(const Globals &g, const QString &verb, const QStringList &positionals,
            const Options &options, Verdict verdict)
{
    if (positionals.size() != 2) {
        fail(QStringLiteral("%1: which user, and which app? (try omahouse --help)").arg(verb));
        return kUsage;
    }
    const QString user = positionals.at(0);
    const QString id = positionals.at(1);
    if (id.startsWith(QLatin1Char('-'))) {
        fail(QStringLiteral("%1: '%2' does not look like an app id").arg(verb, id));
        return kUsage;
    }

    // `allow ... --limit 45m` is sugar, and spec.md §7 says why: the rule and the
    // budget are one thought at the moment somebody is configuring, and making
    // them two commands is making the second one easy to forget.
    int minutes = 0;
    const bool limited = !options.limit.isEmpty();
    if (limited) {
        QString error;
        if (!minutesFromDuration(options.limit, &minutes, &error)) {
            fail(QStringLiteral("%1: --limit %2").arg(verb, error));
            return kUsage;
        }
    }

    if (!mayWrite(verb, paths::profilesFile(), paths::configDirIsTheSystems(), g))
        return kUsage;

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    Profile *profile = profileToChange(verb, &profiles, user, &status);
    if (!profile)
        return status;

    ruleFor(profile, id)->verdict = verdict;
    if (limited) {
        Budget *budget = budgetFor(profile, id);
        if (!budget) {
            // The budget of an app closes that app when it runs out. The one
            // that logs the session out is the session's, and it is written by
            // `omahouse limit --session`.
            profile->budgets.append(Budget {id, id, minutes, OnExhausted::Close});
        } else {
            budget->dailyMinutes = minutes;
        }
    }
    if (!saveProfiles(verb, profiles.all))
        return kUsage;

    if (verdict == Verdict::Allow)
        warnAboutWhatIsInside(user, id);

    QString line;
    if (verdict == Verdict::Deny) {
        line = QStringLiteral("%1: %2 is not allowed to run.").arg(user, id);
    } else if (limited) {
        line = QStringLiteral("%1: %2 allowed, %3 a day, and it closes when the time is out.")
                   .arg(user, id, durationFromMinutes(minutes));
    } else {
        line = QStringLiteral("%1: %2 allowed. No limit of its own, so it spends the session's.")
                   .arg(user, id);
    }
    return wrote(g, *profile, line);
}

int cmdLimit(const Globals &g, const QStringList &positionals, const Options &options)
{
    if (positionals.size() != 1) {
        fail(QStringLiteral("limit: which user? (try omahouse --help)"));
        return kUsage;
    }
    const QString user = positionals.first();

    const bool session = !options.session.isEmpty();
    const bool named = !options.budget.isEmpty();
    if (session == named) {
        fail(QStringLiteral("limit: --session 2h, or --budget minecraft=45m. One of them."));
        return kUsage;
    }

    // The session is the budget whose selector is `*` -- spec.md §2, and the
    // whole reason there is no branch for "the user's time" anywhere in the
    // core. It is spelled differently on the command line because that is how
    // people say it, and it is the same structure underneath.
    QString id = QStringLiteral("session");
    QString match = QStringLiteral("*");
    QString written = options.session;
    OnExhausted whenGone = OnExhausted::Logout;
    if (named) {
        const int equals = options.budget.indexOf(QLatin1Char('='));
        if (equals <= 0 || equals == options.budget.size() - 1) {
            fail(QStringLiteral("limit: --budget wants an id and a length of time, like "
                                "minecraft=45m, not '%1'")
                     .arg(options.budget));
            return kUsage;
        }
        id = options.budget.left(equals);
        match = id;
        written = options.budget.mid(equals + 1);
        whenGone = OnExhausted::Close;
    }

    int minutes = 0;
    QString error;
    if (!minutesFromDuration(written, &minutes, &error)) {
        fail(QStringLiteral("limit: %1").arg(error));
        return kUsage;
    }

    if (!mayWrite(QStringLiteral("limit"), paths::profilesFile(),
                  paths::configDirIsTheSystems(), g))
        return kUsage;

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    Profile *profile = profileToChange(QStringLiteral("limit"), &profiles, user, &status);
    if (!profile)
        return status;

    Budget *budget = budgetFor(profile, id);
    if (!budget) {
        profile->budgets.append(Budget {id, match, minutes, whenGone});
        budget = &profile->budgets.last();
    } else {
        // Only the number. What a budget does when it runs out is a decision
        // somebody made once, and a new limit is not a reason to take it back.
        budget->dailyMinutes = minutes;
    }
    if (!saveProfiles(QStringLiteral("limit"), profiles.all))
        return kUsage;

    return wrote(g, *profile,
                 QStringLiteral("%1: %2 %3 a day, and it %4 when the time is out.")
                     .arg(user, id, durationFromMinutes(minutes), whenOut(budget->onExhausted)));
}

int cmdGrant(const Globals &g, const QStringList &positionals, const Options &options)
{
    if (positionals.size() != 1) {
        fail(QStringLiteral("grant: which user? (try omahouse --help)"));
        return kUsage;
    }
    const QString user = positionals.first();

    const bool session = !options.session.isEmpty();
    const bool named = !options.budget.isEmpty();
    if (session == named) {
        fail(QStringLiteral("grant: --session 10m, or --budget minecraft=15m. One of them."));
        return kUsage;
    }

    QString id = QStringLiteral("session");
    QString written = options.session;
    if (named) {
        const int equals = options.budget.indexOf(QLatin1Char('='));
        if (equals <= 0 || equals == options.budget.size() - 1) {
            fail(QStringLiteral("grant: --budget wants an id and a length of time, like "
                                "minecraft=15m, not '%1'")
                     .arg(options.budget));
            return kUsage;
        }
        id = options.budget.left(equals);
        written = options.budget.mid(equals + 1);
    }

    int minutes = 0;
    QString error;
    if (!minutesFromDuration(written, &minutes, &error)) {
        fail(QStringLiteral("grant: %1").arg(error));
        return kUsage;
    }

    const QDate today = QDate::currentDate();
    if (!mayWrite(QStringLiteral("grant"), paths::ledgerFile(user, today),
                  paths::stateDirIsTheSystems(), g))
        return kUsage;

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    Profile *profile = profileToChange(QStringLiteral("grant"), &profiles, user, &status);
    if (!profile)
        return status;
    const Budget *budget = budgetFor(profile, id);
    if (!budget) {
        // Refused rather than invented: a grant against a budget nobody wrote is
        // time added to a counter the daemon will never look at, and it would
        // read on the report as though it had been given.
        fail(QStringLiteral("grant: %1 has no budget called %2; omahouse profile show %1 "
                            "lists them")
                 .arg(user, id));
        return kMissing;
    }

    Ledger ledger;
    bool missing = false;
    if (!loadLedger(user, today, &ledger, &missing, &status))
        return status;

    // The day's own file, so the minutes expire when the file does: spec.md §4
    // resets the balance at the local turn of the date, and a grant that
    // survived it would be tomorrow's time given away today.
    Grant grant;
    grant.at = QDateTime::currentDateTime();
    grant.by = operatorUser();
    grant.budget = id;
    grant.minutes = minutes;
    ledger.grants.append(grant);

    QString writeError;
    if (!writeLedger(paths::ledgerFile(user, today), ledger, &writeError)) {
        fail(QStringLiteral("grant: %1").arg(writeError));
        return kUsage;
    }

    const int granted = ledger.grantedSeconds(id);
    const int used = ledger.secondsFor(id);
    const bool limited = budget->hasLimit();
    const int limit = limited ? budget->dailyMinutes * 60 + granted : 0;
    const int left = limited ? qMax(0, limit - used) : 0;

    if (g.json) {
        QJsonObject document {
            {QStringLiteral("user"), user},
            {QStringLiteral("budget"), id},
            {QStringLiteral("minutes"), minutes},
            {QStringLiteral("by"), grant.by},
            {QStringLiteral("at"), grant.at.toString(Qt::ISODate)},
            {QStringLiteral("usedSeconds"), used},
            {QStringLiteral("grantedSeconds"), granted},
        };
        document.insert(QStringLiteral("limitSeconds"),
                        limited ? QJsonValue(limit) : QJsonValue());
        document.insert(QStringLiteral("leftSeconds"), limited ? QJsonValue(left) : QJsonValue());
        printJson(document);
        return kOk;
    }

    out() << QStringLiteral("%1: +%2 of %3, from %4.")
                 .arg(user, durationFromMinutes(minutes), id, grant.by);
    if (limited)
        out() << QStringLiteral(" %1 left today.").arg(humanDuration(left));
    else
        out() << QStringLiteral(" %1 has no limit, so it was only written down.").arg(id);
    out() << '\n';
    return kOk;
}

// -- the screen --------------------------------------------------------------

void printHelp()
{
    out() << "omahouse " OMAHOUSE_VERSION R"( — house rules for the accounts on this machine

Usage: omahouse <command> [options]

Reading, and no privilege needed:
  status [user]            live app scopes, what they are, and what is left
                           without a profile it scans and reports, nothing else
  report <user>            the day's ledger
         [--since <date>]  every day from that one, and a total
  profile list             who is under rules
  profile show <user>      the rules, the budgets and what happens when they end

Writing, and root needed — the studio gets there by pkexec:
  profile add <user>       a new profile: observing, and allowing everything
              [--name "Júlia"] [--create-user]
  profile remove <user> [--keep-account]
  profile enforce <user> --on | --off
  profile default <user> --allow | --deny
  allow <user> <app> [--limit 45m]   let it run, and put it on the clock
  deny  <user> <app>                 do not let it run
  limit <user> --session 2h | --budget minecraft=45m
  grant <user> --session 10m | --budget minecraft=15m

An app is named by the id of its scope: `chromium`, `org.freedesktop.Platform`.
`omahouse status` lists the ones that are open, and says when a scope holds
something other than what its name says.

A length of time is 45m, 2h, 1h30m, or a bare 90 for minutes. Anything else is
refused rather than taken for minutes.

Globals:
  --json, -j               one JSON document on stdout   (OMAHOUSE_JSON)
  --version, -V
  --help, -h

Files:
  /etc/omahouse/profiles.json          who is under rules   (OMAHOUSE_CONFIG_DIR)
  /var/lib/omahouse/<user>/<date>.json the day's ledger     (OMAHOUSE_STATE_DIR)
  /sys/fs/cgroup                       the scopes           (OMAHOUSE_CGROUP_ROOT)
  /proc                                what a scope runs    (OMAHOUSE_PROC_ROOT)

`watch`, the daemon that counts and closes, arrives with stage 6 of plan.md.

Exit: 0 it answered, 1 a usage error or a file it could not read, 2 no such
account or no such profile.
)";
}

int dispatch(const Globals &g, const QStringList &args)
{
    Options options;
    QStringList rest;
    if (!parseOptions(args, &options, &rest))
        return kUsage;

    const QString verb = rest.value(0);
    const QStringList positionals = rest.mid(1);

    if (verb == QLatin1String("status")) {
        if (!onlyTheseOptions(options, {}, verb))
            return kUsage;
        return cmdStatus(g, positionals);
    }
    if (verb == QLatin1String("report")) {
        if (!onlyTheseOptions(options, {QStringLiteral("--since")}, verb))
            return kUsage;
        return cmdReport(g, positionals, options.since);
    }
    if (verb == QLatin1String("profile"))
        return cmdProfile(g, positionals, options);
    if (verb == QLatin1String("allow")) {
        if (!onlyTheseOptions(options, {QStringLiteral("--limit")}, verb))
            return kUsage;
        return cmdRule(g, verb, positionals, options, Verdict::Allow);
    }
    if (verb == QLatin1String("deny")) {
        if (!onlyTheseOptions(options, {}, verb))
            return kUsage;
        return cmdRule(g, verb, positionals, options, Verdict::Deny);
    }
    if (verb == QLatin1String("limit")) {
        if (!onlyTheseOptions(options, {QStringLiteral("--session"), QStringLiteral("--budget")},
                              verb))
            return kUsage;
        return cmdLimit(g, positionals, options);
    }
    if (verb == QLatin1String("grant")) {
        if (!onlyTheseOptions(options, {QStringLiteral("--session"), QStringLiteral("--budget")},
                              verb))
            return kUsage;
        return cmdGrant(g, positionals, options);
    }
    // The verb spec.md §7 declares and stage 6 of plan.md builds. Named, rather
    // than met with "unknown command", because it is not a typo.
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
    // Kept whole, globals and all, because the line a refusal suggests running
    // under `pkexec` has to be the line that was typed and not a reconstruction
    // of it.
    g.commandLine = args;
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
