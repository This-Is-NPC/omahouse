#include "Duration.h"
#include "Ledger.h"
#include "Notify.h"
#include "Paths.h"
#include "Proc.h"
#include "Profile.h"
#include "Users.h"
#include "Version.h"
#include "Watch.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>

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
    QString interval;
    bool createUser = false;
    bool keepAccount = false;
    bool on = false;
    bool off = false;
    bool allow = false;
    bool deny = false;
    bool once = false;
    bool dryRun = false;
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
        {"--interval", &Options::interval, "a number of seconds, like 2"},
    };
    struct Flag {
        const char *name;
        bool Options::*field;
    };
    static const Flag flags[] = {
        {"--create-user", &Options::createUser}, {"--keep-account", &Options::keepAccount},
        {"--on", &Options::on},                  {"--off", &Options::off},
        {"--allow", &Options::allow},            {"--deny", &Options::deny},
        {"--once", &Options::once},              {"--dry-run", &Options::dryRun},
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

// How many units of either block below get a line of their own. Dozens of tmux
// panes is what this machine has, and a status screen that is forty-six lines of
// uuid is a status screen nobody reads; `--json` carries all of them.
constexpr int kListed = 5;

/// The live scopes under `app.slice` that omahouse counts and cannot name.
///
/// This block used to sit under `Out of reach` and that was the wrong shelf: a
/// `tmux-spawn-<uuid>.scope` is not out of reach at all. Its processes are
/// counted, and every budget whose selector is `*` -- the session -- is debited
/// for it, because somebody who has been in a terminal all afternoon is somebody
/// using the machine. What the missing id really costs is the two things that
/// need a name: a limit of its own, and being let through by name. So the
/// verdict falls to the profile's default, and the paragraph says which one that
/// is, because under `deny` with the teeth in that is a scope being closed.
void printCountedNotNamed(const QVector<AppScope> &unnamed, const Profile *profile)
{
    if (unnamed.isEmpty())
        return;

    int processes = 0;
    for (const AppScope &scope : unnamed)
        processes += scope.pidCount;

    out() << "\nCounted, not named\n";
    out() << QStringLiteral("  %1 scope%2 under app.slice omahouse could not name, "
                            "holding %3 process%4:\n")
                 .arg(unnamed.size())
                 .arg(unnamed.size() == 1 ? QString() : QStringLiteral("s"))
                 .arg(processes)
                 .arg(processes == 1 ? QString() : QStringLiteral("es"));
    for (int i = 0; i < unnamed.size() && i < kListed; ++i) {
        out() << QStringLiteral("    %1  %2\n")
                     .arg(unnamed.at(i).pidCount, 5)
                     .arg(unnamed.at(i).unit);
    }
    if (unnamed.size() > kListed) {
        out() << QStringLiteral("    … and %1 more (--json lists them all)\n")
                     .arg(unnamed.size() - kListed);
    }
    out() << "  These are in the total: a scope with processes in it is somebody using\n"
             "  the machine, so every budget whose match is `*` — the session — is\n"
             "  debited for them. What the missing name costs is the two things that\n"
             "  need one: they cannot be given a limit of their own, and they cannot be\n"
             "  allowed by name.\n";
    if (!profile) {
        out() << "  With a profile, the verdict on them would be its default.\n";
    } else if (profile->defaultVerdict == Verdict::Allow) {
        out() << "  No rule can name them, so they take the default verdict, allow.\n";
    } else if (profile->enforce) {
        out() << "  No rule can name them, so they take the default verdict, deny — and\n"
                 "  the teeth are in, so they are closed. That is the intended reading of\n"
                 "  an allowlist: something nobody can name is not on it.\n";
    } else {
        out() << "  No rule can name them, so they take the default verdict, deny. Nothing\n"
                 "  is closed while this profile is only observing.\n";
    }
}

/// What is in `session.slice`, where omahouse cannot look -- spec.md §5.
///
/// Printed whether or not there is a profile, and phrased as a fact rather than
/// as an alarm. On a live graphical session the number is never zero: Hyprland's
/// own processes are in it, and omahouse cannot tell them from an app somebody
/// started with a raw `exec`. Saying so is the point. Pretending the number is a
/// clean zero would be the lie.
void printOutOfReach(const QVector<SessionUnit> &session, int sessionProcesses)
{
    if (session.isEmpty())
        return;

    out() << "\nOut of reach\n";
    out() << QStringLiteral("  %1 process%2 in session.slice omahouse can neither count "
                            "nor close:\n")
                 .arg(sessionProcesses)
                 .arg(sessionProcesses == 1 ? QString() : QStringLiteral("es"));
    for (int i = 0; i < session.size() && i < kListed; ++i) {
        out() << QStringLiteral("    %1  %2\n")
                     .arg(session.at(i).pidCount, 5)
                     .arg(session.at(i).unit);
    }
    if (session.size() > kListed) {
        out() << QStringLiteral("    … and %1 more (--json lists them all)\n")
                     .arg(session.size() - kListed);
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

    // About as many as the blocks below name, and for the same reason: a screen
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
    printCountedNotNamed(unnamed, profile);
    printOutOfReach(sessionUnits, sessionProcesses);
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

// -- watch -------------------------------------------------------------------
//
// The loop of spec.md §5, and the one verb of this build that keeps running.
// The work itself is `Watch` in src/sys, because counting is reading the machine
// and warning is speaking to it; what is here is the command line, the screen
// and the journal.
//
// Three things make it safe to try on a machine nobody meant to fiscalise, and
// all three are asked for by plan.md, which puts stages 6 and 7 in the VM:
// `--dry-run` decides and touches nothing, `--once` is a single cycle with no
// loop behind it, and the four roots move by variable exactly as they do for
// every other verb.
//
// Since stage 7 the fourth is the one that matters, and it is not a flag. A
// `Close` is refused unless the cgroup tree really is `/sys/fs/cgroup`, and a
// `terminate-user` is refused unless the configuration really is
// `/etc/omahouse` -- so a run pointed at a tree of its own reads it, counts it,
// prints what it would have closed, and closes nothing. `Enforce.h` has both
// questions and why they are the right ones.

/// The slowest tick worth allowing. An hour between cycles is an hour of
/// somebody's afternoon debited as one tick, which is not accounting; the number
/// is here to catch `--interval 3600` typed for something else, not to be used.
constexpr int kSlowestTick = 3600;

/// One line for the journal: when, who, and what happened.
///
/// stderr, because stdout carries the `--json` stream and a person reading the
/// journal and a script reading the documents must not have to share a channel.
void say(const QDateTime &at, const QString &user, const QString &line)
{
    err() << at.toString(Qt::ISODate) << ' ' << user << ": " << line << '\n';
}

/// What one user's cycle was, in one sentence.
QString whatHappened(const Watched &watched)
{
    if (!watched.error.isEmpty())
        return watched.error;
    if (!watched.enabled)
        return QStringLiteral("the profile is switched off; not counting");
    if (!watched.account)
        return QStringLiteral("no account of that name on this machine");
    if (!watched.session)
        return QStringLiteral("not logged in; nothing to count");

    // The scopes with no id are said out loud rather than left out of the count.
    // They are what the session budget is being spent on when a person has been
    // in a terminal all afternoon, and a line reading `no apps` over a ledger
    // that gained an hour is the journal contradicting the accounting.
    const QString named = watched.apps.isEmpty()
        ? QStringLiteral("no named apps")
        : QStringLiteral("%1 app%2 (%3)")
              .arg(watched.apps.size())
              .arg(watched.apps.size() == 1 ? QString() : QStringLiteral("s"),
                   watched.apps.join(QStringLiteral(", ")));
    const QString apps = watched.unnamedScopes == 0
        ? (watched.apps.isEmpty() ? QStringLiteral("no apps") : named)
        : QStringLiteral("%1 and %2 scope%3 it cannot name")
              .arg(named)
              .arg(watched.unnamedScopes)
              .arg(watched.unnamedScopes == 1 ? QString() : QStringLiteral("s"));
    const QString clock = watched.debited.isEmpty()
        ? QStringLiteral("nothing on the clock")
        : QStringLiteral("counting %1").arg(watched.debited.join(QStringLiteral(", ")));
    return QStringLiteral("%1, %2").arg(apps, clock);
}

/// How a notification went, as a parenthesis after it.
QString howItWent(const Said &said, bool dryRun)
{
    if (dryRun)
        return QStringLiteral(" (dry run: not sent)");
    if (said.sent)
        return {};
    return QStringLiteral(" (not sent: %1)").arg(said.error);
}

/// What one act of the teeth was, in one sentence.
///
/// Every one of them gets a line, always, whether it happened or not. These are
/// the two things omahouse does that take something away from somebody, and a
/// program that closes a window without a word in the journal is a program
/// nobody can hold to account afterwards.
QString whatWasDone(const Done &done)
{
    const QString about = done.unit.isEmpty()
        ? QString()
        : QStringLiteral(" %1").arg(done.app.isEmpty() ? done.unit
                                                       : QStringLiteral("%1 (%2)")
                                                             .arg(done.app, done.unit));
    QString what;
    switch (done.what) {
    case Done::What::Terminate:
        what = QStringLiteral("SIGTERM into%1").arg(about);
        break;
    case Done::What::Kill:
        what = QStringLiteral("cgroup.kill on%1").arg(about);
        break;
    case Done::What::Block:
        what = QStringLiteral("refused at the next login (%1)").arg(paths::blockedFile());
        break;
    case Done::What::Unblock:
        what = QStringLiteral("let back in (%1)").arg(paths::blockedFile());
        break;
    case Done::What::EndSession:
        what = QStringLiteral("loginctl terminate-user");
        break;
    }
    if (done.carriedOut)
        return what;
    return QStringLiteral("%1 — not done: %2")
        .arg(what, done.error.isEmpty() ? QStringLiteral("dry run") : done.error);
}

/// The journal of one cycle: a line for anything that changed, and a line for
/// everything said.
///
/// Silence is the ordinary state. A tick that finds the same apps open and the
/// same budgets running writes nothing at all, because a two second loop that
/// logs every cycle puts seventeen hundred identical lines a day into the
/// journal and buries the one line that mattered.
void logCycle(const Cycle &cycle)
{
    for (const Watched &watched : cycle.users) {
        if (watched.worthSaying)
            say(cycle.at, watched.user, whatHappened(watched));
        for (const Said &said : watched.said) {
            say(cycle.at, watched.user,
                QStringLiteral("%1: %2 — %3%4")
                    .arg(decisionReasonName(said.decision.reason), said.words.summary,
                         said.words.body, howItWent(said, cycle.dryRun)));
        }
        for (const Done &done : watched.done)
            say(cycle.at, watched.user, whatWasDone(done));
    }
    if (!cycle.blockedError.isEmpty()) {
        // Worth a line every cycle and not only when it changes: a `blocked`
        // that cannot be read is a `blocked` refusing nobody, and the machine
        // goes on looking fiscalised while it is not.
        say(cycle.at, QStringLiteral("omahouse"), cycle.blockedError);
    }
    err().flush();
}

/// One cycle on the screen, for somebody who typed `--once` and wants the
/// accounting rather than a log line.
void printCycle(const Cycle &cycle, const Profiles &profiles)
{
    out() << QStringLiteral("omahouse watch — one cycle at %1, counting %2s\n")
                 .arg(cycle.at.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                 .arg(cycle.tickSeconds);
    if (cycle.dryRun)
        out() << "dry run: nothing was written, and nothing was said\n";

    for (const Watched &watched : cycle.users) {
        out() << '\n' << watched.user;
        if (!watched.displayName.isEmpty())
            out() << QStringLiteral(" (%1)").arg(watched.displayName);
        out() << '\n';
        out() << QStringLiteral("  %1\n").arg(whatHappened(watched));

        if (!watched.session || !watched.error.isEmpty() || !watched.enabled
            || !watched.account) {
            continue;
        }

        const QString path = paths::ledgerFile(watched.user, cycle.at.date());
        out() << QStringLiteral("  %1 — %2\n")
                     .arg(path,
                          watched.wrote ? QStringLiteral("written")
                                        : (cycle.dryRun ? QStringLiteral("left alone")
                                                        : QStringLiteral("nothing to change")));

        const Profile *profile = profileFor(profiles, watched.user);
        const QVector<Balance> balances =
            profile ? balancesOf(*profile, watched.ledger) : QVector<Balance>();
        if (!balances.isEmpty()) {
            out() << '\n';
            printBalances(balances);
        }
        for (const Said &said : watched.said) {
            out() << QStringLiteral("\n  %1 — %2%3\n")
                         .arg(said.words.summary, said.words.body,
                              howItWent(said, cycle.dryRun));
        }
        for (const Done &done : watched.done)
            out() << QStringLiteral("  %1\n").arg(whatWasDone(done));
    }
    if (!cycle.blockedError.isEmpty())
        out() << QStringLiteral("\n%1\n").arg(cycle.blockedError);
}

QJsonObject cycleToJson(const Cycle &cycle, const Profiles &profiles)
{
    QJsonArray users;
    for (const Watched &watched : cycle.users) {
        QJsonArray said;
        for (const Said &one : watched.said) {
            QJsonObject object {
                {QStringLiteral("reason"), decisionReasonName(one.decision.reason)},
                {QStringLiteral("budget"), one.decision.budgetId},
                {QStringLiteral("scope"), one.decision.scopeUnit},
                {QStringLiteral("app"), one.app},
                {QStringLiteral("secondsLeft"), one.decision.secondsLeft},
                {QStringLiteral("summary"), one.words.summary},
                {QStringLiteral("body"), one.words.body},
                {QStringLiteral("sent"), one.sent},
            };
            object.insert(QStringLiteral("error"),
                          one.error.isEmpty() ? QJsonValue() : QJsonValue(one.error));
            said.append(object);
        }
        QJsonArray done;
        for (const Done &one : watched.done) {
            QJsonObject object {
                {QStringLiteral("what"), doneWhatName(one.what)},
                {QStringLiteral("kind"), decisionKindName(one.decision.kind)},
                {QStringLiteral("reason"), decisionReasonName(one.decision.reason)},
                {QStringLiteral("budget"), one.decision.budgetId},
                {QStringLiteral("scope"), one.unit},
                {QStringLiteral("app"), one.app},
                {QStringLiteral("carriedOut"), one.carriedOut},
            };
            object.insert(QStringLiteral("error"),
                          one.error.isEmpty() ? QJsonValue() : QJsonValue(one.error));
            done.append(object);
        }
        const Profile *profile = profileFor(profiles, watched.user);
        QJsonObject object {
            {QStringLiteral("user"), watched.user},
            {QStringLiteral("uid"), static_cast<qint64>(watched.uid)},
            {QStringLiteral("account"), watched.account},
            {QStringLiteral("enabled"), watched.enabled},
            {QStringLiteral("session"), watched.session},
            {QStringLiteral("apps"), QJsonArray::fromStringList(watched.apps)},
            // Not in `apps`, and not left out either: a scope with no id has no
            // word to go in that list and is counted like everything else.
            {QStringLiteral("unnamedScopes"), watched.unnamedScopes},
            {QStringLiteral("debited"), QJsonArray::fromStringList(watched.debited)},
            {QStringLiteral("wrote"), watched.wrote},
            {QStringLiteral("said"), said},
            // What the teeth did, and what they were refused. A refusal carries
            // its reason, which is the sentence somebody reading a `close` that
            // did not happen actually needs.
            {QStringLiteral("done"), done},
            {QStringLiteral("blocked"), watched.blocked},
            {QStringLiteral("budgets"),
             profile ? balancesToJson(balancesOf(*profile, watched.ledger)) : QJsonArray()},
        };
        object.insert(QStringLiteral("error"),
                      watched.error.isEmpty() ? QJsonValue() : QJsonValue(watched.error));
        users.append(object);
    }
    QJsonObject document {
        {QStringLiteral("at"), cycle.at.toString(Qt::ISODate)},
        {QStringLiteral("tickSeconds"), cycle.tickSeconds},
        {QStringLiteral("dryRun"), cycle.dryRun},
        {QStringLiteral("blocked"), QJsonArray::fromStringList(cycle.blocked)},
        {QStringLiteral("users"), users},
    };
    document.insert(QStringLiteral("blockedError"),
                    cycle.blockedError.isEmpty() ? QJsonValue()
                                                 : QJsonValue(cycle.blockedError));
    return document;
}

// -- stopping when told ------------------------------------------------------

int g_stopPipe[2] = {-1, -1};

/// One byte down a pipe, and nothing else.
///
/// A signal handler may call almost nothing -- not `QCoreApplication::quit`, not
/// anything that takes a lock -- so it writes a byte and the event loop does the
/// rest. Which means the cycle in flight finishes first: the ledger is written
/// by a single `rename`, and a SIGTERM in the middle of a cycle leaves the day
/// whole either way.
void onStopSignal(int number)
{
    const char byte = static_cast<char>(number);
    const ssize_t written = ::write(g_stopPipe[1], &byte, 1);
    static_cast<void>(written);
}

/// SIGINT and SIGTERM end the loop between cycles. False if the pipe could not
/// be made, and then the default disposition still ends the process -- less
/// tidily, and without a last flush.
bool stopWhenTold(QObject *guard)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, g_stopPipe) != 0)
        return false;
    auto *notifier = new QSocketNotifier(g_stopPipe[0], QSocketNotifier::Read, guard);
    QObject::connect(notifier, &QSocketNotifier::activated, guard,
                     [] { QCoreApplication::quit(); });
    std::signal(SIGINT, onStopSignal);
    std::signal(SIGTERM, onStopSignal);
    return true;
}

int cmdWatch(const Globals &g, const QStringList &positionals, const Options &options)
{
    if (!positionals.isEmpty()) {
        fail(QStringLiteral("watch: it watches every profile there is and takes no user; "
                            "omahouse status %1 answers about one")
                 .arg(positionals.first()));
        return kUsage;
    }

    int interval = 2;
    if (!options.interval.isEmpty()) {
        bool ok = false;
        interval = options.interval.toInt(&ok);
        if (!ok || interval < 1 || interval > kSlowestTick) {
            fail(QStringLiteral("watch: --interval wants whole seconds between 1 and %1, "
                                "not '%2'")
                     .arg(kSlowestTick)
                     .arg(options.interval));
            return kUsage;
        }
    }

    // Two files now, and both are asked for before anything is read. The day's
    // ledger under /var/lib, and -- since the teeth went in -- the
    // /etc/omahouse/blocked of spec.md §2, which is the half of `logout` that
    // does the work. A dry run asks for neither, which is what lets anybody
    // point the roots at a directory of their own and watch one cycle of their
    // own session.
    if (!options.dryRun
        && (!mayWrite(QStringLiteral("watch"), paths::stateDir(), paths::stateDirIsTheSystems(),
                      g)
            || !mayWrite(QStringLiteral("watch"), paths::blockedFile(),
                         paths::configDirIsTheSystems(), g))) {
        return kUsage;
    }

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    // Nobody to watch is not a loop with nothing in it. plan.md is plain about
    // this: a daemon spinning every two seconds over an empty list is a fan
    // running for nothing, and the answer somebody wants is the sentence that
    // says so.
    if (profiles.all.isEmpty()) {
        note(profiles.missing
                 ? QStringLiteral("omahouse: there is no %1 yet, so nobody is under rules "
                                  "and there is nothing to watch")
                       .arg(paths::profilesFile())
                 : QStringLiteral("omahouse: %1 holds no profiles, so there is nothing to "
                                  "watch")
                       .arg(paths::profilesFile()));
        return kOk;
    }

    const Proc proc;
    DesktopNotifier notifier;
    // The teeth are handed in and not switched on: what decides whether anything
    // is closed is the profile's `enforce`, and what decides whether it may be
    // is `Enforce.h`, which asks about the trees the paths are in rather than
    // about a flag on this command line. A flag would be a thing somebody could
    // pass by accident on the wrong machine.
    MachineEnforcer enforcer;
    Watch::Options watching;
    watching.tickSeconds = interval;
    watching.dryRun = options.dryRun;
    Watch watch(&proc, &notifier, &enforcer, watching);

    const auto cycle = [&](const Profiles &current) {
        // The clock enters here and nowhere else. `evaluate` takes `now` by
        // parameter for exactly this reason, and it is what lets a two hour
        // budget be proved in microseconds by a test that never touches the
        // clock of the machine it runs on.
        const Cycle done = watch.tick(current.all, QDateTime::currentDateTime());
        logCycle(done);
        if (g.json)
            printJson(cycleToJson(done, current));
        else if (options.once)
            printCycle(done, current);
        out().flush();
    };

    if (options.once) {
        cycle(profiles);
        return kOk;
    }

    QObject guard;
    if (!stopWhenTold(&guard)) {
        note(QStringLiteral("omahouse: could not arrange to stop on a signal; a SIGTERM will "
                            "end this run abruptly rather than after the cycle it is in"));
    }

    // Read again every cycle, and not held from here. spec.md §1: the operator
    // hands over ten minutes with the game still running, and a daemon holding a
    // copy of the rules from when it started cannot honour that. A file that
    // stops parsing is complained about once and the loop keeps its last good
    // reading of it, because a broken profiles.json must not read as nobody
    // being under rules.
    QString complaint;
    Profiles current = profiles;
    QTimer timer(&guard);
    timer.setInterval(interval * 1000);
    QObject::connect(&timer, &QTimer::timeout, &guard, [&] {
        Profiles read;
        QString error;
        if (readProfiles(paths::profilesFile(), &read.all, &error, &read.missing)) {
            if (!complaint.isEmpty()) {
                note(QStringLiteral("omahouse: %1 reads again").arg(paths::profilesFile()));
                complaint.clear();
            }
            current = read;
        } else if (error != complaint) {
            complaint = error;
            fail(QStringLiteral("watch: %1 (carrying on with the last reading of it)")
                     .arg(error));
            err().flush();
        }
        cycle(current);
    });

    note(QStringLiteral("omahouse: watching %1 profile%2, a cycle every %3s%4")
             .arg(profiles.all.size())
             .arg(profiles.all.size() == 1 ? QString() : QStringLiteral("s"))
             .arg(interval)
             .arg(options.dryRun ? QStringLiteral(" — dry run, nothing is written or said")
                                 : QString()));
    // The first cycle now rather than one interval from now: a daemon that says
    // nothing for its first two seconds is a daemon nobody can tell from one
    // that failed to start.
    cycle(current);
    timer.start();
    return QCoreApplication::exec() == 0 ? kOk : kUsage;
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

The loop, which is the only verb that keeps running:
  watch [--interval 2] [--once] [--dry-run]
                           count what every profile has open, warn before the
                           time is out, and then close it. --dry-run decides
                           and touches nothing, and --once is a single cycle

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
  /etc/omahouse/blocked                who may not log in   (OMAHOUSE_CONFIG_DIR)
  /var/lib/omahouse/<user>/<date>.json the day's ledger     (OMAHOUSE_STATE_DIR)
  /sys/fs/cgroup                       the scopes           (OMAHOUSE_CGROUP_ROOT)
  /proc                                what a scope runs    (OMAHOUSE_PROC_ROOT)
  notify-send                          how a warning is said (OMAHOUSE_NOTIFY_SEND)
  systemd-run                          how it reaches a session (OMAHOUSE_SYSTEMD_RUN)
  loginctl                             how a session is ended (OMAHOUSE_LOGINCTL)

`watch` counts, warns, and then acts. Closing is SIGTERM into the app's scope
and then its cgroup.kill; `session.slice` is never touched, so the compositor
survives. Logging out is the name in /etc/omahouse/blocked and then `loginctl
terminate-user` — one action, never half of it, because a machine with autologin
hands the session straight back otherwise. The name comes out on its own when
the day turns, when time is granted, or when enforcement goes off.

Neither happens against a tree that is not this machine's: a cgroup root that is
not /sys/fs/cgroup is never signalled, and a configuration that is not
/etc/omahouse never ends a session.

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
    if (verb == QLatin1String("watch")) {
        if (!onlyTheseOptions(options,
                              {QStringLiteral("--interval"), QStringLiteral("--once"),
                               QStringLiteral("--dry-run")},
                              verb))
            return kUsage;
        return cmdWatch(g, positionals, options);
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
