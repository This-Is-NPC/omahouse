#include <QtTest>

#include "Blocked.h"
#include "Enforce.h"
#include "Focus.h"
#include "FocusFile.h"
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

/// Everything the teeth would have done, and nothing that happened.
///
/// The same seam as `Recorder`, and here it is not a convenience: the thing this
/// suite is asserting about is a signal sent to a process and a session ended,
/// and there is no version of "run the real one and see" that is safe on the
/// machine this suite runs on. testing.md §6 says so in as many words, and
/// `whyNotCloseable` refuses the temporary tree below anyway -- this records what
/// would have been asked for on the far side of that refusal.
class Bite : public Enforcer {
public:
    bool terminate(const AppScope &scope, int *count, QString *error) override
    {
        termed.append(scope.unit);
        if (count)
            *count = scope.pidCount;
        if (!refuse)
            return true;
        if (error)
            *error = QStringLiteral("no such process");
        return false;
    }

    bool killTree(const AppScope &scope, QString *error) override
    {
        killed.append(scope.unit);
        if (!refuse)
            return true;
        if (error)
            *error = QStringLiteral("cgroup.kill is not writable");
        return false;
    }

    bool endSessions(const QString &user, QString *error) override
    {
        ended.append(user);
        if (!refuse)
            return true;
        if (error)
            *error = QStringLiteral("logind is not answering");
        return false;
    }

    QStringList termed;
    QStringList killed;
    QStringList ended;
    bool refuse = false;
};

/// A seat and a screen the suite decides, rather than the ones the machine
/// running it happens to have.
///
/// The same seam `Recorder` and `Bite` are, and here it is what keeps the suite
/// honest: the machine this runs on has a screen, and a loop whose presence came
/// from that screen would pass or fail depending on whether somebody had walked
/// away from the build.
///
/// Unread by default, which is a seat nobody looked at and a presence of
/// `unknown`. That is deliberately the state that writes nothing: every case
/// here that is about counting time asserts a ledger, and a fake that quietly
/// added presence seconds to all of them would be a fixture editing the thing
/// under test.
class Eyes : public PresenceSource {
public:
    SeatReading reading;
    int looks = 0;

    SeatReading readSeat() override
    {
        ++looks;
        return reading;
    }

    /// The seat showing `uid` with the screen lit -- somebody at the machine.
    void showing(uid_t uid)
    {
        reading.read = true;
        reading.occupied = true;
        reading.uid = uid;
        reading.screen = ScreenState::On;
    }
};

/// The browser's half of the eye, without a browser -- docs/design.md §5.2.
///
/// It hands back the bytes of a focus file, which is exactly what the real one
/// does: the whole of the reasoning about what those bytes mean is pure and
/// lives in `src/core/Focus.cpp`, so the only thing left to drive here is the
/// crossing with presence.
class Tabs : public FocusSource {
public:
    QByteArray blob;
    int reads = 0;

    QByteArray tail(uid_t) override
    {
        ++reads;
        return blob;
    }

    /// One line, as the native messaging host would have appended it a moment
    /// ago.
    void saying(const QString &site, const QDateTime &now, int secondsAgo = 1)
    {
        blob = focusLineFor(now.addSecs(-secondsAgo), site);
    }
};

QStringList unitsOf(const QVector<Done> &done, Done::What what)
{
    QStringList units;
    for (const Done &one : done) {
        if (one.what == what)
            units.append(one.unit);
    }
    return units;
}

const Done *firstOf(const QVector<Done> &done, Done::What what)
{
    for (const Done &one : done) {
        if (one.what == what)
            return &one;
    }
    return nullptr;
}

} // namespace

