#include <QtTest>

#include "AppScope.h"
#include "Blocked.h"
#include "Enforce.h"
#include "Paths.h"
#include "Proc.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace omahouse;

namespace {

/// A scope as `Proc::scopesFor` hands one back: a unit name and the directory it
/// was found in.
AppScope scopeAt(const QString &cgroupPath)
{
    AppScope scope;
    scope.cgroupPath = cgroupPath;
    scope.unit = cgroupPath.section(QLatin1Char('/'), -1);
    scope.id = scopeIdFromUnit(scope.unit, nullptr);
    scope.pidCount = 1;
    return scope;
}

QString appSlice(uid_t uid, const QString &root = Proc::systemCgroupRoot())
{
    return QStringLiteral("%1/user.slice/user-%2.slice/user@%2.service/app.slice")
        .arg(root)
        .arg(static_cast<qulonglong>(uid));
}

QString sessionSlice(uid_t uid, const QString &root = Proc::systemCgroupRoot())
{
    return QStringLiteral("%1/user.slice/user-%2.slice/user@%2.service/session.slice")
        .arg(root)
        .arg(static_cast<qulonglong>(uid));
}

} // namespace

// The guard of plan.md stage 7, proved on the machine the teeth may not run on.
//
// This is the point of `whyNotCloseable` being a pure function over three
// strings: the thing it protects -- somebody's compositor -- is exactly the
// thing that cannot be put at risk to test it. Everything below is a question
// about paths, and every answer is checked in both directions, because a guard
// that refuses everything is a guard nobody would notice was broken until the
// day a budget ran out in the VM.
class EnforceTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tree;

