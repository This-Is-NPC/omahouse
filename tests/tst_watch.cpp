#include <QtTest>

#include "Ledger.h"
#include "Notify.h"
#include "Paths.h"
#include "Proc.h"
#include "Profile.h"
#include "Users.h"
#include "Watch.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <unistd.h>

using namespace omahouse;

namespace {

/// A cgroup, as far as this walk is concerned: a directory with a `cgroup.procs`
/// in it holding one pid per line. The numbers are never read, only counted.
void makeCgroup(const QString &path, int pidCount, int firstPid = 4000)
{
    QVERIFY2(QDir().mkpath(path), qPrintable(path));
    QFile file(path + QStringLiteral("/cgroup.procs"));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Text), qPrintable(path));
    for (int i = 0; i < pidCount; ++i)
        file.write(QByteArray::number(firstPid + i) + '\n');
}

/// Everything that would have been said, and nothing on anybody's screen.
///
/// The seam the loop takes a `Notifier` for. A suite that had to stand up a
/// session bus and a notification daemon to prove that a warning fires once is a
/// suite that only runs on the machine it was written on.
class Recorder : public Notifier {
public:
    struct Note {
        uid_t uid;
        QString summary;
        QString body;
    };

    bool notify(uid_t uid, const QString &summary, const QString &body, QString *error) override
    {
        notes.append(Note {uid, summary, body});
        if (!refuse)
            return true;
        if (error)
            *error = QStringLiteral("no session bus");
        return false;
    }

    QVector<Note> notes;
    /// A screen that will not listen. Counted all the same, because the ledger
    /// is written before a word is said and a warning that could not be
    /// delivered must not come back every two seconds for the rest of the day.
    bool refuse = false;
};

} // namespace

