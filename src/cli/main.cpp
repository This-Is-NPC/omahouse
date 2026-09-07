#include "Chromium.h"
#include "Allocation.h"
#include "Json.h"
#include <QLockFile>
#include <QUuid>
#include "Duration.h"
#include "Focus.h"
#include "FocusFile.h"
#include "Kind.h"
#include "Ledger.h"
#include "NodeConfig.h"
#include "Notify.h"
#include "Omakure.h"
#include "Fleet.h"
#include "Furniture.h"
#include "FurnitureFile.h"
#include "Pairing.h"
#include "Paths.h"
#include "Presence.h"
#include "Proc.h"
#include "Profile.h"
#include "Users.h"
#include "Version.h"
#include "Watch.h"
#include "WebPolicy.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QSocketNotifier>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
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
    /// A domain and a length of time. `limit`'s third shape, beside `--session`
    /// and `--budget`: the same verb, because a limit on YouTube and a limit on
    /// Minecraft are the same thing an operator is doing -- docs/design.md §2's
    /// Budget with the other kind of selector.
    QString site;
    QString interval;
    /// The Omakure node identity of a machine, and where its wire answers.
    /// Both are optional: a household can write a computer down before it is
    /// paired, which is the state between owning it and reaching it.
    QString node;
    QString at;
    /// The one line the other machine printed. `--invite` is what a machine
    /// being prepared is handed, `--pair` is what comes back; two names rather
    /// than one because the direction is the whole meaning, and a line pasted
    /// the wrong way round has to be refused by the flag rather than by a trust
    /// registry an hour later.
    QString invite;
    QString pair;
    /// The Battery whose scripts a prepared machine will run when cued. Named
    /// rather than assumed, because it is the widest thing pairing turns on.
    QString battery;
    /// Which day. Only `day` takes it, and it exists because a machine that was
    /// off at midnight still owes the house yesterday. `--date` and not `--on`:
    /// `profile enforce --on` already means something else, and one flag name
    /// with two meanings is a flag somebody gets wrong once.
    QString date;
    /// How long `watch` should keep going before it stops of its own accord.
    ///
    /// The middle ground between `--once`, which measures a single instant, and
    /// running forever, which needs somebody to own the process. A bounded run
    /// is what lets the loop be a scheduled job: something starts it every
    /// minute, it works for that minute, and it ends. A tick that jams dies with
    /// the minute it was in instead of jamming for a day, and what restarts it
    /// is the schedule rather than `Restart=always`.
    QString forSeconds;
    bool createUser = false;
    bool keepAccount = false;
    bool on = false;
    bool off = false;
    bool allow = false;
    bool deny = false;
    bool once = false;
    bool dryRun = false;
    /// `profile default` says `--allow | --deny` about programs. The web half
    /// says the same thing about sites, and it says it in the words the studio
    /// would use rather than in verdicts -- docs/design.md §8 -- because
    /// "everything but the list" and "only the list" is how somebody thinks
    /// about a browser.
    bool allButListed = false;
    bool onlyListed = false;
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
        {"--site", &Options::site, "a domain and a length of time, like youtube.com=30m"},
        {"--interval", &Options::interval, "a number of seconds, like 2"},
        {"--node", &Options::node, "an Omakure node id, like omk1_1c6eeda142"},
        {"--at", &Options::at, "where its wire answers, like 192.168.1.20:7879"},
        {"--for", &Options::forSeconds, "a number of seconds, like 60"},
        {"--invite", &Options::invite, "the line the operator's machine printed"},
        {"--pair", &Options::pair, "the line the prepared machine printed"},
        {"--battery", &Options::battery, "a Battery name, like omahouse"},
        {"--date", &Options::date, "a date like 2026-09-06"},
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
        {"--all-but-listed", &Options::allButListed},
        {"--only-listed", &Options::onlyListed},
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
/// user rather than as the name of an enum. docs/design.md §8 asks the same of the
/// studio: the screen talks about programs and minutes.
QString whenOut(OnExhausted action)
{
    switch (action) {
    case OnExhausted::Close:
        return QStringLiteral("closes");
    case OnExhausted::Logout:
        return QStringLiteral("logs out");
    case OnExhausted::Block:
        return QStringLiteral("stops opening");
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

/// The sites a budget has run out on today, across every profile.
///
/// Worked out exactly the way `watch` works it out -- `evaluate` over today's
/// ledger with a tick of nothing, which is the same zero-tick question §2's
/// `blocked` is re-derived by -- so that the CLI and the daemon cannot come to
/// disagree about what the browser's policy file should say. Without it every
/// verb that saves a profile would write a policy with today's blocks missing
/// and the daemon would put them back two seconds later, which is a site that
/// flickers open every time somebody types a command.
///
/// A ledger that will not parse is skipped and not guessed at, for the reason
/// `Watch::observe` skips it: no block stands on a day nobody can read, and that
/// is the recoverable direction.
QStringList sitesOutOfTime(const QVector<Profile> &profiles)
{
    const QDateTime now = QDateTime::currentDateTime();
    QStringList sites;
    for (const Profile &profile : profiles) {
        if (!profile.enabled)
            continue;
        Ledger ledger;
        QString error;
        bool missing = false;
        if (!readLedger(paths::ledgerFile(profile.user, now.date()), &ledger, &error, &missing))
            continue;
        if (missing) {
            ledger.user = profile.user;
            ledger.date = now.date();
        }
        for (const Decision &decision : evaluate(profile, {}, ledger, now, 0).decisions) {
            if (decision.kind == Decision::Kind::Block && !decision.site.isEmpty())
                sites.append(decision.site);
        }
    }
    sites.sort();
    sites.erase(std::unique(sites.begin(), sites.end()), sites.end());
    return sites;
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
            balance.limitSeconds = allowanceSeconds(profile, budget, ledger,
                ledger.date.isValid() ? ledger.date : QDate::currentDate());
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

// -- presence -----------------------------------------------------------------

/// The day's presence, by reason, in the order the ledger holds it.
///
/// Its own object beside `budgets` and never inside it, exactly as the ledger
/// keeps it: a reader that walked the budgets and found `screen-off` among them
/// would be a reader that had been told a screen is a budget.
QJsonObject presenceToJson(const Ledger &ledger)
{
    QJsonObject object;
    for (auto it = ledger.presence.constBegin(); it != ledger.presence.constEnd(); ++it)
        object.insert(it.key(), it.value());
    return object;
}

/// What the day's presence adds up to, in words: `12m using, 5m screen-off`.
QString presenceToday(const Ledger &ledger)
{
    QStringList parts;
    for (auto it = ledger.presence.constBegin(); it != ledger.presence.constEnd(); ++it)
        parts.append(QStringLiteral("%1 %2").arg(humanDuration(it.value()), it.key()));
    return parts.join(QStringLiteral(", "));
}

/// Presence, said out loud on the screen an operator reads.
///
/// Four lines at most, and the last of them is the one that matters: this is
/// measured and reported and it takes nothing away from anybody. Somebody
/// reading `away: every connected screen is off` beside a budget that is still
/// being spent has to be told, on the same screen, that the two are not
/// connected -- otherwise the obvious reading is that omahouse has stopped
/// counting, and it has not.
void printPresence(const QString &user, const Presence &presence, const SeatReading &seat,
                   const Ledger &ledger)
{
    out() << "\nPresence\n";
    out() << QStringLiteral("  %1 is %2.\n").arg(user, presenceSentence(presence));
    const QString whatTheSeatIs =
        seat.read ? (seat.occupied ? QStringLiteral("showing uid %1")
                                         .arg(static_cast<qulonglong>(seat.uid))
                                   : QStringLiteral("empty"))
                  : QStringLiteral("unreadable");
    out() << QStringLiteral("  Screen %1, seat %2 — read from the kernel's DRM connectors and "
                            "from\n  logind, never from %3's own compositor.\n")
                 .arg(screenStateName(seat.screen), whatTheSeatIs, user);
    const QString today = presenceToday(ledger);
    if (!today.isEmpty())
        out() << QStringLiteral("  Today: %1.\n").arg(today);
    out() << "  Measured and reported, and it takes nothing away: an app is still billed for\n"
             "  running, screen or no screen.\n";
}

// -- time per site -----------------------------------------------------------

/// The day's per-site seconds, in the order the ledger holds them.
QJsonObject sitesToJson(const Ledger &ledger)
{
    QJsonObject object;
    for (auto it = ledger.sites.constBegin(); it != ledger.sites.constEnd(); ++it)
        object.insert(it.key(), it.value());
    return object;
}

/// What the day's sites add up to, in words: `12m youtube.com, 3m wikipedia.org`.
///
/// Longest first, because a table of a day is read for what the afternoon went
/// on and not alphabetically.
QString sitesToday(const Ledger &ledger)
{
    QVector<QPair<int, QString>> ordered;
    for (auto it = ledger.sites.constBegin(); it != ledger.sites.constEnd(); ++it)
        ordered.append({it.value(), it.key()});
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    QStringList parts;
    for (const auto &one : ordered)
        parts.append(QStringLiteral("%1 %2").arg(humanDuration(one.first), one.second));
    return parts.join(QStringLiteral(", "));
}

/// The ids of the site budgets that can actually run out.
///
/// A site budget with no `dailyMinutes` counts and never runs out, so it is not
/// one of these. Naming it would leave the block claiming a limit that is not
/// written, which is the same lie the other way around.
QStringList limitedSites(const Profile *profile)
{
    QStringList ids;
    if (!profile)
        return ids;
    for (const Budget &budget : profile->budgets) {
        if (budget.isSite() && budget.hasLimit())
            ids.append(budget.id);
    }
    return ids;
}

/// Time per site, on the screen an operator reads.
///
/// The block ends by saying what the count is for, because somebody reading a
/// table of sites beside a table of budgets has to be told whether the first one
/// can take something away. It could not, once: site budgets grew teeth after
/// this was written, and until the profile was passed in here the same screen
/// said `stops opening` in the table above and "there is no site limit" two
/// inches below. That is why this takes a profile and not only a ledger.
void printSites(const QString &user, const QString &now, bool counted, bool couldLook,
                const Ledger &ledger, const Profile *profile)
{
    out() << "\nTIME PER SITE\n";
    if (!couldLook) {
        // Measured on the VM, and it read as the browser being shut: an operator
        // asking about somebody else gets an empty answer here, because the file
        // the browser's host writes is 0600 in that account's own runtime
        // directory. The day's totals below still come out, because those are the
        // ledger's and the ledger is 0644. Two different silences, and saying
        // "no browser is open" about this one would be a lie.
        out() << QStringLiteral("  Only %1 and root can see which tab is in front right now: "
                                "the browser\n  writes it in %1's own runtime directory. The "
                                "day below is from the ledger.\n")
                     .arg(user);
    } else if (now.isEmpty()) {
        out() << QStringLiteral("  Nothing is being reported right now: no browser with the "
                                "meter in it is open,\n  or nobody has it installed.\n");
    } else if (counted) {
        out() << QStringLiteral("  %1 is in the front tab, and it is being counted.\n").arg(now);
    } else {
        // The crossing, said where it happens. This is the sentence the whole
        // mechanism exists to be able to print.
        out() << QStringLiteral("  %1 is in the front tab and is NOT being counted: %2 is "
                                "not in front of\n  the screen.\n")
                     .arg(now, user);
    }
    const QString today = sitesToday(ledger);
    if (!today.isEmpty())
        out() << QStringLiteral("  Today: %1.\n").arg(today);
    const QStringList limited = limitedSites(profile);
    if (limited.isEmpty()) {
        out() << "  Counted only while the screen says somebody is there, and never billed to a\n"
                 "  budget: there is no site limit, no warning and no block.\n";
    } else {
        out() << QStringLiteral("  Counted only while the screen says somebody is there, and "
                                "billed to %1\n  above: this is the count %2 spends.\n")
                     .arg(limited.join(QStringLiteral(", ")),
                          limited.size() == 1 ? QStringLiteral("that budget")
                                              : QStringLiteral("those budgets"));
    }
}

// -- the sites ---------------------------------------------------------------

/// The reach of a browser policy, in three lines, written once — and the once is
/// `webPolicyReach` in `src/core/WebPolicy.h`.
///
/// It moved there when the studio grew a sites view, because there are now two
/// front ends that owe the sentence and a second copy is exactly how the two
/// would come to say it differently. This name stays as the short one the verbs
/// below read with.
QStringList theReach()
{
    return webPolicyReach();
}

/// The web half of one profile, in the shape the app half is printed in.
///
/// Printed by `profile show` and by `status`, so the two cannot come to describe
/// one profile differently. A profile with nothing to say about the web prints
/// one line saying so, and not an empty table -- the same choice `No budgets`
/// makes above.
void printWebRules(const Profile &profile, const QVector<Profile> &profiles)
{
    out() << "\nSITES\n";
    if (!profile.web.saysAnything()) {
        out() << QStringLiteral("  %1 has no web rules: every site opens, and there is no "
                                "browser policy on this machine because of them.\n")
                     .arg(profile.user);
        return;
    }

    out() << QStringLiteral("  %1\n")
                 .arg(profile.web.defaultVerdict == Verdict::Deny
                          ? QStringLiteral("only the listed sites open")
                          : QStringLiteral("every site opens except the blocked ones"));
    if (profile.web.incognitoStated) {
        out() << QStringLiteral("  incognito: %1\n")
                     .arg(profile.web.incognito == Verdict::Deny
                              ? QStringLiteral("does not open")
                              : QStringLiteral("opens — it hides which site, not the time"));
    }
    if (!profile.web.rules.isEmpty()) {
        QVector<QStringList> rows;
        for (const Rule &rule : profile.web.rules) {
            rows.append({rule.verdict == Verdict::Deny ? QStringLiteral("block")
                                                       : QStringLiteral("allow"),
                         rule.match});
        }
        printTable({QStringLiteral("VERDICT"), QStringLiteral("SITE")}, rows, {false, false});
    }

    // What the machine really does, which is not always what this profile asked
    // for -- WebPolicy.h, the most restrictive wins. Printed from the composed
    // policy rather than from this profile, because the composed one is the
    // thing the browser reads.
    const ChromiumPolicy policy = chromiumPolicyFor(profiles, sitesOutOfTime(profiles));
    if (!policy.needed()) {
        out() << QStringLiteral("  no %1 on this machine: nothing above asks for one.\n")
                     .arg(paths::chromiumPolicyFile());
        return;
    }
    out() << QStringLiteral("  %1, from every profile at once:\n").arg(paths::chromiumPolicyFile());
    if (!policy.blocklist.isEmpty()) {
        out() << QStringLiteral("      blocked  %1\n")
                     .arg(policy.blocklist.join(QStringLiteral(", ")));
    }
    if (!policy.allowlist.isEmpty()) {
        out() << QStringLiteral("      allowed  %1%2\n")
                     .arg(policy.allowlist.join(QStringLiteral(", ")),
                          policy.blocklist.isEmpty()
                              ? QStringLiteral("  (which blocks nothing on its own)")
                              : QString());
    }
    if (policy.incognitoDenied)
        out() << "      incognito does not open\n";
    for (const QString &line : theReach())
        out() << QStringLiteral("  %1\n").arg(line);
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

/// What is in `session.slice`, where omahouse cannot look -- docs/design.md §5.
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
             "  session with it.\n";
}

// The scopes whose id is not the name of what is running inside them.
//
// poc/findings.md round 4: seven `app-Hyprland-gtk\x2dlaunch-*.scope` on this
// machine, all of them VS Code, and a terminal that calls itself
// `xdg-terminal-exec`. The rule still matches the id -- that is the model, and
// `Policy::evaluate` never sees any of this -- so the only thing to do about it
// is to say it, which is the same duty docs/design.md §5 puts on the blind spot.

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
    // The seat and the screens, once. Two `loginctl` calls and a walk of
    // /sys/class/drm, all of them world readable, so this stays a screen anybody
    // can ask for without a privilege -- which is the rule the whole read side of
    // this program is built on.
    SeatPresenceSource presenceSource;
    const SeatReading seat = presenceSource.readSeat();
    const Presence presence = presenceOf(uid, session, seat);
    // And what the browser last said was in the front tab. World readable it is
    // not -- the file lives in that user's own runtime directory -- so this
    // answers for whoever runs `status` about themselves, and comes back empty
    // for an operator asking about somebody else. That asymmetry is deliberate
    // and is not a gap: the day's totals below come out of the ledger, which is
    // 0644 and which the daemon wrote, so an operator sees the accounting
    // without being able to read the child's live browsing.
    FileFocusSource focusSource;
    const QString siteNow = siteInFrontOf(focusSource.tail(uid), QDateTime::currentDateTime());
    // Whether this run could have read that file at all. Without it an operator
    // asking about somebody else is told the browser is shut, which is a
    // different thing and is not true.
    const bool couldLookAtTheTab = ::getuid() == 0 || ::getuid() == uid;
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

    const ChromiumPolicy webPolicy =
        chromiumPolicyFor(profiles.all, sitesOutOfTime(profiles.all));

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
            // Measured and reported, and it decides nothing: `verdict` and
            // `budgets` above are exactly what they were before anybody looked
            // at a screen. What reads this is the time per site, later.
            {QStringLiteral("presence"),
             QJsonObject {
                 // Null and not false for `unknown`: "nobody is there" and
                 // "nothing could be read" are different answers, and only one
                 // of them is about the person.
                 {QStringLiteral("present"),
                  presence.known() ? QJsonValue(presence.present) : QJsonValue()},
                 {QStringLiteral("reason"), presenceReasonName(presence.reason)},
                 {QStringLiteral("screen"), screenStateName(seat.screen)},
                 {QStringLiteral("seatRead"), seat.read},
                 {QStringLiteral("seatUid"),
                  seat.read && seat.occupied ? QJsonValue(static_cast<qint64>(seat.uid))
                                             : QJsonValue()},
                 {QStringLiteral("today"), presenceToJson(ledger)},
             }},
            // Unlike `presence` above, this one can be spent: a budget in
            // `budgets` whose `kind` is `site` is billed from exactly what is
            // reported here, and running it out is what shuts the site.
            {QStringLiteral("sites"),
             QJsonObject {
                 // Null and not an empty string: "there is no site in front" and
                 // "nothing is reporting" are different answers, and only one of
                 // them is about a browser that is open.
                 {QStringLiteral("now"), siteNow.isEmpty() ? QJsonValue() : QJsonValue(siteNow)},
                 {QStringLiteral("counted"), !siteNow.isEmpty() && presence.present},
                 // Whether this run could have read the live tab at all. Without
                 // it a script cannot tell a browser that is shut from a browser
                 // it was not allowed to look at.
                 {QStringLiteral("readable"), couldLookAtTheTab},
                 {QStringLiteral("today"), sitesToJson(ledger)},
             }},
            {QStringLiteral("profile"), profile ? QJsonValue(profile->toJson()) : QJsonValue()},
            {QStringLiteral("scopes"), scopesJson},
            {QStringLiteral("unnamed"), unnamedJson},
            {QStringLiteral("outOfReach"),
             QJsonObject {{QStringLiteral("processes"), sessionProcesses},
                          {QStringLiteral("units"), sessionJson}}},
            {QStringLiteral("budgets"), balancesToJson(balances)},
            // The composed policy, and not this profile's half of it: what a
            // script wants to know is what the browser on this machine will do,
            // and that is every profile's rules at once -- docs/design.md §11.
            // The profile's own half is already under `profile`.
            {QStringLiteral("webPolicy"),
             QJsonObject {
                 {QStringLiteral("path"), paths::chromiumPolicyFile()},
                 {QStringLiteral("wholeMachine"), true},
                 {QStringLiteral("contents"),
                  webPolicy.needed() ? QJsonValue(webPolicy.toJson()) : QJsonValue()},
             }},
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

        // The machine's own list is read here for the same reason `watch` reads
        // it every cycle: the two have to agree about what is furniture, and the
        // way to be sure of that is for both to read the file rather than for
        // one to be told.
        const QStringList alsoFurniture = furnitureOfThisMachine();
        bool anyFurniture = false;

        QVector<QStringList> rows;
        for (const AppScope &scope : named) {
            const bool furniture = isFurniture(scope.id, alsoFurniture);
            anyFurniture = anyFurniture || furniture;
            QStringList row {scope.id, QString::number(scope.pidCount)};
            if (profile) {
                // Furniture has no verdict, and saying `allow` would be a lie in
                // the direction that matters: it is not allowed, it is not asked
                // about.
                row.append(furniture ? QStringLiteral("the session's")
                                     : verdictName(profile->verdictFor(scope.id)));
            }
            row.append(scope.unit);
            rows.append(row);
        }
        printTable(headers, rows, right);
        if (anyFurniture) {
            // Said rather than left to be inferred from a word in a column. A
            // reader who counts the rows and then reads the session's total has
            // to be able to see why the two do not add up.
            out() << QStringLiteral("\n  Rows marked `the session's` are what Omarchy "
                                    "starts for itself, not what\n  %1 opened. They are "
                                    "never closed and never on their own start the\n  "
                                    "session's clock. %2 is where this machine adds to "
                                    "that list.\n")
                         .arg(user, paths::furnitureFile());
        }
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

    printPresence(user, presence, seat, ledger);
    printSites(user, siteNow, presence.present, couldLookAtTheTab, ledger, profile);

    if (profile)
        printWebRules(*profile, profiles.all);

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

    // Beside the budgets and never among them: this is how much of the day
    // somebody was really in front of the machine, and nothing above was billed
    // by it.
    if (!ledger.presence.isEmpty()) {
        out() << "\nPRESENCE\n";
        QVector<QStringList> presenceRows;
        for (auto it = ledger.presence.cbegin(); it != ledger.presence.cend(); ++it)
            presenceRows.append({it.key(), humanDuration(it.value())});
        printTable({QStringLiteral("STATE"), QStringLiteral("FOR")}, presenceRows,
                   {false, true});
    }

    // Beside the budgets, like the presence above it, and for a stronger reason:
    // there is no budget here to be beside. It is a count of what the afternoon
    // went on, and nothing in the table above was billed by it.
    if (!ledger.sites.isEmpty()) {
        out() << "\nTIME PER SITE\n";
        QVector<QStringList> siteRows;
        for (auto it = ledger.sites.cbegin(); it != ledger.sites.cend(); ++it)
            siteRows.append({it.key(), humanDuration(it.value())});
        printTable({QStringLiteral("SITE"), QStringLiteral("FOR")}, siteRows, {false, true});
    }

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
    QMap<QString, int> siteTotals;
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
        for (auto it = ledger.sites.cbegin(); it != ledger.sites.cend(); ++it)
            siteTotals[it.key()] += it.value();
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
        // Its own total and never folded into the one above. A site is not a
        // budget, and adding `youtube.com` to a sum of budgets would be a
        // document that says the day was twice as long as it was.
        QJsonObject siteTotalsJson;
        for (auto it = siteTotals.cbegin(); it != siteTotals.cend(); ++it)
            siteTotalsJson.insert(it.key(), it.value());
        printJson(QJsonObject {
            {QStringLiteral("user"), user},
            {QStringLiteral("since"), from.toString(Qt::ISODate)},
            {QStringLiteral("until"), today.toString(Qt::ISODate)},
            {QStringLiteral("days"), daysJson},
            {QStringLiteral("totals"), totalsJson},
            {QStringLiteral("siteTotals"), siteTotalsJson},
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

        if (!siteTotals.isEmpty()) {
            out() << "\nTOTAL PER SITE\n";
            QVector<QStringList> siteRows;
            for (auto it = siteTotals.cbegin(); it != siteTotals.cend(); ++it)
                siteRows.append({it.key(), humanDuration(it.value())});
            printTable({QStringLiteral("SITE"), QStringLiteral("FOR")}, siteRows,
                       {false, true});
        }
    }
    return kOk;
}

// -- profile -----------------------------------------------------------------

QString yesNo(bool value)
{
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

// -- the machines of this household -------------------------------------------
//
// omahouse has always known accounts and never known machines. Everything about
// a fleet -- reaching another computer, reading its day, telling it something --
// needs a place that says which computers are ours, and this is it.
//
// It holds no rules. What a machine does is its own `profiles.json`, on the
// machine, because a central that held the rules would be a central whose
// absence is a machine with no rules at all.

struct Fleet {
    QVector<Machine> all;
    bool missing = false;
};

bool loadMachines(Fleet *fleet, int *status)
{
    QString error;
    if (readMachines(paths::machinesFile(), &fleet->all, &error, &fleet->missing))
        return true;
    fail(QStringLiteral("omahouse: %1").arg(error));
    *status = kUsage;
    return false;
}

int cmdMachines(const Globals &g)
{
    int status = kOk;
    Fleet fleet;
    if (!loadMachines(&fleet, &status))
        return status;

    if (g.json) {
        QJsonArray array;
        for (const Machine &machine : fleet.all) {
            array.append(QJsonObject {
                {QStringLiteral("name"), machine.name},
                {QStringLiteral("nodeId"), machine.nodeId},
                {QStringLiteral("endpoint"), machine.endpoint},
                {QStringLiteral("reachable"), machine.reachable()},
            });
        }
        printJson(array);
        return kOk;
    }

    if (fleet.all.isEmpty()) {
        // The ordinary state, and said as such. Most households are one
        // computer, and a machine that has never been told about another is not
        // misconfigured.
        out() << QStringLiteral("No other machines: this one is the whole house.\n");
        return kOk;
    }

    QVector<QStringList> rows;
    for (const Machine &machine : fleet.all) {
        rows.append({machine.name,
                     machine.nodeId.isEmpty() ? kNothing : machine.nodeId,
                     machine.endpoint.isEmpty() ? kNothing : machine.endpoint,
                     machine.reachable() ? QStringLiteral("yes")
                                         : QStringLiteral("not paired yet")});
    }
    printTable({QStringLiteral("MACHINE"), QStringLiteral("NODE"),
                QStringLiteral("AT"), QStringLiteral("REACHABLE")},
               rows, {false, false, false, false});
    return kOk;
}

// -- pairing -----------------------------------------------------------------
//
// Two machines with no prior channel, brought to trust each other by a person
// who walks between them. That walk is the design and not a limitation: Omakure
// checks a peer against an identity it was given beforehand, so somebody has to
// carry the identity, and the only question is whether they carry it or type it.
//
// Three commands, in the order a person actually moves:
//
//   1. `machine invite` on the operator's computer, which prints a line.
//   2. `machine prepare --invite <line>` on the new computer, with the operator
//      sitting at it -- which is exactly where they already are, because they
//      just installed Omarchy on it.
//   3. `machine add <name> --pair <line>` back on the operator's computer.
//
// What that replaces is the seven steps `vm/cases/the_two_machines_trust_each_other.py`
// found by getting each of them wrong first: node operations run as the wrong
// account leaving root-owned locks, a `node init` before its config file exists,
// a config missing `version` and `[node]`, a `node serve` with no tokens file,
// the wire refusing a non-loopback bind, the trust exchange in both directions,
// and one sudoers line. Every one of them is a failure that reads like a
// networking problem and is not.

/// Declared here and defined with the rest of the writing verbs, which come
/// later in this file. The machine verbs write two of the machine's own roots
/// and want the same refusal every other writing verb gives.
bool mayWrite(const QString &verb, const QString &path, bool theSystems, const Globals &g);

/// Where the wire listens, worked out from the address a machine says it
/// answers at. Every interface, on the port it named: a machine cannot know
/// which of its addresses the household will reach it on, and asking for two
/// numbers that have to agree is asking for two numbers that will not.
QString wireBindFor(const QString &endpoint)
{
    return QStringLiteral("0.0.0.0%1")
            .arg(endpoint.mid(endpoint.lastIndexOf(QLatin1Char(':'))));
}

/// The wire's port, and the one number both sides have to agree on.
constexpr int kWirePort = 7879;

/// One argument, safe to paste into a shell on the far side.
QString shellQuoted(const QString &value)
{
    return QLatin1Char('\'') + QString(value).replace(QLatin1String("'"),
                                                       QLatin1String("'\\''"))
            + QLatin1Char('\'');
}

/// Run one command on another computer, with the operator's own ssh.
///
/// Not wrapped, not replaced, and given no options of its own beyond a
/// destination: whatever their ssh already does -- their keys, their config,
/// their agent -- is what this uses, because ssh access is the one thing they
/// were promised would be enough.
///
/// `$OMAHOUSE_SSH` moves it, and that is what lets the suite prove the whole
/// walk against a script that answers like a machine without one being there.
bool overSsh(const QString &target, const QString &command, QString *answer,
             QString *error)
{
    const QByteArray set = qgetenv("OMAHOUSE_SSH");
    const QString program = set.isEmpty() ? QStringLiteral("ssh")
                                          : QString::fromLocal8Bit(set);
    QProcess asking;
    asking.setProgram(program);
    asking.setArguments({target, command});
    // The far side may ask for a sudo password, and a person watching has to be
    // able to answer it. So this is the one subprocess in omahouse that keeps
    // the terminal it was started from.
    asking.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    asking.setInputChannelMode(QProcess::ForwardedInputChannel);
    asking.start();
    if (!asking.waitForStarted(10000)) {
        *error = QStringLiteral("cannot run %1: %2").arg(program, asking.errorString());
        return false;
    }
    // Generous: this waits on a package install on somebody else's machine.
    if (!asking.waitForFinished(600000)) {
        asking.kill();
        asking.waitForFinished();
        *error = QStringLiteral("%1 did not answer in ten minutes").arg(target);
        return false;
    }
    *answer = QString::fromLocal8Bit(asking.readAllStandardOutput()).trimmed();
    if (asking.exitStatus() != QProcess::NormalExit || asking.exitCode() != 0) {
        *error = QStringLiteral("it came back %1").arg(asking.exitCode());
        return false;
    }
    return true;
}

/// Where this machine's console answers, given where its wire does.
///
/// The same host, on Omakure's own API port. Derived rather than asked for: two
/// addresses a household has to keep in agreement are two addresses that will
/// one day disagree, and the failure of that is a machine that pairs and then
/// cannot be read.
QString apiEndpointFor(const QString &wire)
{
    return wire.left(wire.lastIndexOf(QLatin1Char(':'))) + QStringLiteral(":8787");
}

/// Whether the Omakure this run would touch is the machine's own.
bool omakureConfigIsTheSystems()
{
    return Omakure::configDir() == QLatin1String("/etc/omakure");
}

/// `/etc/sudoers.d`, or `$OMAHOUSE_SUDOERS_DIR`. The same door every other root
/// in `src/sys` has, and for the same reason: this file is the one that gives
/// an account a route to root, and no test of it may write the real one.
QString sudoersFile()
{
    const QByteArray set = qgetenv("OMAHOUSE_SUDOERS_DIR");
    const QString dir = set.isEmpty() ? QStringLiteral("/etc/sudoers.d")
                                      : QString::fromLocal8Bit(set);
    return dir + QStringLiteral("/omahouse-node");
}

/// The one route from the node's account to root, and it names one binary.
///
/// Both sides need it and for the same reason. The account that runs a Battery
/// script is the node's: it has no shell and no sudo, and omahouse's writing
/// verbs need root because `/etc/omahouse` is a root daemon's directory. On the
/// machine under rules that script is a Cue; on the operator's it is the
/// scheduled job that fetches days. Neither can do its work without this line.
///
/// One binary, and no arguments constrained. The checks are omahouse's own, and
/// a rule naming `ALL` here would turn one script into a route to everything.
bool letTheNodeReachOmahouse(const QString &verb, QString *error)
{
    const QString rule = QStringLiteral("%1 ALL=(root) NOPASSWD: %2\n")
                                 .arg(Omakure::account(),
                                      QCoreApplication::applicationFilePath());
    if (Omakure::writeSystemFile(sudoersFile(), rule, QString(), 0440, error))
        return true;
    fail(QStringLiteral("%1: %2").arg(verb, *error));
    return false;
}

/// This machine's Omakure config as omahouse last wrote it, or defaults.
NodeConfig currentNodeConfig()
{
    NodeConfig config;
    QFile file(Omakure::nodeConfigFile());
    if (file.open(QIODevice::ReadOnly)) {
        readRenderedNodeConfig(QString::fromUtf8(file.readAll()), &config);
        file.close();
    }
    return config;
}

bool omakureIsReady(const QString &verb);

/// The binary, the account, and the difference between them.
///
/// Only one of the two can be made from here. A missing binary is a package
/// that is not whole and the answer is the package manager; a missing service
/// account is something omahouse promised to handle and does. So the binary is
/// asked about first and separately, and the account is provisioned rather than
/// demanded.
bool omakureIsInstalledAndProvisioned(const QString &verb, int *status)
{
    QString why;
    if (Omakure::binary().isEmpty()) {
        omakureIsReady(verb);
        *status = kMissing;
        return false;
    }
    if (!Omakure::provision(&why)) {
        fail(QStringLiteral("%1: %2").arg(verb, why));
        *status = kUsage;
        return false;
    }
    if (!omakureIsReady(verb)) {
        *status = kMissing;
        return false;
    }
    return true;
}

/// The Omakure that has to be there before any of this means anything, and the
/// line to run when it is not.
bool omakureIsReady(const QString &verb)
{
    QString why;
    if (Omakure::installed(&why))
        return true;
    // A broken install and not a missing prerequisite. The omahouse package
    // depends on omakure, so a machine with one and not the other is a machine
    // where something took it away -- and telling somebody to go and install a
    // second product would be telling them to work around their own package
    // manager.
    const QString pad(verb.size(), QLatin1Char(' '));
    fail(QStringLiteral("%1: %2.").arg(verb, why));
    fail(QStringLiteral("%1  omahouse ships with omakure, so this install is not "
                        "whole. Put it back with:").arg(pad));
    fail(QStringLiteral("%1    sudo pacman -S omahouse").arg(pad));
    return false;
}

/// The bearers, by machine name. Kept apart from `machines.json` on purpose:
/// that file is 0644 because a household reads its own list, and one file that
/// is half public and half secret is a file somebody eventually publishes.
QJsonObject readMachineTokens()
{
    QFile file(paths::machineTokensFile());
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject();
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    return document.object();
}

bool writeMachineTokens(const QJsonObject &tokens, QString *error)
{
    return Omakure::writeSystemFile(
            paths::machineTokensFile(),
            QString::fromUtf8(QJsonDocument(tokens).toJson(QJsonDocument::Indented)),
            QString(), 0600, error);
}

int cmdMachineToken(const Globals &g, const QStringList &positionals)
{
    if (positionals.isEmpty()) {
        fail(QStringLiteral("machine token: which machine?"));
        return kUsage;
    }
    const QString name = positionals.first();
    if (!mayWrite(QStringLiteral("machine token"), paths::machineTokensFile(),
                  paths::configDirIsTheSystems(), g))
        return kUsage;

    const QJsonObject tokens = readMachineTokens();
    const QJsonObject one = tokens.value(name).toObject();
    const QString bearer = one.value(QStringLiteral("token")).toString();
    if (bearer.isEmpty()) {
        fail(QStringLiteral("machine token: nothing to read %1 with. It was written "
                            "down without pairing, or paired by an older build")
                 .arg(name));
        return kMissing;
    }
    if (g.json) {
        printJson(QJsonObject {{QStringLiteral("machine"), name},
                               {QStringLiteral("at"), one.value(QStringLiteral("api"))},
                               {QStringLiteral("token"), bearer}});
        return kOk;
    }
    // The bearer alone on stdout, and the address on the line before it, so
    // that a script can take either without parsing. This verb exists to be
    // read by the thing that fetches days.
    out() << one.value(QStringLiteral("api")).toString() << '\n' << bearer << '\n';
    return kOk;
}

/// Write down what this machine has become, beside becoming it.
///
/// Called from the two verbs that already decide it, so the file and the fact
/// cannot come apart: a machine that ran `machine prepare` is managed whether or
/// not anything was written, and the point of the file is that a person can ask.
bool rememberKind(const QString &verb, Kind kind, const QString &name,
                  const QString &managedBy)
{
    ThisMachine machine;
    QString error;
    bool ignored = false;
    readThisMachine(paths::thisMachineFile(), &machine, &error, &ignored);
    machine.kind = kind;
    machine.name = name;
    machine.managedBy = managedBy;
    machine.since = QDateTime::currentDateTime();
    if (writeThisMachine(paths::thisMachineFile(), machine, &error))
        return true;
    fail(QStringLiteral("%1: %2").arg(verb, error));
    return false;
}

int cmdMachineKind(const Globals &g)
{
    ThisMachine machine;
    QString error;
    bool missing = false;
    if (!readThisMachine(paths::thisMachineFile(), &machine, &error, &missing)) {
        fail(QStringLiteral("machine kind: %1").arg(error));
        return kUsage;
    }

    if (g.json) {
        printJson(QJsonObject {
            {QStringLiteral("kind"), kindName(machine.kind)},
            {QStringLiteral("name"), machine.name},
            {QStringLiteral("managedBy"), machine.managedBy},
            {QStringLiteral("since"), machine.since.isValid()
                                              ? machine.since.toString(Qt::ISODate)
                                              : QString()},
        });
        return kOk;
    }

    out() << QStringLiteral("%1\n")
                 .arg(machine.name.isEmpty() ? QStringLiteral("this machine")
                                             : machine.name);
    out() << QStringLiteral("  %1.\n").arg(kindSaid(machine.kind));
    if (!machine.managedBy.isEmpty())
        out() << QStringLiteral("  its manager is %1\n").arg(machine.managedBy);
    if (machine.kind == Kind::Alone) {
        // Said out loud, because "alone" is the state somebody would otherwise
        // read as "not set up yet". One computer under rules is the whole
        // product working; linking is what a second computer needs.
        out() << QStringLiteral("\n  Rules, budgets and the browser all work here "
                                "exactly as they are.\n"
                                "  Linking is what a second computer needs, not "
                                "something this one is missing.\n");
    }
    return kOk;
}

/// Make this machine the household's console, and hand back the line that lets
/// another computer trust it.
///
/// Extracted from `machine invite` so `machine link` can do the same thing
/// without shelling out to omahouse and parsing its own output. Running it again
/// is safe: it renders the config whole with every machine already paired still
/// in the peer list, and `node init` is skipped once there is an identity.
int becomeTheManager(const Globals &g, const QString &verb, const Options &options,
                     Pairing *mine, bool *hadIdentity)
{
    if (options.at.isEmpty() || !looksLikeEndpoint(options.at)) {
        fail(QStringLiteral("%1: --at <host:port>, where the other machines will "
                            "reach this one — like 192.168.1.10:7879")
                     .arg(verb));
        return kUsage;
    }
    if (!mayWrite(verb, Omakure::nodeConfigFile(), omakureConfigIsTheSystems(), g))
        return kUsage;
    int ready = kOk;
    if (!omakureIsInstalledAndProvisioned(verb, &ready))
        return ready;

    int status = kOk;
    Fleet fleet;
    if (!loadMachines(&fleet, &status))
        return status;

    // Rendered whole, with every machine already paired still in it. This verb
    // is run again whenever the operator's address changes, and a rewrite that
    // dropped the peers would be a household whose computers trust each other
    // and cannot find each other.
    NodeConfig config = currentNodeConfig();
    if (!options.name.isEmpty())
        config.displayName = options.name;
    if (config.displayName.isEmpty())
        config.displayName = QSysInfo::machineHostName();
    config.directBind = wireBindFor(options.at);
    config.staticPeers.clear();
    // No Batteries, and that is the point of this side. A Conductor that can be
    // cued back is a Conductor somebody took.
    config.cueBatteries.clear();
    for (const Machine &machine : fleet.all) {
        if (machine.reachable())
            config.staticPeers.append(staticPeer(machine.nodeId, machine.endpoint));
    }

    QString error;
    if (!Omakure::writeSystemFile(Omakure::nodeConfigFile(), renderNodeConfig(config),
                                  Omakure::account(), 0640, &error)) {
        fail(QStringLiteral("%1: %2").arg(verb, error));
        return kUsage;
    }

    QString nodeId;
    QString key;
    QString whyNot;
    const bool had = Omakure::identity(&nodeId, &key, &whyNot) && !nodeId.isEmpty();
    if (!had && !Omakure::initialise(&error)) {
        fail(QStringLiteral("%1: %2").arg(verb, error));
        return kUsage;
    }

    // The token before anything can start. `node serve` refuses to run at all
    // without auth material -- and then nothing is listening on the wire
    // either, which reads like a peering problem and is a startup one.
    if (!Omakure::provisionToken(&error)) {
        fail(QStringLiteral("%1: %2").arg(verb, error));
        return kUsage;
    }
    // And the route to root, here as well as on the machines this one will
    // administer. What runs on a schedule here -- the job that fetches every
    // machine's day -- runs as the node's account and files what it fetched.
    if (!letTheNodeReachOmahouse(verb, &error))
        return kUsage;

    if (!Omakure::describe(options.at, mine, &error)) {
        fail(QStringLiteral("%1: %2").arg(verb, error));
        return kUsage;
    }
    mine->name = config.displayName;

    // This machine is the household's console from here on, and it says so in a
    // file rather than only in what it happens to have configured.
    if (!rememberKind(verb, Kind::Manager, mine->name, QString()))
        return kUsage;
    *hadIdentity = had;
    return kOk;
}

int cmdMachineInvite(const Globals &g, const Options &options)
{
    const QString verb = QStringLiteral("machine invite");
    Pairing mine;
    bool had = false;
    const int status = becomeTheManager(g, verb, options, &mine, &had);
    if (status != kOk)
        return status;
    const QString line = encodePairing(mine);

    if (g.json) {
        printJson(QJsonObject {{QStringLiteral("name"), mine.name},
                               {QStringLiteral("nodeId"), mine.nodeId},
                               {QStringLiteral("endpoint"), mine.endpoint},
                               {QStringLiteral("kind"), kindName(Kind::Manager)},
                               {QStringLiteral("madeIdentity"), !had},
                               {QStringLiteral("invite"), line}});
        return kOk;
    }

    if (!had) {
        out() << QStringLiteral("This machine now has an Omakure identity of its own: "
                                "%1.\n").arg(mine.nodeId);
    }
    out() << QStringLiteral("%1 is %2, answering at %3.\n\n")
                 .arg(mine.name, mine.nodeId, mine.endpoint);
    out() << QStringLiteral("Take this line to the computer you are adding, and run "
                            "there:\n\n");
    out() << QStringLiteral("  sudo omahouse machine prepare --at <that computer>:%1 "
                            "\\\n    --invite %2\n\n")
                 .arg(options.at.mid(options.at.lastIndexOf(QLatin1Char(':')) + 1), line);
    // Said plainly, because the line looks like a secret and being careful with
    // it costs a household nothing until they are careful with the wrong thing.
    out() << QStringLiteral("There is no secret in that line: a node id, a public key, "
                            "a certificate\nand an address, all of them public. What it "
                            "buys is not having to type them.\n");
    return kOk;
}

int cmdMachinePrepare(const Globals &g, const Options &options)
{
    const QString verb = QStringLiteral("machine prepare");
    Pairing conductor;
    QString error;
    if (options.invite.isEmpty()) {
        fail(QStringLiteral("%1: --invite <line>, the line `omahouse machine invite` "
                            "printed on the computer that will administer this one")
                     .arg(verb));
        return kUsage;
    }
    if (!decodePairing(options.invite, &conductor, &error)) {
        fail(QStringLiteral("%1: %2").arg(verb, error));
        return kUsage;
    }
    if (options.at.isEmpty() || !looksLikeEndpoint(options.at)) {
        fail(QStringLiteral("%1: --at <host:port>, where this computer will answer — "
                            "like 192.168.1.20:7879")
                     .arg(verb));
        return kUsage;
    }
    if (!mayWrite(verb, Omakure::nodeConfigFile(), omakureConfigIsTheSystems(), g))
        return kUsage;
    int ready = kOk;
    if (!omakureIsInstalledAndProvisioned(verb, &ready))
        return ready;

    NodeConfig config = currentNodeConfig();
    if (!options.name.isEmpty())
        config.displayName = options.name;
    if (config.displayName.isEmpty())
        config.displayName = QSysInfo::machineHostName();
    config.directBind = wireBindFor(options.at);
    config.staticPeers = {staticPeer(conductor.nodeId, conductor.endpoint)};
    config.cueBatteries = {options.battery.isEmpty() ? QStringLiteral("omahouse")
                                                     : options.battery};

    // The config first, and always before `node init`: init runs as the node's
    // account and `/etc/omakure` is root's, so left to create the file itself it
    // answers `io_failed: Permission denied`.
    if (!Omakure::writeSystemFile(Omakure::nodeConfigFile(), renderNodeConfig(config),
                                  Omakure::account(), 0640, &error)) {
        fail(QStringLiteral("%1: %2").arg(verb, error));
        return kUsage;
    }

    QString nodeId;
    QString key;
    QString whyNot;
    const bool had = Omakure::identity(&nodeId, &key, &whyNot) && !nodeId.isEmpty();
    if (!had && !Omakure::initialise(&error)) {
        fail(QStringLiteral("%1: %2").arg(verb, error));
        return kUsage;
    }

    // This side trusts the other as a Conductor: it may give orders here. The
    // other side will trust this one as a Performer, and the asymmetry is the
    // whole of what keeps a child's computer from cueing its parent's.
    if (!Omakure::trust(conductor, QStringLiteral("conductor"), &error)) {
        fail(QStringLiteral("%1: %2").arg(verb, error));
        return kUsage;
    }

    if (!Omakure::provisionToken(&error)) {
        fail(QStringLiteral("%1: %2").arg(verb, error));
        return kUsage;
    }
    QString reading;
    if (!Omakure::provisionReadingToken(&reading, &error)) {
        fail(QStringLiteral("%1: %2").arg(verb, error));
        return kUsage;
    }

    if (!letTheNodeReachOmahouse(verb, &error))
        return kUsage;

    // Last, and allowed to fail. Everything above is the part that is hard to
    // get right and impossible to guess at; a machine with no systemd still has
    // its identity, its trust and its config, and calling the pairing failed
    // because nothing started would be throwing all of that away.
    QString notStarted;
    // The console goes on the network here and nowhere else. It is the one way
    // a day can come back: Omakure's wire carries `cue_dispatch` and `cue_ack`
    // and the ack has no body, so a Cue can tell this machine to do something
    // and can never bring a document. Only the machine being administered opens
    // this door; the operator's stays shut, because nobody pulls from it.
    const bool serving = Omakure::enableService(true, &notStarted);

    Pairing mine;
    if (!Omakure::describe(options.at, &mine, &error)) {
        fail(QStringLiteral("%1: %2").arg(verb, error));
        return kUsage;
    }
    mine.name = config.displayName;
    mine.apiEndpoint = apiEndpointFor(options.at);
    mine.token = reading;
    const QString line = encodePairing(mine);

    // Managed, and it names who by. Everything omahouse does here still works
    // without that manager -- which is the point of installing the whole of it
    // rather than an agent: when the console cannot be reached, whoever has root
    // on this machine can still read the day, hand over time and switch the
    // teeth off, sitting at it.
    if (!rememberKind(verb, Kind::Managed, mine.name, conductor.nodeId))
        return kUsage;

    if (g.json) {
        printJson(QJsonObject {{QStringLiteral("name"), mine.name},
                               {QStringLiteral("nodeId"), mine.nodeId},
                               {QStringLiteral("endpoint"), mine.endpoint},
                               {QStringLiteral("apiEndpoint"), mine.apiEndpoint},
                               {QStringLiteral("conductor"), conductor.nodeId},
                               {QStringLiteral("battery"), config.cueBatteries.value(0)},
                               {QStringLiteral("serving"), serving},
                               {QStringLiteral("pair"), line}});
        return kOk;
    }

    out() << QStringLiteral("%1 is %2, answering at %3.\n")
                 .arg(mine.name, mine.nodeId, mine.endpoint);
    out() << QStringLiteral("It trusts %1 to give it orders, and will run scripts from "
                            "the %2 Battery.\n")
                 .arg(conductor.nodeId, config.cueBatteries.value(0));
    if (!serving) {
        // Named rather than swallowed: everything else worked, and this is the
        // one part somebody has to finish by hand.
        fail(QStringLiteral("%1: %2 — the pairing is written, but nothing is "
                            "listening yet.")
                     .arg(verb, notStarted));
    }
    out() << QStringLiteral("\nTake this line back to the computer that will administer "
                            "it, and run there:\n\n");
    out() << QStringLiteral("  sudo omahouse machine add <a name for this one> \\\n"
                            "    --pair %1\n\n")
                 .arg(line);
    // Said, and said differently from the invitation, because the two lines
    // look identical and are not. The invitation is four public facts. This one
    // carries a key to this machine's console, and a household that learned
    // from the first line that these are safe to paste anywhere would be
    // learning the wrong thing.
    out() << QStringLiteral("Unlike the invitation, that line is a key to this "
                            "computer. Carry it once,\nto that one computer, and do "
                            "not leave it where the line can be read.\n");
    return kOk;
}

int cmdMachineAdd(const Globals &g, const QStringList &positionals, const Options &options)
{
    // A pairing line answers three of these at once -- the identity, the
    // address, and that both are real -- so it is read before anything is
    // demanded of the operator.
    Pairing paired;
    bool wasPaired = false;
    if (!options.pair.isEmpty()) {
        QString why;
        if (!decodePairing(options.pair, &paired, &why)) {
            fail(QStringLiteral("machine add: %1").arg(why));
            return kUsage;
        }
        wasPaired = true;
    }

    const QString name = positionals.value(0, paired.name);
    if (name.isEmpty()) {
        fail(QStringLiteral("machine add: which machine? A name this household would "
                            "use, like \"the kitchen laptop\""));
        return kUsage;
    }
    if (wasPaired && (!options.node.isEmpty() || !options.at.isEmpty())) {
        // Refused rather than merged. The line carries an identity and an
        // address that were measured on the machine itself; a `--node` beside
        // it is somebody correcting a value they cannot have checked, and the
        // failure it buys is a peer that is trusted under one identity and
        // looked for at another.
        fail(QStringLiteral("machine add: --pair already says which machine and where. "
                            "Give it, or give --node and --at, not both"));
        return kUsage;
    }

    int status = kOk;
    Fleet fleet;
    if (!loadMachines(&fleet, &status))
        return status;

    // Writing it down again with new details is how a machine that was named
    // before it was paired becomes reachable, so this updates rather than
    // refusing. What it will not do is silently forget something already there:
    // a field left off keeps what the file had.
    const int existing = indexOfMachine(fleet.all, name);
    Machine machine;
    if (existing >= 0)
        machine = fleet.all.at(existing);
    machine.name = name;
    if (!options.node.isEmpty())
        machine.nodeId = options.node;
    if (!options.at.isEmpty())
        machine.endpoint = options.at;
    if (wasPaired) {
        machine.nodeId = paired.nodeId;
        machine.endpoint = paired.endpoint;
    }
    if (!machine.addedAt.isValid())
        machine.addedAt = QDateTime::currentDateTime();

    if (existing >= 0)
        fleet.all[existing] = machine;
    else
        fleet.all.append(machine);

    // Trust first, and the list after. The list is the household's note to
    // itself and can be written again in a second; the trust registry and the
    // node config are the machine's, and a name written down for a machine this
    // one cannot actually speak to is the exact state `reachable` exists to
    // deny.
    QString error;
    bool serving = false;
    QString notStarted;
    if (wasPaired) {
        if (!mayWrite(QStringLiteral("machine add"), Omakure::nodeConfigFile(),
                      omakureConfigIsTheSystems(), g))
            return kUsage;
        if (!omakureIsReady(QStringLiteral("machine add")))
            return kMissing;
        if (!Omakure::trust(paired, QStringLiteral("performer"), &error)) {
            fail(QStringLiteral("machine add: %1").arg(error));
            return kUsage;
        }
        // And the peer written into this machine's own config, because trusting
        // a machine and knowing where it is are two different files. `invite`
        // wrote the rest of this one; here only the peer list moves.
        NodeConfig config = currentNodeConfig();
        if (config.displayName.isEmpty()) {
            fail(QStringLiteral("machine add: this machine has no Omakure config of "
                                "omahouse's own yet — run `omahouse machine invite "
                                "--at <host:port>` here first"));
            return kUsage;
        }
        config.staticPeers.clear();
        config.cueBatteries.clear();
        for (const Machine &known : fleet.all) {
            if (known.reachable())
                config.staticPeers.append(staticPeer(known.nodeId, known.endpoint));
        }
        if (!Omakure::writeSystemFile(Omakure::nodeConfigFile(), renderNodeConfig(config),
                                      Omakure::account(), 0640, &error)) {
            fail(QStringLiteral("machine add: %1").arg(error));
            return kUsage;
        }
        serving = Omakure::enableService(false, &notStarted);

        // The bearer last, and only once everything it is for exists. A token
        // written down for a machine this one never came to trust is a
        // credential with nothing to open, kept forever.
        if (paired.isReachableForReading()) {
            QJsonObject tokens = readMachineTokens();
            tokens.insert(name, QJsonObject {
                {QStringLiteral("api"), paired.apiEndpoint},
                {QStringLiteral("token"), paired.token},
            });
            if (!writeMachineTokens(tokens, &error)) {
                fail(QStringLiteral("machine add: %1").arg(error));
                return kUsage;
            }
        }
    }

    if (!writeMachines(paths::machinesFile(), fleet.all, &error)) {
        fail(QStringLiteral("machine add: %1").arg(error));
        return kUsage;
    }
    if (g.json) {
        QJsonObject answer = machine.toJson();
        if (wasPaired) {
            answer.insert(QStringLiteral("trusted"), true);
            answer.insert(QStringLiteral("serving"), serving);
        }
        printJson(answer);
        return kOk;
    }
    if (wasPaired) {
        out() << QStringLiteral("%1: paired. This machine trusts it as a performer, "
                                "and knows to find it at %2.\n")
                     .arg(name, machine.endpoint);
        if (!serving) {
            fail(QStringLiteral("machine add: %1 — the pairing is written, but nothing "
                                "is listening here yet.").arg(notStarted));
        }
        return kOk;
    }
    out() << QStringLiteral("%1: %2 in the house%3.\n")
                 .arg(name,
                      existing >= 0 ? QStringLiteral("written down again")
                                    : QStringLiteral("written down"),
                      machine.reachable()
                          ? QStringLiteral(", reachable at %1").arg(machine.endpoint)
                          : QStringLiteral(" — not paired yet, so nothing can be asked "
                                           "of it"));
    return kOk;
}

/// The one command the whole of this exists to make possible.
///
/// A household buys a second computer, puts Omarchy on it, and the operator has
/// ssh to it. From their own machine, one line:
///
///     sudo omahouse machine link arch@192.168.1.20 --as "the kitchen laptop" \
///          --at 192.168.1.10:7879
///
/// and that computer is installed, linked, and in the list. What it does is the
/// three verbs of the pairing walk with the walking done over ssh instead of by
/// a person: this machine becomes the manager, the far one is prepared with the
/// invitation, and the line that comes back is added here.
///
/// It is a verb and not a shell script for one reason: it already holds the
/// invitation in memory. A script would have to run `machine invite`, parse its
/// own output, and hope the format never changed -- three chances to be wrong
/// about something this program already knows.
///
/// ssh is not wrapped or replaced. Whatever the operator's ssh already does --
/// their keys, their config, their agent -- is what this uses, because the one
/// thing they were promised is that ssh access is enough.
int cmdMachineLink(const Globals &g, const QStringList &positionals,
                   const Options &options)
{
    const QString verb = QStringLiteral("machine link");
    if (positionals.isEmpty()) {
        fail(QStringLiteral("%1: which computer? An ssh destination, like "
                            "arch@192.168.1.20").arg(verb));
        return kUsage;
    }
    const QString target = positionals.first();

    // The far machine's address, taken from the destination rather than asked
    // for again. Two addresses a person has to keep in agreement are two
    // addresses that will one day disagree -- and the one they just typed is
    // the one that demonstrably reaches it.
    const QString host = target.contains(QLatin1Char('@'))
                                 ? target.section(QLatin1Char('@'), 1)
                                 : target;
    if (host.isEmpty()) {
        fail(QStringLiteral("%1: '%2' names no computer").arg(verb, target));
        return kUsage;
    }
    const QString name = options.name.isEmpty() ? host : options.name;

    // This machine first. A manager that could not describe itself has nothing
    // to hand over, and finding that out after touching the far machine would
    // leave it half linked.
    Pairing mine;
    bool had = false;
    const int became = becomeTheManager(g, verb, options, &mine, &had);
    if (became != kOk)
        return became;

    QString error;
    QString said;
    // Installed only when it is not there. Re-linking a computer is an ordinary
    // thing to do -- an address changed, a manager was rebuilt -- and it must
    // not reinstall the package underneath somebody.
    if (!overSsh(target, QStringLiteral("command -v omahouse >/dev/null || "
                                        "sudo pacman -S --needed --noconfirm omahouse"),
                 &said, &error)) {
        fail(QStringLiteral("%1: could not put omahouse on %2: %3")
                     .arg(verb, target, error));
        fail(QStringLiteral("%1  omahouse brings omakure and the browser's meter "
                            "with it, so this is the only install there is.")
                     .arg(QString(verb.size(), QLatin1Char(' '))));
        return kUsage;
    }

    // The far machine prepares itself from the invitation and nothing else. Not
    // this machine's node id, not its key, not its certificate -- they are all
    // inside the one line, which is what the line is for.
    const QString prepare =
            QStringLiteral("sudo omahouse --json machine prepare --invite %1 "
                           "--at %2:%3 --name %4")
                    .arg(encodePairing(mine), host, QString::number(kWirePort),
                         shellQuoted(name));
    if (!overSsh(target, prepare, &said, &error)) {
        fail(QStringLiteral("%1: %2 would not prepare itself: %3")
                     .arg(verb, target, error));
        return kUsage;
    }

    const QJsonObject prepared =
            QJsonDocument::fromJson(said.toUtf8()).object();
    Pairing theirs;
    if (!decodePairing(prepared.value(QStringLiteral("pair")).toString(), &theirs,
                       &error)) {
        fail(QStringLiteral("%1: %2 answered without a usable pairing line: %3")
                     .arg(verb, target, error));
        return kUsage;
    }

    // And the last step is the local one this program already knows how to do.
    Options adding;
    adding.pair = encodePairing(theirs);
    adding.given = {QStringLiteral("--pair")};
    const int added = cmdMachineAdd(g, {name}, adding);
    if (added != kOk)
        return added;

    if (g.json)
        return kOk;
    out() << QStringLiteral("\n%1 is linked. From here you can put an account on it "
                            "under rules,\nand `omahouse house <user>` will add its "
                            "day to this one's.\n").arg(name);
    // Said because it is the reason the whole of omahouse went onto that machine
    // rather than an agent, and somebody should know it before they need it.
    out() << QStringLiteral("\nIf this computer ever cannot reach it, log in there "
                            "as root: everything\nomahouse does works on that "
                            "machine on its own.\n");
    return kOk;
}

int cmdMachineRemove(const Globals &g, const QStringList &positionals)
{
    if (positionals.isEmpty()) {
        fail(QStringLiteral("machine remove: which machine?"));
        return kUsage;
    }
    const QString name = positionals.first();

    int status = kOk;
    Fleet fleet;
    if (!loadMachines(&fleet, &status))
        return status;

    const int existing = indexOfMachine(fleet.all, name);
    if (existing < 0) {
        fail(QStringLiteral("machine remove: no machine called %1 in %2")
                 .arg(name, paths::machinesFile()));
        return kMissing;
    }
    fleet.all.removeAt(existing);

    QString error;
    if (!writeMachines(paths::machinesFile(), fleet.all, &error)) {
        fail(QStringLiteral("machine remove: %1").arg(error));
        return kUsage;
    }
    // And the bearer with it. Forgetting a machine while keeping the key to it
    // is the shape of every credential nobody remembers they still hold.
    QJsonObject tokens = readMachineTokens();
    if (tokens.contains(name)) {
        tokens.remove(name);
        QString why;
        if (!writeMachineTokens(tokens, &why))
            fail(QStringLiteral("machine remove: the machine is out of the list, but "
                                "its key is still in %1: %2")
                     .arg(paths::machineTokensFile(), why));
    }
    if (g.json) {
        printJson(QJsonObject {{QStringLiteral("name"), name},
                               {QStringLiteral("removed"), true}});
        return kOk;
    }
    // Said plainly, because taking a machine out of the list does nothing to
    // the machine. Its rules, its daemon and its account are all still there.
    out() << QStringLiteral("%1: out of the house's list. Nothing on that machine "
                            "changed — its rules and its daemon are still running.\n")
                 .arg(name);
    return kOk;
}

int cmdMachine(const Globals &g, const QStringList &positionals, const Options &options)
{
    const QString what = positionals.value(0);
    const QStringList rest = positionals.mid(1);
    if (what == QLatin1String("invite")) {
        if (!onlyTheseOptions(options, {QStringLiteral("--at"), QStringLiteral("--name")},
                              QStringLiteral("machine invite")))
            return kUsage;
        return cmdMachineInvite(g, options);
    }
    if (what == QLatin1String("prepare")) {
        if (!onlyTheseOptions(options,
                              {QStringLiteral("--invite"), QStringLiteral("--at"),
                               QStringLiteral("--name"), QStringLiteral("--battery")},
                              QStringLiteral("machine prepare")))
            return kUsage;
        return cmdMachinePrepare(g, options);
    }
    if (what == QLatin1String("add")) {
        if (!onlyTheseOptions(options,
                              {QStringLiteral("--node"), QStringLiteral("--at"),
                               QStringLiteral("--pair")},
                              QStringLiteral("machine add")))
            return kUsage;
        return cmdMachineAdd(g, rest, options);
    }
    if (what == QLatin1String("remove")) {
        if (!onlyTheseOptions(options, {}, QStringLiteral("machine remove")))
            return kUsage;
        return cmdMachineRemove(g, rest);
    }
    if (what == QLatin1String("link")) {
        if (!onlyTheseOptions(options,
                              {QStringLiteral("--at"), QStringLiteral("--name")},
                              QStringLiteral("machine link")))
            return kUsage;
        return cmdMachineLink(g, rest, options);
    }
    if (what == QLatin1String("kind")) {
        if (!onlyTheseOptions(options, {}, QStringLiteral("machine kind")))
            return kUsage;
        return cmdMachineKind(g);
    }
    if (what == QLatin1String("token")) {
        if (!onlyTheseOptions(options, {}, QStringLiteral("machine token")))
            return kUsage;
        return cmdMachineToken(g, rest);
    }
    fail(QStringLiteral("machine: link, kind, invite, prepare, add, remove or "
                        "token, not '%1'").arg(what));
    return kUsage;
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

    printWebRules(*profile, profiles.all);
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

/// Whether this run may touch the browser's managed policy directory.
///
/// The same shape of question `Enforce.h` asks before it signals a cgroup or
/// ends a session, and it is here for a stronger reason than either: this is the
/// developer's own machine and the developer's own Chromium. A run pointed at a
/// configuration of its own -- the end to end suite, somebody trying a profile
/// out in $TMPDIR -- has no business rewriting the browser policy of whoever
/// started it, and a `$OMAHOUSE_CHROMIUM_POLICY_DIR` of its own is how such a
/// run proves the file instead.
///
/// So: the machine's own policy directory is written only by a run that is also
/// managing the machine's own /etc/omahouse. Anything else writes where it was
/// pointed, and says nothing.
bool mayTouchTheBrowserPolicy()
{
    return !paths::chromiumPolicyDirIsTheSystems() || paths::configDirIsTheSystems();
}

/// Makes `/etc/chromium/policies/managed/omahouse.json` say what the profiles
/// say -- docs/design.md §11 -- and takes it away when they say nothing.
///
/// Called from `saveProfiles` and therefore from every verb that writes a
/// profile, which is the whole of how this file stays true. Nothing has to
/// remember: `omahouse web allow` of the last blocked site removes the file,
/// `profile remove` of the last profile with web rules removes it, and
/// `profile enforce --off` does not, because a disabled *profile* is the switch
/// for that and `enabled` is what `chromiumPolicyFor` reads.
///
/// A failure here is said out loud and does not undo the profile that was just
/// written. The profile is the record of what the operator decided; the policy
/// file is a consequence of it, and the next successful write of any verb
/// reconciles it. Rolling the decision back because a directory was not
/// writable would lose the decision as well.
void refreshWebPolicy(const QString &verb, const QVector<Profile> &profiles)
{
    const ChromiumPolicy policy = chromiumPolicyFor(profiles, sitesOutOfTime(profiles));
    const QString path = paths::chromiumPolicyFile();
    // An unchanged decision is not written again: this file is read by every
    // Chromium that starts, and rewriting it for a verb that had nothing to do
    // with the web would be a modification time that means nothing. Asked first,
    // and not after the refusal below, so that `omahouse allow julia code` in a
    // tree of its own is silent instead of explaining a browser it was never
    // going to touch.
    if (chromiumPolicyIsAlready(path, policy))
        return;

    if (!mayTouchTheBrowserPolicy()) {
        note(QStringLiteral("omahouse: %1 is this machine's own, and this run's configuration "
                            "is %2.")
                 .arg(paths::chromiumPolicyDir(), paths::configDir()));
        note(QStringLiteral("          The browser policy was left alone. Point "
                            "$OMAHOUSE_CHROMIUM_POLICY_DIR somewhere of its own to write it."));
        return;
    }

    QString error;
    // Not emptied -- removed. The machine has to end up where it was before the
    // first web rule was written, and an empty managed policy left behind is a
    // machine that still looks managed.
    const bool done = policy.needed() ? writeChromiumPolicy(path, policy, &error)
                                      : removeChromiumPolicy(path, &error);
    if (done)
        return;
    fail(QStringLiteral("%1: the profile was saved, and %2").arg(verb, error));
    if (!runningAsRoot()) {
        fail(QStringLiteral("%1  that directory belongs to root; try pkexec")
                 .arg(QString(verb.size(), QLatin1Char(' '))));
    }
}

bool saveProfiles(const QString &verb, const QVector<Profile> &profiles)
{
    QString error;
    if (!writeProfiles(paths::profilesFile(), profiles, &error)) {
        fail(QStringLiteral("%1: %2").arg(verb, error));
        if (!runningAsRoot()) {
            fail(QStringLiteral("%1  that file belongs to root; try pkexec")
                     .arg(QString(verb.size(), QLatin1Char(' '))));
        }
        return false;
    }
    refreshWebPolicy(verb, profiles);
    return true;
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
/// docs/design.md §5: an app launched through a shim takes the shim's name, so a rule
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

    // docs/design.md §1: the operator is whoever is in wheel. A profile for one of them
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
    // docs/design.md §5: a new profile observes. It counts and reports and closes
    // nothing, because seeing a day of the report before switching the teeth on
    // is what the lan house always did.
    profile.enforce = false;
    // And it allows, which is docs/design.md §4's reading of a missing `default`: a
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

    // The account is left where it is, always. docs/design.md §7 spells the flag as
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
    // The same engine, said the way docs/design.md §8 asks the studio to say it: a list
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

    // `allow ... --limit 45m` is sugar, and docs/design.md §7 says why: the rule and the
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
            profile->budgets.append(
                Budget {id, id, Selects::App, minutes, OnExhausted::Close});
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

// -- web ---------------------------------------------------------------------
//
// docs/design.md §11. The same three parts the app half has -- a default
// verdict, a list of rules, and the verbs to change them -- with a domain where
// a scope id would be, and one extra switch that has no counterpart: incognito.
//
// The verbs are `block` and `allow` rather than `deny` and `allow` on purpose.
// `omahouse deny julia chromium` and `omahouse web block julia youtube.com` are
// different enough acts that they should not be one word: the first takes a
// program off somebody's list, the second changes what every browser on the
// machine will open.

/// The lines of `theReach`, on stderr, under the program's name. Once per run,
/// after the write, so that `--json` stays one document on stdout and a person
/// still reads the sentence.
void sayTheReach()
{
    const QStringList lines = theReach();
    for (int i = 0; i < lines.size(); ++i) {
        note(QStringLiteral("%1%2")
                 .arg(i == 0 ? QStringLiteral("omahouse: ") : QStringLiteral("          "),
                      lines.at(i)));
    }
}

/// A domain as it will be written into the file, or a refusal saying what was
/// wanted.
///
/// Lower cased, because a hostname is not case sensitive and `YouTube.com` and
/// `youtube.com` are one site -- the opposite of the app half, where
/// `org.freedesktop.Platform` and `org.freedesktop.platform` are two different
/// flatpaks. Everything else is refused rather than repaired: a URL, a path, a
/// scheme. Chromium's filter format would accept most of them and mean something
/// slightly different by each, and an operator who typed a whole URL and got a
/// rule about its host would not find out until the day it did not fire.
bool domainFromWhatWasTyped(const QString &verb, const QString &typed, QString *out)
{
    const QString domain = typed.trimmed().toLower();
    if (domain.isEmpty()) {
        fail(QStringLiteral("%1: which site? (try omahouse --help)").arg(verb));
        return false;
    }
    if (domain == QLatin1String("*")) {
        fail(QStringLiteral("%1: `*` is every site, and it is spelled as a mode rather than as "
                            "a rule.")
                 .arg(verb));
        fail(QStringLiteral("%1  omahouse web <user> --only-listed")
                 .arg(QString(verb.size(), QLatin1Char(' '))));
        return false;
    }
    if (domain.contains(QLatin1String("://")) || domain.contains(QLatin1Char('/'))
        || domain.contains(QLatin1Char(' '))) {
        fail(QStringLiteral("%1: '%2' is not a domain. Write the site's name on its own, like "
                            "youtube.com,")
                 .arg(verb, typed));
        fail(QStringLiteral("%1  and not a whole address. A bare domain covers its subdomains "
                            "too.")
                 .arg(QString(verb.size(), QLatin1Char(' '))));
        return false;
    }
    if (!domain.contains(QLatin1Char('.'))) {
        fail(QStringLiteral("%1: '%2' has no dot in it, so it is not a domain. Did you mean "
                            "%2.com?")
                 .arg(verb, domain));
        return false;
    }
    *out = domain;
    return true;
}

/// The web rule about `domain`, made at the end of the list if there is none.
/// Changed in place for the reason `ruleFor` is: the first rule that names a
/// site wins, so a second line about it would be a line that never fires.
Rule *webRuleFor(Profile *profile, const QString &domain)
{
    for (Rule &rule : profile->web.rules) {
        if (rule.match == domain)
            return &rule;
    }
    profile->web.rules.append(Rule {domain, Verdict::Allow});
    return &profile->web.rules.last();
}

/// What the machine will really do about `domain` once every profile has had its
/// say, printed when it is not what this one profile asked for.
///
/// This is the visible half of the composition rule of `WebPolicy.h`: the most
/// restrictive wins, so an `allow` here can be overruled by a `block` elsewhere.
/// Being overruled silently is the one thing that would make the rule dishonest,
/// so the verb that was overruled says who by.
void warnAboutTheOtherProfiles(const QVector<Profile> &profiles, const QString &user,
                               const QString &domain, Verdict asked)
{
    QStringList disagreeing;
    for (const Profile &profile : profiles) {
        if (profile.user == user || !profile.enabled || !profile.web.saysAnything())
            continue;
        if (profile.web.verdictFor(domain) != asked)
            disagreeing.append(profile.user);
    }
    if (disagreeing.isEmpty())
        return;
    note(QStringLiteral("omahouse: %1 %2 about %3, and the most restrictive of the two is what "
                        "the machine")
             .arg(disagreeing.join(QStringLiteral(", ")),
                  disagreeing.size() == 1 ? QStringLiteral("disagrees")
                                          : QStringLiteral("disagree"),
                  domain));
    note(QStringLiteral("          does — there is one policy file, and no precedence between "
                        "profiles."));
}

int cmdWebRule(const Globals &g, const QString &verb, const QStringList &positionals,
               Verdict verdict)
{
    if (positionals.size() != 2) {
        fail(QStringLiteral("%1: which user, and which site? (try omahouse --help)").arg(verb));
        return kUsage;
    }
    const QString user = positionals.at(0);
    QString domain;
    if (!domainFromWhatWasTyped(verb, positionals.at(1), &domain))
        return kUsage;

    if (!mayWrite(verb, paths::profilesFile(), paths::configDirIsTheSystems(), g))
        return kUsage;

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    Profile *profile = profileToChange(verb, &profiles, user, &status);
    if (!profile)
        return status;

    webRuleFor(profile, domain)->verdict = verdict;
    if (!saveProfiles(verb, profiles.all))
        return kUsage;

    warnAboutTheOtherProfiles(profiles.all, user, domain, verdict);
    sayTheReach();

    QString line;
    if (verdict == Verdict::Deny) {
        line = QStringLiteral("%1: %2 is blocked, and so are its subdomains.").arg(user, domain);
    } else if (chromiumPolicyFor(profiles.all, sitesOutOfTime(profiles.all))
                   .blocklist.isEmpty()) {
        // The trap the browser spike measured one policy over: an
        // allowlist with no blocklist beside it lets everything through,
        // including the thing it names. Saying "allowed" and stopping would read
        // as a rule that is doing something.
        line = QStringLiteral("%1: %2 is on the allowed list — which blocks nothing on its own, "
                              "because nothing is blocked yet.")
                   .arg(user, domain);
    } else {
        line = QStringLiteral("%1: %2 is allowed through what is blocked.").arg(user, domain);
    }
    return wrote(g, *profile, line);
}

int cmdWebDefault(const Globals &g, const QStringList &positionals, const Options &options)
{
    const QString verb = QStringLiteral("web");
    if (positionals.size() != 1) {
        fail(QStringLiteral("web: block, allow, incognito, or a user and "
                            "--all-but-listed | --only-listed? (try omahouse --help)"));
        return kUsage;
    }
    if (options.allButListed == options.onlyListed) {
        fail(QStringLiteral("web: --all-but-listed or --only-listed, and one of them"));
        return kUsage;
    }
    const QString user = positionals.first();
    if (!mayWrite(verb, paths::profilesFile(), paths::configDirIsTheSystems(), g))
        return kUsage;

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    Profile *profile = profileToChange(verb, &profiles, user, &status);
    if (!profile)
        return status;

    profile->web.defaultVerdict = options.onlyListed ? Verdict::Deny : Verdict::Allow;
    if (!saveProfiles(verb, profiles.all))
        return kUsage;

    int allowed = 0;
    for (const Rule &rule : profile->web.rules) {
        if (rule.verdict == Verdict::Allow)
            ++allowed;
    }
    sayTheReach();
    return wrote(g, *profile,
                 options.onlyListed
                     ? QStringLiteral("%1: only the listed sites open. %2 site%3 on the list.")
                           .arg(user)
                           .arg(allowed)
                           .arg(allowed == 1 ? QString() : QStringLiteral("s"))
                     : QStringLiteral("%1: every site opens except the blocked ones. %2 rule%3.")
                           .arg(user)
                           .arg(profile->web.rules.size())
                           .arg(profile->web.rules.size() == 1 ? QString() : QStringLiteral("s")));
}

int cmdWebIncognito(const Globals &g, const QStringList &positionals, const Options &options)
{
    const QString verb = QStringLiteral("web incognito");
    if (positionals.size() != 1) {
        fail(QStringLiteral("%1: which user? (try omahouse --help)").arg(verb));
        return kUsage;
    }
    if (options.allow == options.deny) {
        fail(QStringLiteral("%1: --allow or --deny, and one of them").arg(verb));
        return kUsage;
    }
    const QString user = positionals.first();
    if (!mayWrite(verb, paths::profilesFile(), paths::configDirIsTheSystems(), g))
        return kUsage;

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    Profile *profile = profileToChange(verb, &profiles, user, &status);
    if (!profile)
        return status;

    profile->web.incognitoStated = true;
    profile->web.incognito = options.deny ? Verdict::Deny : Verdict::Allow;
    if (!saveProfiles(verb, profiles.all))
        return kUsage;

    if (options.deny)
        sayTheReach();

    // An incognito window is not extra screen time -- docs/the-browser-half.md
    // §4.3. It is the same browser in the same scope under the same session
    // budget, so what it buys is anonymity about which site and not a minute of
    // anybody's day. Said here because it is the reason `--allow` is a
    // defensible answer and not a hole somebody left open.
    return wrote(g, *profile,
                 options.deny
                     ? QStringLiteral("%1: incognito windows do not open — for every account on "
                                      "this machine.")
                           .arg(user)
                     : QStringLiteral("%1: incognito windows open. They hide which site, not the "
                                      "time: the session budget counts them either way.")
                           .arg(user));
}

int cmdWeb(const Globals &g, const QStringList &positionals, const Options &options)
{
    const QString subcommand = positionals.value(0);
    if (subcommand.isEmpty()) {
        fail(QStringLiteral("web: block, allow, incognito, or a user and "
                            "--all-but-listed | --only-listed? (try omahouse --help)"));
        return kUsage;
    }
    if (subcommand == QLatin1String("block")) {
        if (!onlyTheseOptions(options, {}, QStringLiteral("web block")))
            return kUsage;
        return cmdWebRule(g, QStringLiteral("web block"), positionals.mid(1), Verdict::Deny);
    }
    if (subcommand == QLatin1String("allow")) {
        if (!onlyTheseOptions(options, {}, QStringLiteral("web allow")))
            return kUsage;
        return cmdWebRule(g, QStringLiteral("web allow"), positionals.mid(1), Verdict::Allow);
    }
    if (subcommand == QLatin1String("incognito")) {
        if (!onlyTheseOptions(options, {QStringLiteral("--allow"), QStringLiteral("--deny")},
                              QStringLiteral("web incognito")))
            return kUsage;
        return cmdWebIncognito(g, positionals.mid(1), options);
    }
    // Not a subcommand, so it is a user -- `omahouse web julia --only-listed`.
    // The three words above are reserved by being tried first, which is the
    // whole of the ambiguity: an account really named `block` cannot be reached
    // this way, and `profile default` is the shape that already had this
    // problem and solved it by being one word longer.
    if (!onlyTheseOptions(options,
                          {QStringLiteral("--all-but-listed"), QStringLiteral("--only-listed")},
                          QStringLiteral("web")))
        return kUsage;
    return cmdWebDefault(g, positionals, options);
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
    const bool site = !options.site.isEmpty();
    if (session + named + site != 1) {
        fail(QStringLiteral("limit: --session 2h, or --budget minecraft=45m, or "
                            "--site youtube.com=30m. One of them."));
        return kUsage;
    }

    // The session is the budget whose selector is `*` -- docs/design.md §2, and the
    // whole reason there is no branch for "the user's time" anywhere in the
    // core. It is spelled differently on the command line because that is how
    // people say it, and it is the same structure underneath.
    QString id = QStringLiteral("session");
    QString match = QStringLiteral("*");
    QString written = options.session;
    Selects selects = Selects::App;
    OnExhausted whenGone = OnExhausted::Logout;
    if (named || site) {
        const QString typed = named ? options.budget : options.site;
        const QString flag = named ? QStringLiteral("--budget") : QStringLiteral("--site");
        const QString example = named ? QStringLiteral("minecraft=45m")
                                      : QStringLiteral("youtube.com=30m");
        const int equals = typed.indexOf(QLatin1Char('='));
        if (equals <= 0 || equals == typed.size() - 1) {
            fail(QStringLiteral("limit: %1 wants %2 and a length of time, like %3, not '%4'")
                     .arg(flag,
                          named ? QStringLiteral("an id") : QStringLiteral("a domain"),
                          example, typed));
            return kUsage;
        }
        id = typed.left(equals);
        written = typed.mid(equals + 1);
        whenGone = named ? OnExhausted::Close : OnExhausted::Block;
        selects = named ? Selects::App : Selects::Site;
        // The same reading of a domain the web half has, and the same refusals:
        // a whole URL, a path or a scheme is refused rather than repaired into a
        // rule about its host. One routine for both, because a site a `limit`
        // named differently from the way a `web block` names it would be two
        // rows in the report and one site on the screen.
        if (site && !domainFromWhatWasTyped(QStringLiteral("limit"), id, &id))
            return kUsage;
        match = id;
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
        profile->budgets.append(Budget {id, match, selects, minutes, whenGone});
        budget = &profile->budgets.last();
    } else if (budget->selects != selects) {
        // One id, one thing. `org.freedesktop.Platform` is a scope id with dots
        // in it, so an id that is also a domain is not a shape anybody can rule
        // out -- and the same id meaning an app in the budgets and a site in the
        // ledger is a report with two rows that are the same row.
        fail(QStringLiteral("limit: %1 already has a budget called %2, and it is about %3 "
                            "rather than %4")
                 .arg(user, id, budget->isSite() ? QStringLiteral("a site")
                                                 : QStringLiteral("an app"),
                      site ? QStringLiteral("a site") : QStringLiteral("an app")));
        return kUsage;
    } else {
        // Only the number. What a budget does when it runs out is a decision
        // somebody made once, and a new limit is not a reason to take it back.
        budget->dailyMinutes = minutes;
    }
    if (!saveProfiles(QStringLiteral("limit"), profiles.all))
        return kUsage;

    const int status2 = wrote(
        g, *profile,
        QStringLiteral("%1: %2 %3 a day, and it %4 when the time is out.")
            .arg(user, id, durationFromMinutes(minutes), whenOut(budget->onExhausted)));
    // Said once, here, where somebody is deciding it: the browser's policy is
    // one file for the whole machine -- docs/design.md §11 -- so a site that
    // runs out stops opening for every account on it, the operator's included.
    // `omahouse web` says the same thing when it writes and `status` says it
    // when it prints, and none of the three says it twice.
    if (site && !g.json && budget->onExhausted == OnExhausted::Block)
        sayTheReach();
    return status2;
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

    const QString ledgerPath = paths::ledgerFile(user, today);
    QLockFile ledgerLock(ledgerPath + QStringLiteral(".lock"));
    if (!QDir().mkpath(QFileInfo(ledgerPath).absolutePath()) || !ledgerLock.tryLock(5000)) {
        fail(QStringLiteral("cannot lock the day's ledger")); return kUsage;
    }
    Ledger ledger;
    bool missing = false;
    if (!loadLedger(user, today, &ledger, &missing, &status))
        return status;

    // The day's own file, so the minutes expire when the file does: docs/design.md §4
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
    const int limit = limited ? allowanceSeconds(*profile, *budget, ledger, today) : 0;
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
        document.insert(QStringLiteral("pendingAllocation"), !profile->allocation.isEmpty());
        printJson(document);
        return kOk;
    }

    out() << QStringLiteral("%1: +%2 of %3, from %4.")
                 .arg(user, durationFromMinutes(minutes), id, grant.by);
    if (!profile->allocation.isEmpty())
        out() << QStringLiteral(" Household credit recorded; the next successful Battery sync assigns it.");
    else if (limited)
        out() << QStringLiteral(" %1 left today.").arg(humanDuration(left));
    else
        out() << QStringLiteral(" %1 has no limit, so it was only written down.").arg(id);
    out() << '\n';
    return kOk;
}

int cmdLeave(const Globals &g, const QStringList &positionals, const Options &options)
{
    // A local operator adjustment. Repeating it after consumption tops up
    // remaining time, so distributed synchronization uses absolute allocations.
    if (positionals.size() != 1) {
        fail(QStringLiteral("leave: which user? (try omahouse --help)"));
        return kUsage;
    }
    const QString user = positionals.first();

    const bool session = !options.session.isEmpty();
    const bool named = !options.budget.isEmpty();
    if (session == named) {
        fail(QStringLiteral("leave: --session 30m, or --budget minecraft=15m. One of them."));
        return kUsage;
    }

    QString id = QStringLiteral("session");
    QString written = options.session;
    if (named) {
        const int equals = options.budget.indexOf(QLatin1Char('='));
        if (equals <= 0 || equals == options.budget.size() - 1) {
            fail(QStringLiteral("leave: --budget wants an id and a length of time, like "
                                "minecraft=15m, not '%1'")
                     .arg(options.budget));
            return kUsage;
        }
        id = options.budget.left(equals);
        written = options.budget.mid(equals + 1);
    }

    int wantMinutes = 0;
    QString error;
    if (written != QLatin1String("0m") && written != QLatin1String("0")
            && !minutesFromDuration(written, &wantMinutes, &error)) {
        fail(QStringLiteral("leave: %1").arg(error));
        return kUsage;
    }

    const QDate today = QDate::currentDate();
    if (!mayWrite(QStringLiteral("leave"), paths::ledgerFile(user, today),
                  paths::stateDirIsTheSystems(), g))
        return kUsage;

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    Profile *profile = profileToChange(QStringLiteral("leave"), &profiles, user, &status);
    if (!profile)
        return status;
    if (!profile->allocation.isEmpty()) {
        fail(QStringLiteral("leave: this profile uses exclusive portions; use allocation apply"));
        return kUsage;
    }
    const Budget *budget = budgetFor(profile, id);
    if (!budget) {
        fail(QStringLiteral("leave: %1 has no budget called %2; omahouse profile show %1 "
                            "lists them")
                 .arg(user, id));
        return kMissing;
    }
    if (!budget->hasLimit()) {
        // A budget with no limit counts and never runs out, so there is nothing
        // for a number to be left of. Refused rather than written, because a row
        // in the report saying `-20m` against a budget that cannot end would be
        // a sentence nobody can read.
        fail(QStringLiteral("leave: %1 has no limit, so there is nothing to leave of it. "
                            "omahouse limit %2 --budget %1=<time> writes one")
                 .arg(id, user));
        return kUsage;
    }

    const QString ledgerPath = paths::ledgerFile(user, today);
    QLockFile ledgerLock(ledgerPath + QStringLiteral(".lock"));
    if (!QDir().mkpath(QFileInfo(ledgerPath).absolutePath()) || !ledgerLock.tryLock(5000)) {
        fail(QStringLiteral("cannot lock the day's ledger")); return kUsage;
    }
    Ledger ledger;
    bool missing = false;
    if (!loadLedger(user, today, &ledger, &missing, &status))
        return status;

    const int want = wantMinutes * 60;
    const int used = ledger.secondsFor(id);
    const int granted = ledger.grantedSeconds(id);
    const int daily = budget->dailyMinutes * 60;

    // What the total would have to be for exactly `want` to remain, floored so
    // that the day can never be worth less than nothing.
    int wantGranted = qMax(-daily, want + used - daily);
    // Whole minutes, because that is what a grant carries and what the report
    // says back. Rounded **down**, so `leave 30m` leaves at most thirty minutes
    // and never a little more: of the two ways to be wrong here, the one that
    // hands time back is the one nobody asked for.
    const int deltaSeconds = wantGranted - granted;
    const int deltaMinutes = deltaSeconds >= 0 ? deltaSeconds / 60
                                               : -((-deltaSeconds + 59) / 60);

    if (deltaMinutes != 0) {
        Grant grant;
        grant.at = QDateTime::currentDateTime();
        grant.by = operatorUser();
        grant.budget = id;
        grant.minutes = deltaMinutes;
        grant.adjustment = true;
        ledger.grants.append(grant);

        QString writeError;
        if (!writeLedger(paths::ledgerFile(user, today), ledger, &writeError)) {
            fail(QStringLiteral("leave: %1").arg(writeError));
            return kUsage;
        }
    }

    const int nowGranted = ledger.grantedSeconds(id);
    const int limit = daily + nowGranted;
    const int left = qMax(0, limit - used);

    if (g.json) {
        printJson(QJsonObject {
            {QStringLiteral("user"), user},
            {QStringLiteral("budget"), id},
            {QStringLiteral("askedSeconds"), want},
            {QStringLiteral("minutes"), deltaMinutes},
            {QStringLiteral("by"), operatorUser()},
            {QStringLiteral("usedSeconds"), used},
            {QStringLiteral("grantedSeconds"), nowGranted},
            {QStringLiteral("limitSeconds"), limit},
            {QStringLiteral("leftSeconds"), left},
        });
        return kOk;
    }

    if (deltaMinutes == 0) {
        // Said out loud, because a loop that repeats itself has to be able to
        // tell "nothing to do" from "it did not work".
        out() << QStringLiteral("%1: %2 already has %3 left today; nothing written.\n")
                     .arg(user, id, humanDuration(left));
        return kOk;
    }
    // The sign is spelled out in words rather than handed to
    // `durationFromMinutes`, which answers `0m` to anything at or below zero --
    // that is its contract everywhere else in this program, where time is only
    // ever given, and it is not this verb's to change.
    out() << QStringLiteral("%1: %2 of %3 left today, %4 %5.\n")
                 .arg(user, humanDuration(left), id,
                      durationFromMinutes(qAbs(deltaMinutes)),
                      deltaMinutes > 0 ? QStringLiteral("handed back")
                                       : QStringLiteral("taken back"))
          << QStringLiteral("       by %1.\n").arg(operatorUser());
    return kOk;
}

// -- the transport, which is two verbs and no daemon --------------------------
//
// `house` adds up the days of every machine. Getting those days to the machine
// doing the adding is transport, and transport is deliberately not a service:
// it is one machine publishing a document and another accepting it, with
// something outside deciding when. That something is a scheduled Battery script
// on the operator's computer, and it can be replaced by a person with `ssh` and
// a pipe without omahouse noticing -- which is the test of whether the seam is
// in the right place.
//
// The document is the ledger, unchanged. No summary, no envelope, no version of
// its own: the file on the machine that spent the time is already the answer to
// what a day was, and every transformation between there and the sum is a place
// the two can come to disagree.

int cmdDay(const Globals &g, const QStringList &positionals, const Options &options)
{
    if (positionals.size() != 1) {
        fail(QStringLiteral("day: which user? (try omahouse --help)"));
        return kUsage;
    }
    const QString user = positionals.first();

    QDate when = QDate::currentDate();
    if (!options.date.isEmpty()) {
        when = QDate::fromString(options.date, Qt::ISODate);
        if (!when.isValid()) {
            fail(QStringLiteral("day: --date wants a date like 2026-09-06, not '%1'")
                     .arg(options.date));
            return kUsage;
        }
    }

    Ledger ledger;
    QString error;
    bool missing = false;
    if (!readLedger(paths::ledgerFile(user, when), &ledger, &error, &missing)) {
        fail(QStringLiteral("day: %1").arg(error));
        return kUsage;
    }
    Profiles profiles;
    int profileStatus = kOk;
    if (!loadProfiles(&profiles, &profileStatus)) return profileStatus;
    for (const auto &profile : profiles.all) {
        if (profile.user == user && !profile.allocation.isEmpty()) {
            ledger.user = user;
            ledger.date = when;
            ledger.allocation = profile.allocation;
            ledger.observedAt = QDateTime::currentDateTimeUtc();
            missing = false; // An enrolled, idle machine explicitly reports zero.
        }
    }
    // `missing` and not the return value: a file that is not there is not a
    // failure to read, which is right everywhere else in omahouse and wrong
    // here. Exit 2 and no document, because a day nobody spent and a machine
    // that is not reporting must never look the same to the thing adding them
    // up -- and an empty ledger printed here would be a day of zeroes filed
    // under a real date, which is worse than no answer.
    if (missing) {
        fail(QStringLiteral("day: nothing for %1 on %2 (%3)")
                 .arg(user, when.toString(Qt::ISODate),
                      paths::ledgerFile(user, when)));
        return kMissing;
    }

    // One shape, whether or not `--json` was asked for, and the help says so.
    // This verb exists to be read by another computer; a second, prettier form
    // would be a second answer to what a day was.
    printJson(ledger.toJson());
    Q_UNUSED(g);
    return kOk;
}

int cmdCollect(const Globals &g, const QStringList &positionals)
{
    if (positionals.size() != 2) {
        fail(QStringLiteral("collect: which machine, and whose day? "
                            "(try omahouse --help)"));
        return kUsage;
    }
    const QString machine = positionals.at(0);
    const QString user = positionals.at(1);

    int status = kOk;
    Fleet fleet;
    if (!loadMachines(&fleet, &status))
        return status;
    if (indexOfMachine(fleet.all, machine) < 0) {
        // Refused, and this is the whole of what stands between the sum and a
        // stranger. A day is accepted only for a computer the household has
        // written down; anything else would let whatever can reach this machine
        // decide what the house spent.
        fail(QStringLiteral("collect: no machine called %1 in %2")
                 .arg(machine, paths::machinesFile()));
        return kMissing;
    }

    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) {
        fail(QStringLiteral("collect: cannot read the day on standard input"));
        return kUsage;
    }
    const QByteArray raw = input.readAll();
    input.close();
    if (raw.trimmed().isEmpty()) {
        fail(QStringLiteral("collect: nothing came in on standard input. The day "
                            "goes in by pipe: omahouse day %1 | omahouse collect %2 %1")
                 .arg(user, machine));
        return kUsage;
    }

    QJsonParseError parsed {};
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parsed);
    if (!document.isObject()) {
        fail(QStringLiteral("collect: what came in is not a day: %1")
                 .arg(parsed.errorString()));
        return kUsage;
    }
    Ledger ledger;
    QString error;
    if (!Ledger::fromJson(document.object(), &ledger, &error)) {
        fail(QStringLiteral("collect: what came in is not a day: %1").arg(error));
        return kUsage;
    }

    // The document names its own person and its own date, and both are checked
    // against what was asked for rather than trusted. A day filed under the
    // wrong name is time added to somebody who did not spend it, and nothing
    // downstream would ever notice: `house` reads whatever is in the directory.
    if (ledger.user != user) {
        fail(QStringLiteral("collect: that day belongs to %1, and it was offered as "
                            "%2's").arg(ledger.user, user));
        return kUsage;
    }
    if (ledger.date > QDate::currentDate()) {
        // A machine whose clock is ahead writes a day this one has not reached.
        // Accepting it puts a file in tomorrow's name that today's sum will not
        // read and tomorrow's will, which is a total that changes overnight for
        // no reason anybody can see.
        fail(QStringLiteral("collect: that day is dated %1, which has not happened "
                            "here yet").arg(ledger.date.toString(Qt::ISODate)));
        return kUsage;
    }

    const QString path = paths::elsewhereLedgerFile(machine, user, ledger.date);
    if (!mayWrite(QStringLiteral("collect"), path, paths::stateDirIsTheSystems(), g))
        return kUsage;
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        fail(QStringLiteral("collect: cannot make %1")
                 .arg(QFileInfo(path).absolutePath()));
        return kUsage;
    }
    // The same writer this machine's own days go through, so a collected day and
    // a local one are the same bytes read by the same reader.
    QLockFile snapshotLock(path + QStringLiteral(".lock"));
    if (!snapshotLock.tryLock(5000)) {
        fail(QStringLiteral("collect: cannot lock the observation")); return kUsage;
    }
    Ledger previous;
    bool absent = false;
    if (!readLedger(path, &previous, &error, &absent)) {
        fail(QStringLiteral("collect: %1").arg(error)); return kUsage;
    }
    if (!absent && previous.observedAt.isValid()) {
        if (!ledger.observedAt.isValid() || ledger.observedAt < previous.observedAt) {
            fail(QStringLiteral("collect: stale snapshot")); return kUsage;
        }
        for (auto it = previous.seconds.begin(); it != previous.seconds.end(); ++it) {
            if (ledger.secondsFor(it.key()) < it.value()) {
                fail(QStringLiteral("collect: consumption moved backwards")); return kUsage;
            }
        }
    }
    if (!writeLedger(path, ledger, &error)) {
        fail(QStringLiteral("collect: %1").arg(error));
        return kUsage;
    }

    if (g.json) {
        printJson(QJsonObject {{QStringLiteral("machine"), machine},
                               {QStringLiteral("user"), user},
                               {QStringLiteral("date"), ledger.date.toString(Qt::ISODate)},
                               {QStringLiteral("path"), path}});
        return kOk;
    }
    out() << QStringLiteral("%1: %2's %3 is in, and counts towards the house.\n")
                 .arg(machine, user, ledger.date.toString(Qt::ISODate));
    return kOk;
}

int cmdHouse(const Globals &g, const QStringList &positionals)
{
    // What the house spent, as opposed to what this computer spent.
    //
    // The profile's number is the household's: `session: 120` means two hours in
    // the house and not two hours per computer. A machine on its own enforces
    // the whole thing, which is right and is what makes one computer complete;
    // a house with three of them has to add the three up, and this is where
    // that happens.
    //
    // It reads days and nothing else. The other machines' are collected into
    // `<stateDir>/elsewhere/<machine>/`, by whatever brings them, and a machine
    // whose day is not there is simply not in the sum -- said out loud, because
    // a total quietly missing a computer is worse than no total at all.
    if (positionals.size() != 1) {
        fail(QStringLiteral("house: which user? (try omahouse --help)"));
        return kUsage;
    }
    const QString user = positionals.first();
    const QDate today = QDate::currentDate();

    int status = kOk;
    Profiles profiles;
    if (!loadProfiles(&profiles, &status))
        return status;
    const Profile *profile = nullptr;
    for (const Profile &one : profiles.all) {
        if (one.user == user)
            profile = &one;
    }
    if (!profile) {
        fail(QStringLiteral("house: no profile for %1 in %2")
                 .arg(user, paths::profilesFile()));
        return kMissing;
    }

    Fleet fleet;
    if (!loadMachines(&fleet, &status))
        return status;

    QVector<QPair<QString, Ledger>> days;
    QStringList quiet;

    // This machine first, and named `here` rather than by a hostname: the
    // household's word for a computer is in `machines.json`, and the one you are
    // sitting at has not necessarily been written down.
    Ledger mine;
    bool missing = false;
    if (!loadLedger(user, today, &mine, &missing, &status))
        return status;
    days.append({QStringLiteral("here"), mine});

    for (const Machine &machine : fleet.all) {
        Ledger theirs;
        bool absent = false;
        QString error;
        const QString path = paths::elsewhereLedgerFile(machine.name, user, today);
        if (!readLedger(path, &theirs, &error, &absent)) {
            fail(QStringLiteral("house: %1").arg(error));
            return kUsage;
        }
        if (absent) {
            quiet.append(machine.name);
            continue;
        }
        days.append({machine.name, theirs});
    }

    const QVector<HouseBudget> house = consolidate(*profile, days);

    if (g.json) {
        QJsonArray budgets;
        for (const HouseBudget &budget : house) {
            QJsonArray spent;
            for (const Contribution &one : budget.spent) {
                spent.append(QJsonObject {{QStringLiteral("machine"), one.machine},
                                          {QStringLiteral("seconds"), one.seconds}});
            }
            QJsonObject object {
                {QStringLiteral("id"), budget.id},
                {QStringLiteral("spent"), spent},
                {QStringLiteral("totalSeconds"), budget.totalSeconds},
            };
            object.insert(QStringLiteral("limitSeconds"),
                          budget.hasLimit() ? QJsonValue(budget.limitSeconds) : QJsonValue());
            object.insert(QStringLiteral("leftSeconds"),
                          budget.hasLimit() ? QJsonValue(budget.leftSeconds()) : QJsonValue());
            budgets.append(object);
        }
        QJsonArray silent;
        for (const QString &name : quiet)
            silent.append(name);
        printJson(QJsonObject {
            {QStringLiteral("user"), user},
            {QStringLiteral("date"), today.toString(Qt::ISODate)},
            {QStringLiteral("machines"), static_cast<int>(days.size())},
            {QStringLiteral("budgets"), budgets},
            {QStringLiteral("notHeardFrom"), silent},
        });
        return kOk;
    }

    out() << QStringLiteral("the house — %1, %2\n").arg(user, today.toString(Qt::ISODate));
    if (house.isEmpty()) {
        out() << QStringLiteral("\nNo budgets: %1 has no time limits, only verdicts.\n")
                     .arg(user);
        return kOk;
    }

    QStringList headers {QStringLiteral("BUDGET")};
    QVector<bool> right {false};
    for (const auto &day : days) {
        headers.append(day.first.toUpper());
        right.append(true);
    }
    headers << QStringLiteral("IN ALL") << QStringLiteral("OF") << QStringLiteral("LEFT");
    right << true << true << true;

    QVector<QStringList> rows;
    for (const HouseBudget &budget : house) {
        QStringList row {budget.id};
        for (const Contribution &one : budget.spent)
            row.append(humanDuration(one.seconds));
        row << humanDuration(budget.totalSeconds)
            << (budget.hasLimit() ? humanDuration(budget.limitSeconds) : kNothing)
            << (budget.hasLimit() ? humanDuration(budget.leftSeconds()) : kNothing);
        rows.append(row);
    }
    out() << '\n';
    printTable(headers, rows, right);

    if (!quiet.isEmpty()) {
        // The sentence that keeps the number honest. A total missing a computer
        // reads exactly like a total of a quiet afternoon, and one of those is a
        // fact while the other is a machine nobody has heard from.
        out() << QStringLiteral("\n  Nothing today from %1, so %2 not in the sum "
                                "above.\n")
                     .arg(quiet.join(QStringLiteral(", ")),
                          quiet.size() == 1 ? QStringLiteral("it is")
                                            : QStringLiteral("they are"));
    }
    return kOk;
}

// -- watch -------------------------------------------------------------------
//
// The loop of docs/design.md §5, and the one verb of this build that keeps running.
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

/// The longest a bounded run is allowed to last: a day.
///
/// `--for` exists so that something else owns the restarting -- a schedule, a
/// unit -- and a run asked to live for a week is a run whose owner has stopped
/// owning it. A day is well past any useful window and still catches the zero
/// somebody meant to type after `86400`.
constexpr int kLongestRun = 86400;

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
    // And whether anybody was in front of it. Said on the same line as the
    // counting, deliberately: `screen-off, counting session` is the sentence
    // that tells an operator these two are not connected yet, and it is the
    // sentence the time per site exists to fix.
    //
    // The site is on the same line for the same reason, and it carries whether
    // it was billed. `youtube.com not counted` beside `screen-off` is the
    // crossing of docs/design.md §5.2 visible in the journal: the browser said a
    // site, the machine said the room was dark, and the site got nothing.
    const QString site = watched.site.isEmpty()
        ? QString()
        : QStringLiteral(", %1%2")
              .arg(watched.site,
                   watched.siteCounted ? QString() : QStringLiteral(" not counted"));
    return QStringLiteral("%1, %2, %3%4")
        .arg(apps, clock, presenceReasonName(watched.presence.reason), site);
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
    case Done::What::BlockSite:
        what = QStringLiteral("%1 stopped opening (%2)")
                   .arg(done.site, paths::chromiumPolicyFile());
        break;
    case Done::What::UnblockSite:
        what = QStringLiteral("%1 opens again (%2)")
                   .arg(done.site, paths::chromiumPolicyFile());
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
        if (!watched.ledger.sites.isEmpty()) {
            out() << QStringLiteral("\n  time per site: %1\n")
                         .arg(sitesToday(watched.ledger));
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
            // Reported and never acted on. `budgets` below is what it would have
            // been with nobody in the room.
            {QStringLiteral("presence"),
             QJsonObject {
                 {QStringLiteral("present"),
                  watched.presence.known() ? QJsonValue(watched.presence.present)
                                           : QJsonValue()},
                 {QStringLiteral("reason"), presenceReasonName(watched.presence.reason)},
                 {QStringLiteral("today"), presenceToJson(watched.ledger)},
             }},
            // The site, and whether the crossing let it through. Both, because a
            // reader that only saw `today` could not tell a quiet browser from a
            // dark screen.
            {QStringLiteral("site"),
             QJsonObject {
                 {QStringLiteral("now"),
                  watched.site.isEmpty() ? QJsonValue() : QJsonValue(watched.site)},
                 {QStringLiteral("counted"), watched.siteCounted},
                 {QStringLiteral("today"), sitesToJson(watched.ledger)},
             }},
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
        // The one look at the seat and the screens this cycle took. One
        // machine's worth, above the users, because that is what it is.
        {QStringLiteral("seat"),
         QJsonObject {
             {QStringLiteral("read"), cycle.seat.read},
             {QStringLiteral("screen"), screenStateName(cycle.seat.screen)},
             {QStringLiteral("uid"),
              cycle.seat.read && cycle.seat.occupied
                  ? QJsonValue(static_cast<qint64>(cycle.seat.uid))
                  : QJsonValue()},
         }},
        {QStringLiteral("users"), users},
    };
    document.insert(QStringLiteral("blockedError"),
                    cycle.blockedError.isEmpty() ? QJsonValue()
                                                 : QJsonValue(cycle.blockedError));
    return document;
}

// -- the meter ---------------------------------------------------------------
//
// The native messaging host of docs/design.md §5.2, and it is stupid on purpose.
//
// Chromium spawns it as whoever opened the browser -- the browser spike
// §1 measured uid 1001, cwd the host's own directory, stdin and stdout pipes,
// and the child's whole session environment. So this half has no privilege and
// is not given any: it appends `<epoch> <site>` to a file in that user's runtime
// directory and does nothing else. It does not read a profile, does not know
// what a budget is, does not accumulate, and never touches the ledger -- which
// it could not write anyway, since /var/lib/omahouse is root's.
//
// `docs/the-browser-half.md` §8.1 reached instead for a socket in the root
// daemon, and called the framing and the second writer's worth of validation
// "the largest single piece of unplanned work on this page". This is what
// replaced it. The accumulation stays in `watch`, which is already root, already
// ticks every two seconds, and already knows whether anybody is in front of the
// screen; there is no new endpoint and nothing listening.
//
// It revalidates what the extension sent rather than trusting it -- the one
// thing `docs/the-browser-half.md` §2 says is worth stealing from the prior art:
// a compromised extension must not be able to push a whole URL through the wire
// by putting one in the field. Anything that is not a plausible registrable
// domain is written as `-`, which is the same thing the browser says when there
// is no site in front.

/// The largest native messaging frame that will be read, in bytes.
///
/// A message from this extension is about forty. Chromium's own ceiling is a
/// megabyte; this one is here so that a frame length read off a pipe cannot ask
/// this process to allocate one.
constexpr int kLongestFrame = 64 * 1024;

/// Exactly `wanted` bytes, or false at the end of the stream.
bool readExactly(char *into, int wanted)
{
    int got = 0;
    while (got < wanted) {
        const ssize_t some = ::read(STDIN_FILENO, into + got, static_cast<size_t>(wanted - got));
        if (some == 0)
            return false;
        if (some < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        got += static_cast<int>(some);
    }
    return true;
}

int cmdMeter(const Globals &g, const QStringList &positionals)
{
    // Chromium passes the extension's origin as the first argument. It is not
    // read: what decides which extension may reach this host is `allowed_origins`
    // in the manifest under /etc/chromium/native-messaging-hosts, which is root's
    // file, and a check here over an argument the caller chose would be a check
    // that proves nothing.
    static_cast<void>(positionals);

    if (g.json) {
        fail(QStringLiteral("meter: it speaks Chromium's native messaging on stdin and "
                            "writes no document"));
        return kUsage;
    }

    // A person, rather than a browser. Native messaging is always a pipe
    // (the browser spike measured `STDIN_ISATTY=False`), so a
    // terminal here is somebody who typed the verb to see what it does -- and
    // what it would do is sit there silently forever.
    if (::isatty(STDIN_FILENO) == 1) {
        fail(QStringLiteral("meter: this is the browser's native messaging host, not a "
                            "command. Chromium starts it through "
                            "/etc/chromium/native-messaging-hosts/com.omahouse.meter.json"));
        return kUsage;
    }

    const QString path = focusFileFor(runtimeRoot(), ::getuid());
    QString complaint;

    while (true) {
        quint32 length = 0;
        // Native byte order, which is what the protocol says and what every
        // machine this runs on spells little-endian.
        if (!readExactly(reinterpret_cast<char *>(&length), sizeof(length)))
            break;
        if (length == 0 || length > kLongestFrame) {
            fail(QStringLiteral("meter: a frame of %1 bytes is not one of ours; stopping")
                     .arg(length));
            return kUsage;
        }
        QByteArray frame(static_cast<int>(length), Qt::Uninitialized);
        if (!readExactly(frame.data(), frame.size()))
            break;

        QJsonParseError problem {};
        const QJsonDocument document = QJsonDocument::fromJson(frame, &problem);
        if (problem.error != QJsonParseError::NoError || !document.isObject()) {
            // Not fatal. A frame that will not parse is one message lost, and the
            // port stays open: the alternative is a browser that stops being
            // measured for the rest of the afternoon because of one bad message.
            continue;
        }

        // A message with no `site` in it is not a message of ours, and it is
        // dropped rather than written as anything. Writing `-` for it would be
        // this host deciding that a message it did not understand meant "there
        // is nothing in front", which is a claim about the screen it has no
        // business making.
        const QJsonValue said = document.object().value(QStringLiteral("site"));
        if (!said.isString())
            continue;

        // The one field, reduced again on this side and refused if it is not a
        // domain. `-` arrives here as "not a domain" and leaves as `-`, which is
        // exactly right: it is the browser saying there is nothing in front.
        QString site = registrableDomain(said.toString());
        if (!isPlausibleDomain(site))
            site.clear();

        QString error;
        if (!appendFocusLine(path, QDateTime::currentDateTime(), site, &error)) {
            // Said once per distinct failure and not once per message. This runs
            // for as long as a browser is open, and a full tmpfs would otherwise
            // put a line in the journal every five seconds all evening.
            if (error != complaint) {
                complaint = error;
                fail(QStringLiteral("meter: %1").arg(error));
                err().flush();
            }
        } else if (!complaint.isEmpty()) {
            complaint.clear();
        }
    }

    // The browser closed the port, which is the ordinary end. Nothing is written
    // to say so: `watch` finds the file stale within fifteen seconds and stops
    // billing, which is the same answer by a shorter road.
    return kOk;
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

// -- exclusive household allocations -----------------------------------------

int cmdAllocation(const Globals &g, const QStringList &args, const Options &options)
{
    const QString action = args.value(0);
    const QString user = args.value(1);
    if (args.size() != 2 || !QStringList{"init", "enroll", "apply", "plan", "show"}.contains(action)) {
        fail(QStringLiteral("allocation: init|enroll|apply|plan|show <user>")); return kUsage;
    }
    if (!onlyTheseOptions(options, action == QLatin1String("enroll")
                          ? QStringList{"--node", "--name"} : QStringList{}, "allocation"))
        return kUsage;
    const bool reading = action == QLatin1String("show");
    if (!reading && !mayWrite("allocation", paths::profilesFile(), paths::configDirIsTheSystems(), g))
        return kUsage;
    const QString directory = paths::stateDir() + QStringLiteral("/allocations/") + user;
    if (!reading && !QDir().mkpath(directory)) {
        fail(QStringLiteral("allocation: cannot create reservation directory")); return kUsage;
    }
    // All allocation updates of profiles share one lock; planners of one user
    // share it too, so issued credit is persisted exactly once before delivery.
    QLockFile lock(paths::stateDir() + QStringLiteral("/allocation.lock"));
    if (!reading && !lock.tryLock(5000)) {
        fail(QStringLiteral("allocation: another allocation operation is running")); return kUsage;
    }
    Profiles profiles;
    int status = kOk;
    if (!loadProfiles(&profiles, &status)) return status;
    Profile *profile = profileToChange("allocation", &profiles, user, &status);
    if (!profile) return status;
    QString error;
    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.date();
    const QString file = directory + QLatin1Char('/') + today.toString(Qt::ISODate) + QStringLiteral(".json");
    if (reading) {
        QJsonObject reservation;
        bool absent = false;
        if (!readJsonObject(file, &reservation, &error, &absent)) {
            fail(error); return kUsage;
        }
        printJson(QJsonObject{{"user", user}, {"allocation", profile->allocation},
                             {"reservation", reservation}});
        return kOk;
    }
    if (action == QLatin1String("init") || action == QLatin1String("enroll")) {
        const QString authority = action == QLatin1String("init")
                ? (profile->allocation.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                   : profile->allocation.value("authority").toString()) : options.node;
        const QString machine = action == QLatin1String("init") ? QStringLiteral("here") : options.name;
        const QJsonObject enrollment{{"authority", authority}, {"machine", machine}};
        if (!validAllocation(enrollment, &error)) { fail(error); return kUsage; }
        if (!profile->allocation.isEmpty()) {
            if (profile->allocation.value("authority").toString() != authority
                    || profile->allocation.value("machine").toString() != machine) {
                fail(QStringLiteral("allocation: already bound to another authority/machine")); return kUsage;
            }
        } else {
            bool limited = false;
            for (const auto &budget : profile->budgets) {
                if (budget.hasLimit()) {
                    limited = true;
                    if (budget.onExhausted == OnExhausted::Warn) {
                        fail(QStringLiteral("allocation: limited budgets must close, log out or block"));
                        return kUsage;
                    }
                }
            }
            if (!limited) { fail(QStringLiteral("allocation: profile has no limited budgets")); return kUsage; }
            profile->allocation = enrollment;
            profile->enabled = true;
            profile->enforce = true;
            // Exclusive credit has no extra grace allowance per machine.
            profile->graceSeconds = 0;
            if (!saveProfiles("allocation enroll", profiles.all)) return kUsage;
        }
        printJson(QJsonObject{{"user", user}, {"allocation", profile->allocation}});
        return kOk;
    }
    if (action == QLatin1String("apply")) {
        QFile input;
        if (!input.open(stdin, QIODevice::ReadOnly)) {
            fail(QStringLiteral("allocation: cannot read stdin")); return kUsage;
        }
        QJsonParseError parsing;
        const auto doc = QJsonDocument::fromJson(input.read(1024 * 1024 + 1), &parsing);
        if (parsing.error != QJsonParseError::NoError || !doc.isObject()
                || !applyAllocation(profile, doc.object(), today, &error)) {
            fail(QStringLiteral("allocation: %1").arg(error.isEmpty() ? "invalid JSON document" : error));
            return kUsage;
        }
        if (!saveProfiles("allocation apply", profiles.all)) return kUsage;
        printJson(profile->allocation);
        return kOk;
    }
    // A local day is live; each remote one must have arrived via collect.
    QVector<QPair<QString, Ledger>> days;
    Ledger mine;
    bool missing = false;
    if (!loadLedger(user, today, &mine, &missing, &status)) return status;
    mine.allocation = profile->allocation;
    mine.observedAt = now;
    days.append({"here", mine});
    Fleet fleet;
    if (!loadMachines(&fleet, &status)) return status;
    for (const auto &machine : fleet.all) {
        Ledger theirs;
        if (!readLedger(paths::elsewhereLedgerFile(machine.name, user, today), &theirs, &error, &missing)
                || missing) {
            fail(QStringLiteral("allocation: %1 needs a collected day: %2").arg(machine.name, error));
            return kUsage;
        }
        days.append({machine.name, theirs});
    }
    QJsonObject previous, plan;
    if (!readJsonObject(file, &previous, &error, &missing)
            || !planAllocations(*profile, days, previous, now, &plan, &error)
            || (plan != previous && !writeJsonAtomically(file, plan, &error))) {
        fail(QStringLiteral("allocation: %1").arg(error)); return kUsage;
    }
    printJson(plan);
    return kOk;
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

    // How long this run lives. Zero is forever, which is what a daemon wants and
    // what the unit still does.
    //
    // A length rather than a number of cycles, because what a caller knows is
    // when it wants the process back -- `*/1 * * * *` firing a run that lasts a
    // minute -- and not how many times two seconds fits into that.
    int liveFor = 0;
    if (!options.forSeconds.isEmpty()) {
        // Whole seconds, like `--interval` and unlike every other length in this
        // program. The two are siblings -- both are about this loop's clock --
        // and the duration vocabulary elsewhere reads a bare number as minutes,
        // so `--for 60` would quietly mean an hour. A flag whose plain number
        // means one thing here and another thing next to it is a flag somebody
        // gets wrong once and never trusts again.
        bool ok = false;
        liveFor = options.forSeconds.toInt(&ok);
        if (!ok || liveFor < 1 || liveFor > kLongestRun) {
            fail(QStringLiteral("watch: --for wants whole seconds between 1 and %1, "
                                "not '%2'")
                     .arg(kLongestRun)
                     .arg(options.forSeconds));
            return kUsage;
        }
        if (liveFor < interval) {
            fail(QStringLiteral("watch: --for %1s is shorter than one cycle of %2s, so "
                                "nothing would be counted")
                     .arg(liveFor)
                     .arg(interval));
            return kUsage;
        }
        if (options.once) {
            fail(QStringLiteral("watch: --once and --for ask for different things: one "
                                "cycle, or cycles for a while"));
            return kUsage;
        }
    }

    // Two files now, and both are asked for before anything is read. The day's
    // ledger under /var/lib, and -- since the teeth went in -- the
    // /etc/omahouse/blocked of docs/design.md §2, which is the half of `logout` that
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
    // The eyes -- `Presence.h`. Handed in like the teeth are, and for the same
    // reason: what it reads is the kernel's DRM attributes and root's own
    // logind, never the fiscalised user's compositor, so a run on a machine with
    // no seat and no screen says `unknown` and carries on counting.
    SeatPresenceSource presence;
    // And the browser's half of the eye -- docs/design.md §5.2. Handed in like
    // the rest, and it needs nothing of its own: it reads a file in each
    // fiscalised user's runtime directory, treats every way that file can be
    // wrong as "nothing to bill", and writes nothing anywhere.
    FileFocusSource focus;
    Watch::Options watching;
    watching.tickSeconds = interval;
    watching.dryRun = options.dryRun;
    Watch watch(&proc, &notifier, &enforcer, &presence, &focus, watching);

    const auto cycle = [&](const Profiles &current) {
        // The clock enters here and nowhere else. `evaluate` takes `now` by
        // parameter for exactly this reason, and it is what lets a two hour
        // budget be proved in microseconds by a test that never touches the
        // clock of the machine it runs on.
        // Read every cycle, beside the profiles, so a line added to
        // `/etc/omahouse/furniture` lands on the next tick and not on the next
        // restart -- the discipline `/etc/omahouse/blocked` already keeps.
        const Cycle done = watch.tick(current.all, QDateTime::currentDateTime(),
                                      furnitureOfThisMachine());
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

    // Read again every cycle, and not held from here. docs/design.md §1: the operator
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

    // The clock that ends a bounded run. A single shot rather than a countdown
    // checked in the cycle, so the last cycle a run does is a whole one: a loop
    // that stopped halfway through deciding would leave a scope SIGTERMed and
    // nobody to finish the sequence.
    QTimer until(&guard);
    if (liveFor > 0) {
        until.setSingleShot(true);
        until.setInterval(liveFor * 1000);
        QObject::connect(&until, &QTimer::timeout, &guard, [] {
            QCoreApplication::quit();
        });
        until.start();
    }

    note(QStringLiteral("omahouse: watching %1 profile%2, a cycle every %3s%4%5")
             .arg(profiles.all.size())
             .arg(profiles.all.size() == 1 ? QString() : QStringLiteral("s"))
             .arg(interval)
             .arg(liveFor > 0 ? QStringLiteral(", for %1s").arg(liveFor) : QString())
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
  machines                 the computers this household owns
  house <user>             what the house spent today, every machine added up

  machine kind             what this computer is: alone, manager or managed.
                           Alone is the ordinary state and nothing is missing

Writing, and root needed — the studio gets there by pkexec:
  profile add <user>       a new profile: observing, and allowing everything
              [--name "Júlia"] [--create-user]
  day <user> [--date YYYY-MM-DD]
                           what this computer spent, as a document another one
                           can read. Exit 2 when there is no such day
  machine link <user@host> --at host:port [--name "the kitchen laptop"]
                           install omahouse on another computer and link it
                           here, over ssh. One command, and the far machine
                           gets the whole of omahouse rather than an agent
  machine invite --at host:port
                           print the line that lets another computer trust this
                           one. Run it where the operator sits
  machine prepare --invite <line> --at host:port
                           put this computer under a household. Run it as root
                           on the computer being added
  machine add <name> --pair <line>
                           the other end of prepare: trust that computer, and
                           write it down
  machine add <name> [--node omk1_…] [--at host:port]
                           write a computer down without pairing it; a note to
                           self, and nothing can be asked of it yet
  machine remove <name>    out of the list. The machine itself is untouched
  collect <machine> <user> take in a day another computer spent, on stdin:
                           omahouse day julia | ssh study omahouse collect …
  profile remove <user> [--keep-account]
  profile enforce <user> --on | --off
  profile default <user> --allow | --deny
  allow <user> <app> [--limit 45m]   let it run, and put it on the clock
  deny  <user> <app>                 do not let it run
  limit <user> --session 2h | --budget minecraft=45m | --site youtube.com=30m
  allocation init|enroll|apply|plan|show <user>
      enroll: --node <authority> --name <machine>; apply reads JSON from stdin
  grant <user> --session 10m | --budget minecraft=15m
  leave <user> --session 30m | --budget minecraft=15m
                           what should be left of today, rather than what to
                           take away — so a loop can say it twice
  web block <user> <domain>          do not let that site open
  web allow <user> <domain>          let it open through what is blocked
  web <user> --all-but-listed | --only-listed
  web incognito <user> --allow | --deny

The loop, which is the only verb that keeps running:
  watch [--interval 2] [--once] [--for 60] [--dry-run]
                           count what every profile has open, warn before the
                           time is out, and then close it. --dry-run decides
                           and touches nothing, --once is a single cycle, and
                           --for is cycles for that many seconds and then out,
                           which is what lets a schedule own the restarting

Started by the browser, and never by a person:
  meter                    Chromium's native messaging host. It appends the site
                           in the front tab to a file in the caller's own runtime
                           directory, and does nothing else — no ledger, no
                           budget, no privilege. `watch` is what counts it

An app is named by the id of its scope: `chromium`, `org.freedesktop.Platform`.
`omahouse status` lists the ones that are open, and says when a scope holds
something other than what its name says.

A length of time is 45m, 2h, 1h30m, or a bare 90 for minutes. Anything else is
refused rather than taken for minutes.

Time per site is counted the way time per app is: the browser extension reports
the site in the front tab, `watch` bills it only while the screen says somebody
is there, and `status` and `report` show the total. `limit --site` puts a day's
worth on it, and running out stops the site opening until the day turns. Without
a limit it only counts, and a day with no extension on the machine looks exactly
as it always did.

A site is named by its bare domain — `youtube.com` — and that covers its
subdomains. The web rules of every profile are composed into one Chromium
managed policy, so they hold for every account on this machine and not only for
the one they were written for. The most restrictive profile wins, and there is
no precedence between them. Taking the last web rule away takes the file away.

Globals:
  --json, -j               one JSON document on stdout   (OMAHOUSE_JSON)
  --version, -V
  --help, -h

Files:
  /etc/omahouse/profiles.json          who is under rules   (OMAHOUSE_CONFIG_DIR)
  /etc/omahouse/blocked                who may not log in   (OMAHOUSE_CONFIG_DIR)
  /etc/chromium/policies/managed/omahouse.json
                                       which sites open (OMAHOUSE_CHROMIUM_POLICY_DIR)
  /var/lib/omahouse/<user>/<date>.json the day's ledger     (OMAHOUSE_STATE_DIR)
  /sys/fs/cgroup                       the scopes           (OMAHOUSE_CGROUP_ROOT)
  /proc                                what a scope runs    (OMAHOUSE_PROC_ROOT)
  notify-send                          how a warning is said (OMAHOUSE_NOTIFY_SEND)
  systemd-run                          how it reaches a session (OMAHOUSE_SYSTEMD_RUN)
  loginctl                             how a session is ended (OMAHOUSE_LOGINCTL)
  /sys/class/drm                       whether a screen is lit (OMAHOUSE_DRM_ROOT)
  /run/user/<uid>/omahouse/focus       the site in the front tab (OMAHOUSE_RUNTIME_ROOT)

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

    // The scheduler may apply a portion while an operator edits a rule. Both
    // replace profiles.json, so serialize the entire read/modify/write cycle.
    const bool profileWrite = QStringList{"allow", "deny", "limit"}.contains(verb)
        || (verb == QLatin1String("profile") && !QStringList{"show", "list"}.contains(rest.value(1)))
        || (verb == QLatin1String("web") && rest.value(1) != QLatin1String("show"))
        || (verb == QLatin1String("allocation") && rest.value(1) != QLatin1String("show"));
    QLockFile profileLock(paths::profilesFile() + QStringLiteral(".lock"));
    if (profileWrite) {
        if (!mayWrite(verb, paths::profilesFile(), paths::configDirIsTheSystems(), g))
            return kUsage;
        if (!QDir().mkpath(paths::configDir()) || !profileLock.tryLock(5000)) {
            fail(QStringLiteral("%1: cannot lock profiles for writing").arg(verb));
            return kUsage;
        }
    }

    if (verb == QLatin1String("allocation"))
        return cmdAllocation(g, positionals, options);
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
    if (verb == QLatin1String("machines")) {
        if (!onlyTheseOptions(options, {}, verb))
            return kUsage;
        return cmdMachines(g);
    }
    if (verb == QLatin1String("machine"))
        return cmdMachine(g, positionals, options);
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
    if (verb == QLatin1String("web"))
        return cmdWeb(g, positionals, options);
    if (verb == QLatin1String("limit")) {
        if (!onlyTheseOptions(options,
                              {QStringLiteral("--session"), QStringLiteral("--budget"),
                               QStringLiteral("--site")},
                              verb))
            return kUsage;
        return cmdLimit(g, positionals, options);
    }
    if (verb == QLatin1String("day")) {
        if (!onlyTheseOptions(options, {QStringLiteral("--date")}, verb))
            return kUsage;
        return cmdDay(g, positionals, options);
    }
    if (verb == QLatin1String("collect")) {
        if (!onlyTheseOptions(options, {}, verb))
            return kUsage;
        return cmdCollect(g, positionals);
    }
    if (verb == QLatin1String("house")) {
        if (!onlyTheseOptions(options, {}, verb))
            return kUsage;
        return cmdHouse(g, positionals);
    }
    if (verb == QLatin1String("leave")) {
        if (!onlyTheseOptions(options, {QStringLiteral("--session"),
                                        QStringLiteral("--budget")}, verb))
            return kUsage;
        return cmdLeave(g, positionals, options);
    }
    if (verb == QLatin1String("grant")) {
        if (!onlyTheseOptions(options, {QStringLiteral("--session"), QStringLiteral("--budget")},
                              verb))
            return kUsage;
        return cmdGrant(g, positionals, options);
    }
    if (verb == QLatin1String("meter")) {
        if (!onlyTheseOptions(options, {}, verb))
            return kUsage;
        return cmdMeter(g, positionals);
    }

    if (verb == QLatin1String("watch")) {
        if (!onlyTheseOptions(options,
                              {QStringLiteral("--interval"), QStringLiteral("--for"),
                               QStringLiteral("--once"),
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