// The loop of docs/design.md §5, against a machine that is a directory.
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

    /// The names in the `blocked` of this test's own configuration directory.
    QStringList blockedNames() const
    {
        QString error;
        return readBlocked(paths::blockedFile(), &error);
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
        // And the configuration, which since stage 7 the loop writes to as well:
        // `/etc/omahouse/blocked` is the half of `logout` that does the work,
        // and a suite that left this pointed at /etc would be a suite trying to
        // refuse whoever runs it a login.
        qputenv("OMAHOUSE_CONFIG_DIR", QFile::encodeName(m_box + QStringLiteral("/etc")));
    }

    void cleanupTestCase()
    {
        qunsetenv("OMAHOUSE_STATE_DIR");
        qunsetenv("OMAHOUSE_CONFIG_DIR");
    }

    // -- the cycle -----------------------------------------------------------

    // docs/design.md §5 steps 2 to 6, in one turn: the scopes are listed, the budgets
    // with a live app matching them are debited once each, and the day is
    // written.
    void countsOneTickAndWritesTheDay()
    {
        makeSession();
        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});

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
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {5, false});

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
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});

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
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        recorder.refuse = true;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});

        const Cycle first = watch.tick({profile()}, QDateTime(day, QTime(19, 0, 0)));
        QCOMPARE(first.users.first().said.size(), 1);
        QVERIFY(!first.users.first().said.first().sent);
        QCOMPARE(first.users.first().said.first().error, QStringLiteral("no session bus"));

        const Cycle second = watch.tick({profile()}, QDateTime(day, QTime(19, 0, 2)));
        QVERIFY(second.users.first().said.isEmpty());
        QCOMPARE(recorder.notes.size(), 1);
    }

    // -- the teeth ------------------------------------------------------------

    // `enforce: false` is what a profile is born with, and under it the core
    // emits no Close and no Logout at all -- so a run of it cannot close
    // anything or end anybody's session by any path, whatever else is true.
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
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});
        const Cycle cycle = watch.tick({observing}, QDateTime(day, QTime(19, 0, 0)));

        const Watched &watched = cycle.users.first();
        QVERIFY(watched.done.isEmpty());
        QVERIFY(watched.logouts.isEmpty());
        QVERIFY(!watched.blocked);
        QVERIFY(teeth.termed.isEmpty() && teeth.killed.isEmpty() && teeth.ended.isEmpty());
        for (const Said &said : watched.said)
            QCOMPARE(said.decision.kind, Decision::Kind::Warn);

        // Two apps and one nameless scope refused, and two budgets out: all of
        // it said and none of it acted on. The `tmux-spawn` scope is in the
        // count because no rule can name it and the default is what is left,
        // which under `deny` is a refusal like any other.
        QCOMPARE(watched.said.size(), 5);
        QCOMPARE(recorder.notes.size(), 5);
        QVERIFY(recorder.notes.first().summary.endsWith(QStringLiteral("is not allowed")));

        // Nobody was refused a login either, and the file was not so much as
        // created: an empty answer that matches an absent file is no write.
        QVERIFY(!QFile::exists(paths::blockedFile()));

        // And it is still counted: the seconds happened whatever the verdict
        // was.
        QCOMPARE(readBack(day).secondsFor(QStringLiteral("session")), 120 * 60 + 2);
    }

    // The rule of testing.md §6, and the reason this suite can exist at all.
    //
    // The tree above is a directory in $TMPDIR whose `cgroup.procs` hold pids
    // somebody typed -- 4000, 4100, 4200 -- and those are real pids on the
    // machine running this. So with the teeth fully on, every Close is refused
    // by `whyNotCloseable`, the refusal says why, and no signal is asked for.
    // A build that got this wrong would be a test suite killing whatever
    // happened to be process 4000.
    void aTreeThatIsNotTheMachinesIsCountedAndNeverSignalled()
    {
        makeSession();
        const QDate day(2026, 9, 3);
        seedLedger(day, {{QStringLiteral("chromium"), 45 * 60}});

        Profile enforcing = profile();
        enforcing.enforce = true;
        enforcing.graceSeconds = 0;

        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});
        const Cycle cycle = watch.tick({enforcing}, QDateTime(day, QTime(19, 0, 0)));

        const Watched &watched = cycle.users.first();
        // The decision was made, and it names the scope `cgroup.kill` would have
        // been found under -- so the accounting is whole and only the act is
        // missing.
        QCOMPARE(unitsOf(watched.done, Done::What::Terminate),
                 QStringList({QStringLiteral("app-Hyprland-chromium-031bdc27.scope")}));
        const Done *refused = firstOf(watched.done, Done::What::Terminate);
        QVERIFY(refused);
        QVERIFY(!refused->carriedOut);
        QVERIFY2(refused->error.contains(QStringLiteral("/sys/fs/cgroup")),
                 qPrintable(refused->error));
        QCOMPARE(refused->app, QStringLiteral("chromium"));

        // Nothing was signalled and nothing was killed.
        QVERIFY(teeth.termed.isEmpty());
        QVERIFY(teeth.killed.isEmpty());

        // And the seconds were still counted, which is the other half of the
        // promise: a run that may not bite still keeps the books.
        QCOMPARE(readBack(day).secondsFor(QStringLiteral("chromium")), 45 * 60 + 2);
    }

    // -- logout is two things ------------------------------------------------

    // docs/design.md §2, and the order it has to happen in. The name goes into
    // `blocked` first, and only a user who is really in that file has their
    // session ended -- because poc/findings.md round 2 measured a bare
    // `terminate-user` being undone by the tty1 autologin in the same breath.
    void logoutWritesTheBlockBeforeItEndsAnything()
    {
        makeSession();
        const QDate day(2026, 9, 3);
        seedLedger(day, {{QStringLiteral("session"), 120 * 60}});

        Profile enforcing = profile();
        enforcing.enforce = true;
        enforcing.graceSeconds = 0;

        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});
        const Cycle cycle = watch.tick({enforcing}, QDateTime(day, QTime(19, 0, 0)));

        const Watched &watched = cycle.users.first();
        QCOMPARE(watched.logouts.size(), 1);
        QCOMPARE(watched.logouts.first().budgetId, QStringLiteral("session"));

        // The file, written and readable, with exactly the one name in it.
        QCOMPARE(blockedNames(), QStringList({m_user}));
        QCOMPARE(cycle.blocked, QStringList({m_user}));
        QVERIFY(cycle.blockedError.isEmpty());
        QVERIFY(watched.blocked);
        const Done *block = firstOf(watched.done, Done::What::Block);
        QVERIFY(block);
        QVERIFY(block->carriedOut);

        // And the termination refused, because this suite's `blocked` is a file
        // in $TMPDIR that no PAM stack reads. Ending a session behind it would
        // be an eviction with no lock on the door -- and on this machine it
        // would be the session of whoever is running the suite.
        const Done *ended = firstOf(watched.done, Done::What::EndSession);
        QVERIFY(ended);
        QVERIFY(!ended->carriedOut);
        QVERIFY2(ended->error.contains(QStringLiteral("/etc/omahouse")),
                 qPrintable(ended->error));
        QVERIFY(teeth.ended.isEmpty());
    }

    // The other half of §2's promise: the name comes out on its own.
    //
    // It comes out because it is never remembered. Every cycle asks today's
    // ledger whether a logout still stands, so a grant, `enforce --off`, a
    // profile switched off and the turn of the day all take the name out
    // without any of them knowing the file exists.
    void theBlockIsLiftedByWhateverGaveTheTimeBack()
    {
        makeSession();
        const QDate day(2026, 9, 3);
        seedLedger(day, {{QStringLiteral("session"), 120 * 60}});

        Profile enforcing = profile();
        enforcing.enforce = true;
        enforcing.graceSeconds = 0;

        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});

        watch.tick({enforcing}, QDateTime(day, QTime(19, 0, 0)));
        QCOMPARE(blockedNames(), QStringList({m_user}));

        // An operator hands over ten minutes with the game still open --
        // docs/design.md §1 -- and the very next cycle lets them back in.
        Ledger day3 = readBack(day);
        Grant grant;
        grant.at = QDateTime(day, QTime(19, 0, 1));
        grant.by = QStringLiteral("howl");
        grant.budget = QStringLiteral("session");
        grant.minutes = 10;
        day3.grants.append(grant);
        QString error;
        QVERIFY2(writeLedger(ledgerPath(day), day3, &error), qPrintable(error));

        const Cycle after = watch.tick({enforcing}, QDateTime(day, QTime(19, 0, 2)));
        QCOMPARE(blockedNames(), QStringList());
        QVERIFY(after.users.first().logouts.isEmpty());
        QVERIFY(!after.users.first().blocked);
        const Done *unblock = firstOf(after.users.first().done, Done::What::Unblock);
        QVERIFY(unblock);
        QVERIFY(unblock->carriedOut);
    }

    // The turn of the day, which is the one docs/design.md §2 names in as many words.
    // Nothing does it: the balance is a file per day, so tomorrow's ledger is
    // empty, so no logout stands, so the name is not written.
    void theTurnOfTheDayLiftsTheBlock()
    {
        makeSession();
        const QDate day(2026, 9, 3);
        seedLedger(day, {{QStringLiteral("session"), 120 * 60}});

        Profile enforcing = profile();
        enforcing.enforce = true;
        enforcing.graceSeconds = 0;

        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});

        watch.tick({enforcing}, QDateTime(day, QTime(23, 59, 58)));
        QCOMPARE(blockedNames(), QStringList({m_user}));

        watch.tick({enforcing}, QDateTime(QDate(2026, 9, 4), QTime(0, 0, 0)));
        QCOMPARE(blockedNames(), QStringList());
    }

    // And the block outlives the session it ended, which is the whole point.
    //
    // `terminate-user` is what left them without one, so a loop that only asked
    // about users who are logged in would take the name straight back out and
    // let them in again -- which is the theatre round 2 measured, arrived at by
    // a different road.
    void aUserWithNoSessionStaysBlocked()
    {
        // No app.slice at all: nobody is logged in.
        const QDate day(2026, 9, 3);
        seedLedger(day, {{QStringLiteral("session"), 120 * 60}});

        Profile enforcing = profile();
        enforcing.enforce = true;
        enforcing.graceSeconds = 0;

        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});
        const Cycle cycle = watch.tick({enforcing}, QDateTime(day, QTime(19, 0, 0)));

        QVERIFY(!cycle.users.first().session);
        QCOMPARE(blockedNames(), QStringList({m_user}));
        // Nothing to end, so nothing is asked of logind: they are already out.
        QVERIFY(unitsOf(cycle.users.first().done, Done::What::EndSession).isEmpty());
        // And the day was not written by a cycle that counted nothing.
        QCOMPARE(readBack(day).secondsFor(QStringLiteral("session")), 120 * 60);
    }

    // A profile switched off, and one whose account is gone, are both "no rules
    // apply" -- so neither may leave somebody shut out of their own machine.
    void aProfileThatStopsApplyingLetsThemBackIn()
    {
        const QDate day(2026, 9, 3);
        seedLedger(day, {{QStringLiteral("session"), 120 * 60}});

        Profile enforcing = profile();
        enforcing.enforce = true;
        enforcing.graceSeconds = 0;

        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});
        watch.tick({enforcing}, QDateTime(day, QTime(19, 0, 0)));
        QCOMPARE(blockedNames(), QStringList({m_user}));

        Profile off = enforcing;
        off.enabled = false;
        watch.tick({off}, QDateTime(day, QTime(19, 0, 2)));
        QCOMPARE(blockedNames(), QStringList());

        // And a profile removed altogether: the user is not in the cycle at all,
        // so they are not in the set the file is written from.
        watch.tick({enforcing}, QDateTime(day, QTime(19, 0, 4)));
        QCOMPARE(blockedNames(), QStringList({m_user}));
        watch.tick({}, QDateTime(day, QTime(19, 0, 6)));
        QCOMPARE(blockedNames(), QStringList());
    }

    // A dry run says who it would shut out and shuts nobody out. The mode that
    // makes the loop safe to point at a machine nobody meant to fiscalise, kept
    // true of the half of it that can lock a door.
    void aDryRunNeverWritesTheBlock()
    {
        makeSession();
        const QDate day(2026, 9, 3);
        seedLedger(day, {{QStringLiteral("session"), 120 * 60}});

        Profile enforcing = profile();
        enforcing.enforce = true;
        enforcing.graceSeconds = 0;

        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, true});
        const Cycle cycle = watch.tick({enforcing}, QDateTime(day, QTime(19, 0, 0)));

        const Watched &watched = cycle.users.first();
        QCOMPARE(watched.logouts.size(), 1);
        const Done *block = firstOf(watched.done, Done::What::Block);
        QVERIFY(block);
        QVERIFY(!block->carriedOut);
        QVERIFY(!QFile::exists(paths::blockedFile()));
        QVERIFY(teeth.ended.isEmpty() && teeth.termed.isEmpty() && teeth.killed.isEmpty());
    }

    // -- the day -------------------------------------------------------------

    // docs/design.md §4 keeps one file per day and the balance resets at the local turn
    // of the date. The loop reads and writes by the date of `now`, so a session
    // open across midnight simply starts writing tomorrow's file.
    void theTurnOfTheDayStartsANewFile()
    {
        makeSession();
        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});

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
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, true});
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
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});
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
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});
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
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});
        const Cycle cycle = watch.tick({orphan}, QDateTime(QDate(2026, 9, 3), QTime(19, 0, 0)));

        QVERIFY(!cycle.users.first().account);
        QVERIFY(!cycle.users.first().session);
        QVERIFY(recorder.notes.isEmpty());
    }

    // -- presence ------------------------------------------------------------

    // The seat is one thing about one machine, so it is read once and handed to
    // every profile in the cycle. Three profiles asking `loginctl` three times a
    // tick would be paying per person for an answer that is not about a person.
    void theSeatIsReadOncePerCycleAndNotOncePerUser()
    {
        makeSession();
        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        eyes.showing(m_uid);
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});

        Profile second = profile();
        second.user = QStringLiteral("omahouse-nobody-4f8ae1c3");
        const Cycle cycle =
            watch.tick({profile(), second}, QDateTime(QDate(2026, 9, 3), QTime(19, 0, 0)));

        QCOMPARE(eyes.looks, 1);
        QCOMPARE(cycle.users.size(), 2);
        QVERIFY(cycle.seat.read);
        QCOMPARE(cycle.users.first().presence.reason, Presence::Reason::Using);
        QVERIFY(cycle.users.first().presence.present);
    }

    // The day's file gains the presence beside the budgets, and the budgets are
    // the same either way. This is the whole of what this step promised: the
    // screen going dark is written down and changes nothing about what an app is
    // billed -- docs/design.md §5 bills running time, and that is not this
    // step's to take back.
    void presenceIsWrittenBesideTheBudgetsAndNeverIntoThem()
    {
        makeSession();
        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        eyes.showing(m_uid);
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});
        const QDate day(2026, 9, 3);

        watch.tick({profile()}, QDateTime(day, QTime(19, 0, 0)));
        Ledger written = readBack(day);
        QCOMPARE(written.presenceSecondsFor(QStringLiteral("using")), 2);
        QCOMPARE(written.secondsFor(QStringLiteral("session")), 2);

        // The screen goes dark with every one of those apps still running. The
        // presence says so; the session budget goes on being spent exactly as it
        // was.
        eyes.reading.screen = ScreenState::Off;
        const Cycle dark = watch.tick({profile()}, QDateTime(day, QTime(19, 0, 2)));
        QCOMPARE(dark.users.first().presence.reason, Presence::Reason::ScreenOff);
        QVERIFY(!dark.users.first().presence.present);

        written = readBack(day);
        QCOMPARE(written.presenceSecondsFor(QStringLiteral("using")), 2);
        QCOMPARE(written.presenceSecondsFor(QStringLiteral("screen-off")), 2);
        QCOMPARE(written.secondsFor(QStringLiteral("session")), 4);
        QCOMPARE(written.secondsFor(QStringLiteral("chromium")), 4);
    }

    // The crossing of docs/design.md §5.2, and the case the whole browser half
    // exists to be able to pass.
    //
    // `.temp/spike-extension.md` §5 measured a browser answering `active`
    // ninety-four times through thirty minutes of an empty room, with the
    // monitor physically off for twenty-five of them. A meter that trusted the
    // browser would bill YouTube all night beside a sleeping child. So the name
    // comes from the browser and the presence comes from the kernel, and a
    // second is only billed where the two agree.
    void aSiteInFrontOfADarkScreenDebitsNothing()
    {
        makeSession();
        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        eyes.showing(m_uid);
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});
        const QDate day(2026, 9, 3);

        const QDateTime lit(day, QTime(19, 0, 0));
        tabs.saying(QStringLiteral("youtube.com"), lit);
        const Cycle watching = watch.tick({profile()}, lit);
        QCOMPARE(watching.users.first().site, QStringLiteral("youtube.com"));
        QVERIFY(watching.users.first().siteCounted);
        QCOMPARE(readBack(day).siteSecondsFor(QStringLiteral("youtube.com")), 2);

        // The screen goes dark with the same tab in front, and the browser goes
        // on saying so -- because it does not know either.
        eyes.reading.screen = ScreenState::Off;
        const QDateTime dark(day, QTime(19, 0, 2));
        tabs.saying(QStringLiteral("youtube.com"), dark);
        const Cycle nobody = watch.tick({profile()}, dark);

        // Still reported, so the journal can say `youtube.com not counted` and
        // an operator can see that omahouse knows the difference. Not billed.
        QCOMPARE(nobody.users.first().site, QStringLiteral("youtube.com"));
        QVERIFY(!nobody.users.first().siteCounted);
        QCOMPARE(readBack(day).siteSecondsFor(QStringLiteral("youtube.com")), 2);

        // And the app half is untouched by any of it: the browser scope was
        // running, so it was billed, screen or no screen. docs/design.md §5 bills
        // running time and this step does not get to change that.
        QCOMPARE(readBack(day).secondsFor(QStringLiteral("chromium")), 4);
    }

    // Every way the file can be wrong is one way: nothing is billed. The
    // arithmetic of each is the pure suite's; what is asserted here is that the
    // loop treats them all alike and goes on counting everything else.
    void aFocusFileThatIsWrongInAnyWayBillsNothing()
    {
        makeSession();
        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        eyes.showing(m_uid);
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});
        const QDate day(2026, 9, 3);
        QDateTime now(day, QTime(19, 0, 0));

        const QVector<QByteArray> wrong {
            QByteArray(),                                   // she deleted it
            QByteArray("nonsense\n"),                        // she filled it with rubbish
            focusLineFor(now.addSecs(-600), QStringLiteral("youtube.com")),  // it went stale
            focusLineFor(now.addSecs(600), QStringLiteral("youtube.com")),   // she dated it ahead
            focusLineFor(now, QString()),                   // nothing is in front
        };
        for (const QByteArray &blob : wrong) {
            tabs.blob = blob;
            const Cycle cycle = watch.tick({profile()}, now);
            QVERIFY2(cycle.users.first().site.isEmpty(), blob.constData());
            QVERIFY(!cycle.users.first().siteCounted);
            now = now.addSecs(2);
        }
        QVERIFY(readBack(day).sites.isEmpty());
        // And the day went on being counted throughout, which is the half that
        // makes evading this pointless: she wins anonymity, not minutes.
        QCOMPARE(readBack(day).secondsFor(QStringLiteral("session")), 10);
    }

    // A machine with no extension on it -- which is every machine today -- writes
    // exactly the ledger it has always written.
    void aCycleWithNoFocusSourceWritesNoSites()
    {
        makeSession();
        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        eyes.showing(m_uid);
        Watch watch(&reader, &recorder, &teeth, &eyes, nullptr, Watch::Options {2, false});
        const QDate day(2026, 9, 3);

        const Cycle cycle = watch.tick({profile()}, QDateTime(day, QTime(19, 0, 0)));
        QVERIFY(cycle.users.first().site.isEmpty());
        QVERIFY(readBack(day).sites.isEmpty());
        QVERIFY(!readBack(day).toJson().contains(QStringLiteral("sites")));
    }

    // A loop that was never given eyes must say it cannot see. Nothing is
    // written about presence at all, because an hour of `unknown` in the day's
    // file is an hour of somebody's afternoon described as a failure to look.
    void aCycleWithNoPresenceSourceWritesNoPresence()
    {
        makeSession();
        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, nullptr, &tabs, Watch::Options {2, false});
        const QDate day(2026, 9, 3);

        const Cycle cycle = watch.tick({profile()}, QDateTime(day, QTime(19, 0, 0)));
        QCOMPARE(cycle.users.first().presence.reason, Presence::Reason::Unknown);
        QVERIFY(!cycle.seat.read);

        const Ledger written = readBack(day);
        QVERIFY(written.presence.isEmpty());
        QCOMPARE(written.secondsFor(QStringLiteral("session")), 2);
    }

    // A screen going dark is a change of shape and gets its line. Without it,
    // the journal of an evening where somebody walked away at eight reads
    // exactly like the journal of an evening where they did not.
    void aChangeOfPresenceIsWorthALine()
    {
        makeSession();
        const Proc reader = proc();
        Recorder recorder;
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        eyes.showing(m_uid);
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});
        const QDate day(2026, 9, 3);

        QVERIFY(watch.tick({profile()}, QDateTime(day, QTime(19, 0, 0)))
                    .users.first()
                    .worthSaying);
        QVERIFY(!watch.tick({profile()}, QDateTime(day, QTime(19, 0, 2)))
                     .users.first()
                     .worthSaying);

        eyes.reading.screen = ScreenState::Off;
        QVERIFY(watch.tick({profile()}, QDateTime(day, QTime(19, 0, 4)))
                    .users.first()
                    .worthSaying);
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
        Bite teeth;
        Eyes eyes;
        Tabs tabs;
        Watch watch(&reader, &recorder, &teeth, &eyes, &tabs, Watch::Options {2, false});
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