// The loop of spec.md §5, against a machine that is a directory.
//
// Everything the loop touches moves: the cgroup tree by `OMAHOUSE_CGROUP_ROOT`,
// the ledgers by `OMAHOUSE_STATE_DIR`, the notification by the `Notifier` it is
// handed, and the clock by the `now` it passes down to `evaluate`. So a day of
// budget is proved in microseconds, by an unprivileged suite, with no session
// open and nothing on anybody's screen -- which is the same promise stage 2 made
// about the core, kept one layer up.
//
// The account is the one running the suite, because the loop resolves a name to
// a uid through NSS and an invented name is a profile with no account, which is
// its own case below.
class WatchTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tree;
    QString m_box;
    QString m_user;
    uid_t m_uid = 0;

    QString appSlice() const
    {
        return QStringLiteral("%1/cgroup/user.slice/user-%2.slice/user@%2.service/app.slice")
            .arg(m_box)
            .arg(static_cast<qulonglong>(m_uid));
    }

    Proc proc() const { return Proc(m_box + QStringLiteral("/cgroup"), m_box + QStringLiteral("/proc")); }

    QString ledgerPath(const QDate &date) const
    {
        return paths::ledgerFile(m_user, date);
    }

    /// A session with a browser, an editor and a terminal multiplexer in it. The
    /// last has no id -- `tmux-spawn-<uuid>.scope` is not an app scope name --
    /// and the development machine had twenty-three of them.
    void makeSession()
    {
        makeCgroup(appSlice() + QStringLiteral("/app-graphical.slice/")
                       + QStringLiteral("app-Hyprland-chromium-031bdc27.scope"),
                   19, 4000);
        makeCgroup(appSlice() + QStringLiteral("/app-code-3579042.scope"), 12, 4100);
        makeCgroup(appSlice() + QStringLiteral("/app-graphical.slice/")
                       + QStringLiteral("tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope"),
                   18, 4200);
    }

    /// Two hours of session that ends the session, forty-five minutes of
    /// chromium that closes it, and an editor that is only counted.
    Profile profile() const
    {
        Profile profile;
        profile.user = m_user;
        profile.displayName = QStringLiteral("Júlia");
        profile.enabled = true;
        profile.enforce = false;
        profile.defaultVerdict = Verdict::Allow;
        profile.warnAt = {10, 5, 1};
        profile.graceSeconds = 20;
        profile.budgets = {
            Budget {QStringLiteral("session"), QStringLiteral("*"), 120, OnExhausted::Logout},
            Budget {QStringLiteral("chromium"), QStringLiteral("chromium"), 45,
                    OnExhausted::Close},
            Budget {QStringLiteral("code"), QStringLiteral("code"), 0, OnExhausted::Warn},
        };
        return profile;
    }

    void seedLedger(const QDate &date, const QMap<QString, int> &seconds)
    {
        Ledger ledger;
        ledger.user = m_user;
        ledger.date = date;
        ledger.seconds = seconds;
        QString error;
        QVERIFY2(writeLedger(ledgerPath(date), ledger, &error), qPrintable(error));
    }

    Ledger readBack(const QDate &date)
    {
        Ledger ledger;
        QString error;
        bool missing = false;
        const bool read = readLedger(ledgerPath(date), &ledger, &error, &missing);
        QTest::qVerify(read && !missing, "the day was written", qPrintable(error), __FILE__,
                       __LINE__);
        return ledger;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tree.isValid());
        m_uid = ::getuid();
        m_user = currentUser();
        QVERIFY2(!m_user.isEmpty(), "the suite needs an account of its own to be about");
    }

    /// A machine of its own per test: a ledger one test wrote is a ledger the
    /// next one would have to know about.
    void init()
    {
        m_box = m_tree.filePath(QString::fromLatin1(QTest::currentTestFunction()));
        QVERIFY(QDir().mkpath(m_box));
        qputenv("OMAHOUSE_STATE_DIR", QFile::encodeName(m_box + QStringLiteral("/var")));
    }

    void cleanupTestCase() { qunsetenv("OMAHOUSE_STATE_DIR"); }

    // -- the cycle -----------------------------------------------------------

    // spec.md §5 steps 2 to 6, in one turn: the scopes are listed, the budgets
    // with a live app matching them are debited once each, and the day is
    // written.
    void countsOneTickAndWritesTheDay()
    {
        makeSession();
        const Proc reader = proc();
        Recorder recorder;
        Watch watch(&reader, &recorder, Watch::Options {2, false});

        const QDateTime now(QDate(2026, 9, 3), QTime(19, 0, 0));
        const Cycle cycle = watch.tick({profile()}, now);

        QCOMPARE(cycle.users.size(), 1);
        const Watched &watched = cycle.users.first();
        QVERIFY(watched.account);
        QVERIFY(watched.session);
        QVERIFY(watched.wrote);
        QVERIFY(watched.error.isEmpty());

        // The scope with no id is counted and has no name to be counted under.
        // It stays out of `apps`, which is a list of words for a sentence, and
        // it is reported as itself so that a journal line about a session spent
        // entirely inside a terminal does not read `no apps`.
        QCOMPARE(watched.apps, QStringList({QStringLiteral("chromium"), QStringLiteral("code")}));
        QCOMPARE(watched.unnamedScopes, 1);

        // Once per budget and never once per process: the nineteen processes of
        // one Chromium are one app, which is the whole reason the identity is
        // the scope.
        QCOMPARE(watched.debited,
                 QStringList({QStringLiteral("chromium"), QStringLiteral("code"),
                              QStringLiteral("session")}));

        const Ledger written = readBack(now.date());
        QCOMPARE(written.user, m_user);
        QCOMPARE(written.date, now.date());
        QCOMPARE(written.secondsFor(QStringLiteral("session")), 2);
        QCOMPARE(written.secondsFor(QStringLiteral("chromium")), 2);
        QCOMPARE(written.secondsFor(QStringLiteral("code")), 2);
        QVERIFY(recorder.notes.isEmpty());
    }

    // The tick is the interval, and one number for both: a loop that wakes every
    // two seconds and debits three is a day that ends forty minutes early.
    void debitsTheIntervalItWasGiven()
    {
        makeSession();
        const Proc reader = proc();
        Recorder recorder;
        Watch watch(&reader, &recorder, Watch::Options {5, false});

        const QDateTime now(QDate(2026, 9, 3), QTime(19, 0, 0));
        watch.tick({profile()}, now);
        QCOMPARE(readBack(now.date()).secondsFor(QStringLiteral("session")), 5);
    }

    // -- what gets said ------------------------------------------------------

    // The mark fires once, and the ledger is what remembers it. Without that,
    // "warn at five minutes left" is a notification every two seconds for five
    // minutes.
    void saysAWarningOnceAndNotAgain()
    {
        makeSession();
        const QDate day(2026, 9, 3);
        seedLedger(day, {{QStringLiteral("session"), 120 * 60 - 300}});

        const Proc reader = proc();
        Recorder recorder;
        Watch watch(&reader, &recorder, Watch::Options {2, false});

        const Cycle first = watch.tick({profile()}, QDateTime(day, QTime(19, 0, 0)));
        QCOMPARE(first.users.first().said.size(), 1);
        const Said &said = first.users.first().said.first();
        QCOMPARE(said.decision.kind, Decision::Kind::Warn);
        QCOMPARE(said.decision.reason, Decision::Reason::Warning);
        QCOMPARE(said.decision.budgetId, QStringLiteral("session"));
        QVERIFY(said.sent);
        QCOMPARE(recorder.notes.size(), 1);
        QCOMPARE(recorder.notes.first().uid, m_uid);
        QCOMPARE(recorder.notes.first().summary, QStringLiteral("5 minutes left"));
        // The clock time of §6's example, and it is the caller that works it
        // out: the core hands back seconds.
        QCOMPARE(recorder.notes.first().body, QStringLiteral("Your session runs out at 19:04."));

        const Cycle second = watch.tick({profile()}, QDateTime(day, QTime(19, 0, 2)));
        QVERIFY(second.users.first().said.isEmpty());
        QCOMPARE(recorder.notes.size(), 1);

        // And the mark is in the day, which is what a restarted daemon reads to
        // know it has already spoken.
        QVERIFY(readBack(day).hasWarned(QStringLiteral("session"), 5));
    }

    // A screen that will not listen is a line in the journal and nothing else.
    // The ledger is written first on purpose: a warning recorded and not
    // delivered is one lost warning, and a warning delivered against a ledger
    // that did not land is one every two seconds until midnight.
    void aWarningThatCouldNotBeDeliveredIsNotSaidAgain()
    {
        makeSession();
        const QDate day(2026, 9, 3);
        seedLedger(day, {{QStringLiteral("session"), 120 * 60 - 300}});

        const Proc reader = proc();
        Recorder recorder;
        recorder.refuse = true;
        Watch watch(&reader, &recorder, Watch::Options {2, false});

        const Cycle first = watch.tick({profile()}, QDateTime(day, QTime(19, 0, 0)));
        QCOMPARE(first.users.first().said.size(), 1);
        QVERIFY(!first.users.first().said.first().sent);
        QCOMPARE(first.users.first().said.first().error, QStringLiteral("no session bus"));

        const Cycle second = watch.tick({profile()}, QDateTime(day, QTime(19, 0, 2)));
        QVERIFY(second.users.first().said.isEmpty());
        QCOMPARE(recorder.notes.size(), 1);
    }

    // -- the teeth, which are not in --------------------------------------

    // `enforce: false` is what a profile is born with, and under it the core
    // emits no Close and no Logout at all -- so there is nothing for this stage
    // to hold back, and a run of it cannot end anybody's session by any path.
    void observingCarriesOutNothingButWarnings()
    {
        makeSession();
        const QDate day(2026, 9, 3);
        // Both budgets already spent, and one app refused outright.
        seedLedger(day, {{QStringLiteral("session"), 120 * 60},
                         {QStringLiteral("chromium"), 45 * 60}});

        Profile observing = profile();
        observing.enforce = false;
        observing.defaultVerdict = Verdict::Deny;

        const Proc reader = proc();
        Recorder recorder;
        Watch watch(&reader, &recorder, Watch::Options {2, false});
        const Cycle cycle = watch.tick({observing}, QDateTime(day, QTime(19, 0, 0)));

        const Watched &watched = cycle.users.first();
        QVERIFY(watched.notYet.isEmpty());
        for (const Said &said : watched.said)
            QCOMPARE(said.decision.kind, Decision::Kind::Warn);

        // Two apps and one nameless scope refused, and two budgets out: all of
        // it said and none of it acted on. The `tmux-spawn` scope is in the
        // count because no rule can name it and the default is what is left,
        // which under `deny` is a refusal like any other.
        QCOMPARE(watched.said.size(), 5);
        QCOMPARE(recorder.notes.size(), 5);
        QVERIFY(recorder.notes.first().summary.endsWith(QStringLiteral("is not allowed")));

        // And it is still counted: the seconds happened whatever the verdict
        // was.
        QCOMPARE(readBack(day).secondsFor(QStringLiteral("session")), 120 * 60 + 2);
    }

    // With the teeth switched on the core does emit them, and this stage still
    // carries out only the warnings. plan.md keeps Close and Logout for stage 7
    // and the VM, so they are named in the cycle and stepped over -- which is
    // the one thing a reader of stage 6 has to be able to check.
    void theTeethAreNamedAndNotUsed()
    {
        makeSession();
        const QDate day(2026, 9, 3);
        seedLedger(day, {{QStringLiteral("session"), 120 * 60}});

        Profile enforcing = profile();
        enforcing.enforce = true;
        enforcing.defaultVerdict = Verdict::Deny;
        // No window, so the action lands on this very tick rather than on one
        // twenty seconds from now.
        enforcing.graceSeconds = 0;

        const Proc reader = proc();
        Recorder recorder;
        Watch watch(&reader, &recorder, Watch::Options {2, false});
        const Cycle cycle = watch.tick({enforcing}, QDateTime(day, QTime(19, 0, 0)));

        const Watched &watched = cycle.users.first();
        QVERIFY(!watched.notYet.isEmpty());
        bool logout = false;
        bool close = false;
        for (const Decision &decision : watched.notYet) {
            QVERIFY(decision.kind != Decision::Kind::Warn);
            logout = logout || decision.kind == Decision::Kind::Logout;
            close = close || decision.kind == Decision::Kind::Close;
        }
        QVERIFY(logout);
        QVERIFY(close);

        // Everything that was carried out was a warning, and nothing else
        // reached the machine.
        QCOMPARE(recorder.notes.size(), watched.said.size());
        for (const Said &said : watched.said)
            QCOMPARE(said.decision.kind, Decision::Kind::Warn);
    }

    // -- the day -------------------------------------------------------------

    // spec.md §4 keeps one file per day and the balance resets at the local turn
    // of the date. The loop reads and writes by the date of `now`, so a session
    // open across midnight simply starts writing tomorrow's file.
    void theTurnOfTheDayStartsANewFile()
    {
        makeSession();
        const Proc reader = proc();
        Recorder recorder;
        Watch watch(&reader, &recorder, Watch::Options {2, false});

        const QDate before(2026, 9, 3);
        const QDate after(2026, 9, 4);
        watch.tick({profile()}, QDateTime(before, QTime(23, 59, 59)));
        watch.tick({profile()}, QDateTime(after, QTime(0, 0, 1)));

        QVERIFY(QFile::exists(ledgerPath(before)));
        QVERIFY(QFile::exists(ledgerPath(after)));
        QCOMPARE(readBack(before).secondsFor(QStringLiteral("session")), 2);
        QCOMPARE(readBack(after).secondsFor(QStringLiteral("session")), 2);
        QCOMPARE(readBack(after).date, after);
    }

    // -- the doors that make it safe to run here -----------------------------

    void aDryRunDecidesAndTouchesNothing()
    {
        makeSession();
        const QDate day(2026, 9, 3);
        seedLedger(day, {{QStringLiteral("session"), 120 * 60 - 300}});
        const QDateTime stamp = QFileInfo(ledgerPath(day)).lastModified();

        const Proc reader = proc();
        Recorder recorder;
        Watch watch(&reader, &recorder, Watch::Options {2, true});
        const Cycle cycle = watch.tick({profile()}, QDateTime(day, QTime(19, 0, 0)));

        // It decided, and it wrote the decision down nowhere.
        const Watched &watched = cycle.users.first();
        QCOMPARE(watched.said.size(), 1);
        QCOMPARE(watched.said.first().words.summary, QStringLiteral("5 minutes left"));
        QVERIFY(!watched.said.first().sent);
        QVERIFY(!watched.wrote);
        QVERIFY(recorder.notes.isEmpty());
        QCOMPARE(readBack(day).secondsFor(QStringLiteral("session")), 120 * 60 - 300);
        QCOMPARE(QFileInfo(ledgerPath(day)).lastModified(), stamp);
    }

    // Nobody logged in: no app.slice under user@<uid>.service, and nothing to
    // count. Not an error, and not a file either -- a day with no ledger is a
    // day nobody spent.
    void aUserWithNoSessionIsNotCounted()
    {
        const Proc reader = proc();
        Recorder recorder;
        Watch watch(&reader, &recorder, Watch::Options {2, false});
        const QDateTime now(QDate(2026, 9, 3), QTime(19, 0, 0));
        const Cycle cycle = watch.tick({profile()}, now);

        QVERIFY(cycle.users.first().account);
        QVERIFY(!cycle.users.first().session);
        QVERIFY(!QFile::exists(ledgerPath(now.date())));
    }

    void aProfileThatIsOffIsNotCounted()
    {
        makeSession();
        Profile off = profile();
        off.enabled = false;

        const Proc reader = proc();
        Recorder recorder;
        Watch watch(&reader, &recorder, Watch::Options {2, false});
        const QDateTime now(QDate(2026, 9, 3), QTime(19, 0, 0));
        const Cycle cycle = watch.tick({off}, now);

        QVERIFY(!cycle.users.first().enabled);
        QVERIFY(!cycle.users.first().session);
        QVERIFY(!QFile::exists(ledgerPath(now.date())));
    }

    // A profile can outlive the account it was written for, and one can be
    // written before its account exists. Neither is a reason to invent a uid.
    void aProfileWithNoAccountIsNotCounted()
    {
        makeSession();
        Profile orphan = profile();
        orphan.user = QStringLiteral("omahouse-nobody-4f8ae1c3");

        const Proc reader = proc();
        Recorder recorder;
        Watch watch(&reader, &recorder, Watch::Options {2, false});
        const Cycle cycle = watch.tick({orphan}, QDateTime(QDate(2026, 9, 3), QTime(19, 0, 0)));

        QVERIFY(!cycle.users.first().account);
        QVERIFY(!cycle.users.first().session);
        QVERIFY(recorder.notes.isEmpty());
    }

    // -- the journal ---------------------------------------------------------

    // A two second loop that logs every cycle writes the same sentence
    // seventeen hundred times a day and buries the one line that mattered. So a
    // cycle that looks like the one before it is worth nothing, and the first
    // one always is -- a daemon that starts and says nothing cannot be told from
    // one that failed to start.
    void aCycleThatChangedNothingIsWorthNoLine()
    {
        makeSession();
        const Proc reader = proc();
        Recorder recorder;
        Watch watch(&reader, &recorder, Watch::Options {2, false});
        const QDate day(2026, 9, 3);

        QVERIFY(watch.tick({profile()}, QDateTime(day, QTime(19, 0, 0)))
                    .users.first()
                    .worthSaying);
        QVERIFY(!watch.tick({profile()}, QDateTime(day, QTime(19, 0, 2)))
                     .users.first()
                     .worthSaying);

        // An app opening is a change of shape, and it gets a line.
        makeCgroup(appSlice() + QStringLiteral("/app-graphical.slice/")
                       + QStringLiteral("app-flatpak-org.freedesktop.Platform-2351381583.scope"),
                   4, 4400);
        QVERIFY(watch.tick({profile()}, QDateTime(day, QTime(19, 0, 4)))
                    .users.first()
                    .worthSaying);
    }

    // -- the words -----------------------------------------------------------

    // The core hands back a Reason and a number of seconds and writes no
    // sentence. These are the sentences, and what makes them the caller's job is
    // that they depend on things the core is right to ignore: what the budget
    // does when it runs out, and whether the teeth are in at all.
    void theWordsComeFromTheReason()
    {
        Profile enforcing = profile();
        enforcing.enforce = true;
        const QDateTime now(QDate(2026, 9, 3), QTime(19, 30, 0));

        Decision denied;
        denied.reason = Decision::Reason::Denied;
        denied.scopeUnit = QStringLiteral("app-Hyprland-steam-9f2c11ab.scope");
        const Words refused = wordsFor(enforcing, denied, QStringLiteral("steam"), now);
        QCOMPARE(refused.summary, QStringLiteral("steam is not allowed"));
        QCOMPARE(refused.body,
                 QStringLiteral("It is not one of the programs released for Júlia."));

        // A scope with no id is named by its unit rather than by a blank: a
        // person can at least tell which window it is about.
        const Words unnamed = wordsFor(enforcing, denied, QString(), now);
        QCOMPARE(unnamed.summary,
                 QStringLiteral("app-Hyprland-steam-9f2c11ab.scope is not allowed"));

        Decision warning;
        warning.reason = Decision::Reason::Warning;
        warning.budgetId = QStringLiteral("chromium");
        warning.secondsLeft = 298;
        // Rounded up: the mark somebody configured was five minutes, and a
        // warning that fires at 298 seconds calling itself four would make the
        // configuration look wrong.
        QCOMPARE(wordsFor(enforcing, warning, QString(), now).summary,
                 QStringLiteral("5 minutes left"));
        QCOMPARE(wordsFor(enforcing, warning, QString(), now).body,
                 QStringLiteral("chromium closes at 19:34."));

        warning.secondsLeft = 60;
        QCOMPARE(wordsFor(enforcing, warning, QString(), now).summary,
                 QStringLiteral("1 minute left"));

        // The same warning under a profile that is only watching. Nothing is
        // going to close, so nothing says it will: a notification that promises
        // what does not happen teaches whoever reads it to ignore the next one.
        Profile observing = profile();
        QCOMPARE(wordsFor(observing, warning, QString(), now).body,
                 QStringLiteral("chromium runs out at 19:31."));

        // The session budget is the one whose selector is `*`, and to whoever is
        // being warned it is not a budget at all.
        warning.budgetId = QStringLiteral("session");
        warning.secondsLeft = 300;
        QCOMPARE(wordsFor(enforcing, warning, QString(), now).body,
                 QStringLiteral("Your session ends at 19:35."));

        Decision grace;
        grace.reason = Decision::Reason::GraceStarted;
        grace.budgetId = QStringLiteral("session");
        grace.secondsLeft = 20;
        QCOMPARE(wordsFor(enforcing, grace, QString(), now).summary, QStringLiteral("Time is up"));
        QCOMPARE(wordsFor(enforcing, grace, QString(), now).body,
                 QStringLiteral("You will be logged out in 20 seconds."));
        grace.budgetId = QStringLiteral("chromium");
        QCOMPARE(wordsFor(enforcing, grace, QString(), now).body,
                 QStringLiteral("chromium closes in 20 seconds."));

        Decision exhausted;
        exhausted.reason = Decision::Reason::Exhausted;
        exhausted.budgetId = QStringLiteral("chromium");
        QCOMPARE(wordsFor(enforcing, exhausted, QString(), now).body,
                 QStringLiteral("chromium is closing now."));
        QCOMPARE(wordsFor(observing, exhausted, QString(), now).body,
                 QStringLiteral("chromium is out of time for today. Nothing is being closed."));
    }

    // -- how a warning gets there --------------------------------------------

    // The command of poc/findings.md round 2, kept exactly as it was measured
    // against a real notification daemon. The bus address is built from the uid
    // because /run/user/<uid>/bus is where logind puts it, and a daemon outside
    // the session has no other way to learn it.
    void theCommandIsTheOneThePocMeasured()
    {
        QCOMPARE(notifyCommand(1001, 0, QStringLiteral("Faltam 5 minutos"),
                               QStringLiteral("Minecraft fecha às 19:35")),
                 QStringList({
                     QStringLiteral("systemd-run"),
                     QStringLiteral("--uid=1001"),
                     QStringLiteral("--setenv=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1001/bus"),
                     QStringLiteral("notify-send"),
                     QStringLiteral("Faltam 5 minutos"),
                     QStringLiteral("Minecraft fecha às 19:35"),
                 }));

        // Watching the account it is running as, which is what makes stage 6
        // something that can be tried on a development machine without root:
        // the session bus is already in this environment.
        QCOMPARE(notifyCommand(1001, 1001, QStringLiteral("Time is up"), QStringLiteral("Now")),
                 QStringList({QStringLiteral("notify-send"), QStringLiteral("Time is up"),
                              QStringLiteral("Now")}));

        // And the same door `$OMAHOUSE_USERADD` is, for the same reason: the end
        // to end suite proves the command without a notification appearing on
        // the screen of whoever runs it.
        qputenv("OMAHOUSE_NOTIFY_SEND", "/tmp/omahouse-said");
        QCOMPARE(notifySendProgram(), QStringLiteral("/tmp/omahouse-said"));
        QCOMPARE(notifyCommand(1001, 1001, QStringLiteral("a"), QStringLiteral("b")).first(),
                 QStringLiteral("/tmp/omahouse-said"));
        qunsetenv("OMAHOUSE_NOTIFY_SEND");
        QCOMPARE(notifySendProgram(), QStringLiteral("notify-send"));
        QCOMPARE(systemdRunProgram(), QStringLiteral("systemd-run"));
    }
};

int runWatchTests(int argc, char **argv)
{
    WatchTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_watch.moc"