private slots:
    void initTestCase() { QVERIFY(m_tree.isValid()); }

    // -- what may be closed --------------------------------------------------

    // An ordinary app scope on the real tree, which is the only shape that is
    // ever allowed through.
    void anAppScopeOnTheRealTreeMayBeClosed()
    {
        const QString slice = appSlice(1001);
        const AppScope chromium = scopeAt(slice + QStringLiteral("/app-graphical.slice/")
                                          + QStringLiteral("app-Hyprland-chromium-031bdc27.scope"));
        QCOMPARE(chromium.id, QStringLiteral("chromium"));
        QCOMPARE(whyNotCloseable(Proc::systemCgroupRoot(), slice, chromium), QString());

        // Directly under app.slice, which is where a `.desktop` launch lands.
        const AppScope code = scopeAt(slice + QStringLiteral("/app-code-3579042.scope"));
        QCOMPARE(whyNotCloseable(Proc::systemCgroupRoot(), slice, code), QString());

        // And a scope nothing can name. It is still an app scope, it is still
        // under app.slice, and spec.md §5 is explicit that under an allowlist
        // with the teeth in it is closed like anything else.
        const AppScope nameless =
            scopeAt(slice + QStringLiteral("/app-graphical.slice/")
                    + QStringLiteral("tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope"));
        QVERIFY(nameless.id.isEmpty());
        QCOMPARE(whyNotCloseable(Proc::systemCgroupRoot(), slice, nameless), QString());
    }

    // -- the one that justifies the whole file -------------------------------

    // `session.slice` is never judged -- spec.md §5 -- and this is the failure
    // that kills the product: an allowlist that takes Hyprland down two seconds
    // after somebody logs in, leaving them at a greeter with no explanation. The
    // prefix test alone already refuses it; the name is looked for as well,
    // because a check that is redundant today is what catches the refactor that
    // made it necessary.
    void nothingInSessionSliceMayEverBeClosed()
    {
        const QString slice = appSlice(1001);

        // The compositor's own unit, which a raw `hyprctl dispatch exec` was
        // measured landing inside of (poc/findings.md round 2).
        AppScope compositor = scopeAt(sessionSlice(1001)
                                      + QStringLiteral("/wayland-wm@hyprland.desktop.service"));
        QVERIFY(!whyNotCloseable(Proc::systemCgroupRoot(), slice, compositor).isEmpty());

        // A real `.scope` in session.slice, so that the refusal is not just the
        // ".service" rule wearing a different hat.
        AppScope inSession = scopeAt(sessionSlice(1001)
                                     + QStringLiteral("/app-Hyprland-sleep-7865852f.scope"));
        const QString why = whyNotCloseable(Proc::systemCgroupRoot(), slice, inSession);
        QVERIFY2(!why.isEmpty(), "a scope under session.slice was allowed through");

        // And with the session slice handed in as the root, which is the shape
        // of a caller that got the two trees the wrong way round.
        QVERIFY(!whyNotCloseable(Proc::systemCgroupRoot(), sessionSlice(1001), inSession)
                     .isEmpty());
    }

    // Another account's app.slice is another account's business. The uid in the
    // path is the whole of what tells them apart, so the prefix is built from
    // the uid the decision was about.
    void anotherUsersAppSliceIsNotThisUsers()
    {
        const AppScope theirs =
            scopeAt(appSlice(1002) + QStringLiteral("/app-code-3579042.scope"));
        QVERIFY(!whyNotCloseable(Proc::systemCgroupRoot(), appSlice(1001), theirs).isEmpty());
    }

    // A prefix without its separator matches a sibling whose name merely starts
    // the same way.
    void aSiblingThatStartsTheSameWayIsNotInside()
    {
        const QString slice = appSlice(1001);
        AppScope neighbour = scopeAt(slice + QStringLiteral(".other/app-code-1.scope"));
        QVERIFY(!whyNotCloseable(Proc::systemCgroupRoot(), slice, neighbour).isEmpty());
    }

    // Slices and services are systemd's own grouping and other packages'
    // declarations. Only a scope is an app somebody opened.
    void onlyAScopeIsAnApp()
    {
        const QString slice = appSlice(1001);
        QVERIFY(!whyNotCloseable(Proc::systemCgroupRoot(), slice,
                                 scopeAt(slice + QStringLiteral("/dconf.service")))
                     .isEmpty());
        QVERIFY(!whyNotCloseable(Proc::systemCgroupRoot(), slice,
                                 scopeAt(slice + QStringLiteral("/app-graphical.slice")))
                     .isEmpty());
        AppScope nothing;
        QVERIFY(!whyNotCloseable(Proc::systemCgroupRoot(), slice, nothing).isEmpty());
    }

    // -- the tree has to be the machine's own --------------------------------

    // testing.md §6 made into code. The end to end suite builds a cgroup tree in
    // $TMPDIR whose `cgroup.procs` hold pids a person typed -- 4000, 4100 -- and
    // those are real pids on the machine running the suite. So the same scope,
    // the same shape, the same everything, is readable and countable there and
    // is never signalled.
    void aTreeThatIsNotTheMachinesIsNeverSignalled()
    {
        const QString fake = m_tree.filePath(QStringLiteral("cgroup"));
        const QString slice = appSlice(1001, fake);
        const AppScope scope =
            scopeAt(slice + QStringLiteral("/app-Hyprland-chromium-031bdc27.scope"));

        const QString why = whyNotCloseable(fake, slice, scope);
        QVERIFY2(!why.isEmpty(), "a scope in a temporary tree was allowed through");
        QVERIFY(why.contains(QStringLiteral("/sys/fs/cgroup")));

        // And it is the root that is refused, not the path: the very same scope
        // read from the real tree is fine.
        QCOMPARE(whyNotCloseable(Proc::systemCgroupRoot(), appSlice(1001),
                                 scopeAt(appSlice(1001)
                                         + QStringLiteral("/app-Hyprland-chromium-1.scope"))),
                 QString());
    }

    // The two spellings of one path mean one thing, or the check is one somebody
    // can talk their way out of by adding a slash.
    void theRealTreeSpeltOddlyIsStillTheRealTree()
    {
        const QString odd = QStringLiteral("/sys/fs/cgroup/");
        const QString slice = appSlice(1001, QStringLiteral("/sys/fs/cgroup/"));
        QCOMPARE(whyNotCloseable(odd, slice,
                                 scopeAt(slice + QStringLiteral("/app-code-1.scope"))),
                 QString());
    }

    // -- the block and the termination are one action ------------------------

    // spec.md §2. A `blocked` in $TMPDIR is a file no PAM stack reads, so a
    // `terminate-user` behind it is a session ended with nothing holding the
    // door -- which poc/findings.md round 2 measured being undone by the tty1
    // autologin in the same breath.
    void aBlockedFileNobodyReadsEndsNobodysSession()
    {
        QVERIFY(!whyNotBlockable(m_tree.path()).isEmpty());
        QVERIFY(!whyNotBlockable(QStringLiteral("/etc/omahouse-elsewhere")).isEmpty());
        QCOMPARE(whyNotBlockable(QStringLiteral("/etc/omahouse")), QString());
        QCOMPARE(whyNotBlockable(QStringLiteral("/etc/omahouse/")), QString());
    }

    // -- the file itself -----------------------------------------------------

    // One name per line, because `pam_listfile` reads it and this is the one
    // file omahouse writes whose format somebody else chose.
    void theBlockedFileIsOneNamePerLine()
    {
        const QString path = m_tree.filePath(QStringLiteral("etc/blocked"));

        // Not there is not a failure. It is the state of every machine where
        // nobody has run out of time, and `onerr=succeed` makes it refuse
        // nobody.
        QString error;
        bool missing = false;
        QCOMPARE(readBlocked(path, &error, &missing), QStringList());
        QVERIFY(missing);
        QVERIFY(error.isEmpty());

        QVERIFY2(writeBlocked(path, {QStringLiteral("julia"), QStringLiteral("theo")}, &error),
                 qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("julia\ntheo\n"));
        file.close();

        // 0644: PAM opens this in the process that is authenticating, and a mode
        // only root can read is a block that never blocks.
        const QFileDevice::Permissions mode = QFile::permissions(path);
        QVERIFY(mode.testFlag(QFileDevice::ReadOwner));
        QVERIFY(mode.testFlag(QFileDevice::WriteOwner));
        QVERIFY(mode.testFlag(QFileDevice::ReadGroup));
        QVERIFY(mode.testFlag(QFileDevice::ReadOther));
        QVERIFY(!mode.testFlag(QFileDevice::WriteGroup));
        QVERIFY(!mode.testFlag(QFileDevice::WriteOther));
        QVERIFY(!mode.testFlag(QFileDevice::ExeOwner));

        QCOMPARE(readBlocked(path, &error, &missing),
                 QStringList({QStringLiteral("julia"), QStringLiteral("theo")}));
        QVERIFY(!missing);

        // The whole set every time, never a change to it. An empty set is an
        // empty file and not a file removed: the mode and the ownership are
        // already right for the next time somebody has to go in it.
        QVERIFY(writeBlocked(path, {}, &error));
        QVERIFY(QFile::exists(path));
        QCOMPARE(readBlocked(path, &error, &missing), QStringList());
    }

    // Blank lines and stray whitespace are not accounts. A name with a space
    // around it read as a different name is somebody refused for a login they
    // never attempted.
    void blanksAreNotAccounts()
    {
        const QString path = m_tree.filePath(QStringLiteral("etc/ragged"));
        QFile file(path);
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("\njulia \n\n  theo\n\n");
        file.close();

        QString error;
        QCOMPARE(readBlocked(path, &error, nullptr),
                 QStringList({QStringLiteral("julia"), QStringLiteral("theo")}));
    }

    // -- the command ---------------------------------------------------------

    // The one poc/findings.md round 3 measured, and the same door
    // `$OMAHOUSE_USERADD` is: a suite proves the command is built right without
    // anybody's session ending.
    void theCommandIsTheOneThePocMeasured()
    {
        QCOMPARE(terminateUserCommand(QStringLiteral("julia")),
                 QStringList({QStringLiteral("loginctl"), QStringLiteral("terminate-user"),
                              QStringLiteral("julia")}));

        qputenv("OMAHOUSE_LOGINCTL", "/tmp/omahouse-ended");
        QCOMPARE(loginctlProgram(), QStringLiteral("/tmp/omahouse-ended"));
        QCOMPARE(terminateUserCommand(QStringLiteral("julia")).first(),
                 QStringLiteral("/tmp/omahouse-ended"));
        qunsetenv("OMAHOUSE_LOGINCTL");
        QCOMPARE(loginctlProgram(), QStringLiteral("loginctl"));
    }

    // The file the PAM line of spec.md §2 names, spelt in one place.
    void theBlockedFileIsBesideTheProfiles()
    {
        qputenv("OMAHOUSE_CONFIG_DIR", QFile::encodeName(m_tree.path()));
        QCOMPARE(paths::blockedFile(), m_tree.path() + QStringLiteral("/blocked"));
        qunsetenv("OMAHOUSE_CONFIG_DIR");
        QCOMPARE(paths::blockedFile(), QStringLiteral("/etc/omahouse/blocked"));
        QCOMPARE(paths::systemConfigDir(), QStringLiteral("/etc/omahouse"));
    }
};

int runEnforceTests(int argc, char **argv)
{
    EnforceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_enforce.moc"
