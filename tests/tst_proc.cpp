#include <QtTest>

#include "AppScope.h"
#include "Proc.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace omahouse;

namespace {

constexpr uid_t kUid = 1000;

/// A cgroup, as far as this walk is concerned: a directory with a `cgroup.procs`
/// in it holding one pid per line.
///
/// The pids are made up and never read; only the count is. Writing real ones
/// would be writing numbers that mean something on the machine the suite happens
/// to run on, which is the opposite of what a temporary tree is for.
void makeCgroup(const QString &path, int pidCount)
{
    QVERIFY2(QDir().mkpath(path), qPrintable(path));
    QFile file(path + QStringLiteral("/cgroup.procs"));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Text), qPrintable(path));
    for (int i = 0; i < pidCount; ++i)
        file.write(QByteArray::number(4000 + i) + '\n');
    file.close();
}

QString appSlice(const QString &root)
{
    return root + QStringLiteral("/user.slice/user-1000.slice/user@1000.service/app.slice");
}

QString sessionSlice(const QString &root)
{
    return root + QStringLiteral("/user.slice/user-1000.slice/user@1000.service/session.slice");
}

const AppScope *find(const QVector<AppScope> &scopes, const QString &unit)
{
    for (const AppScope &scope : scopes) {
        if (scope.unit == unit)
            return &scope;
    }
    return nullptr;
}

} // namespace

// The adapter that reads the machine, read against a machine that is a
// directory in $TMPDIR.
//
// Every unit name below was measured -- poc/findings.md for the flatpak scope
// and the escaped one, and the development machine for the rest -- because a
// walk tested against names somebody invented is a walk tested against a layout
// somebody invented. The point of the injectable root is exactly this: the same
// code path that will run under a root daemon on a graphical session is exercised
// here by an unprivileged suite with no session at all.
class ProcTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tree;

private slots:
    void initTestCase()
    {
        QVERIFY(m_tree.isValid());
        const QString apps = appSlice(m_tree.path());

        // Directly under app.slice: a `.desktop` launch, no launcher field.
        makeCgroup(apps + QStringLiteral("/app-code-3579042.scope"), 13);
        // The shape this machine gives a Chromium web app: the app id is the
        // name, dots and capitals and all.
        makeCgroup(apps + QStringLiteral("/app-org.chromium.Chromium-3735302.scope"), 4);
        // A user service under app.slice. Plumbing on the app side of the tree,
        // and not something anybody opened.
        makeCgroup(apps + QStringLiteral("/dconf.service"), 1);
        // A dbus activation gets a slice of its own, with the service inside it.
        makeCgroup(apps + QStringLiteral("/app-dbus\\x2d:1.1\\x2dorg.freedesktop.FileManager1.slice"
                                         "/dbus-:1.1-org.freedesktop.FileManager1@0.service"),
                   1);

        // Under app-graphical.slice: what uwsm launched, which is everything
        // Omarchy opens.
        const QString graphical = apps + QStringLiteral("/app-graphical.slice");
        makeCgroup(graphical + QStringLiteral("/app-Hyprland-chromium-031bdc27.scope"), 21);
        makeCgroup(graphical
                       + QStringLiteral("/app-Hyprland-xdg\\x2dterminal\\x2dexec-151e8e07.scope"),
                   4);
        makeCgroup(graphical
                       + QStringLiteral("/app-flatpak-org.freedesktop.Platform-2351381583.scope"),
                   4);
        // A scope on its way out: the unit is still there, nothing is in it.
        makeCgroup(graphical + QStringLiteral("/app-Hyprland-sleep-7865852f.scope"), 0);
        // A scope whose name is not an app scope name at all. This machine has
        // thirty of them, one per tmux pane.
        makeCgroup(graphical
                       + QStringLiteral("/tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope"),
                   18);
        // A delegated scope with a child cgroup. The child's processes are the
        // app's too.
        const QString delegated = graphical + QStringLiteral("/app-Hyprland-code-9f2c11ab.scope");
        makeCgroup(delegated, 2);
        makeCgroup(delegated + QStringLiteral("/worker"), 3);

        // session.slice, spec.md §5's blind spot.
        const QString session = sessionSlice(m_tree.path());
        makeCgroup(session + QStringLiteral("/wayland-wm@hyprland.desktop.service"), 8);
        makeCgroup(session + QStringLiteral("/pipewire.service"), 1);
        makeCgroup(session + QStringLiteral("/wireplumber.service"), 1);
        makeCgroup(session + QStringLiteral("/dbus-broker.service"), 2);
        makeCgroup(session + QStringLiteral("/xdg-desktop-portal.service"), 1);
        makeCgroup(session + QStringLiteral("/gvfs-daemon.service"), 5);
        // A scope in session.slice: something registered at run time on the side
        // of the tree where only declared units belong.
        makeCgroup(session + QStringLiteral("/stray-3c1f9a02.scope"), 2);
    }

    void findsEveryAppScopeWhereverItSits()
    {
        const Proc proc(m_tree.path());
        const QVector<AppScope> scopes = proc.scopesFor(kUid);

        QStringList units;
        for (const AppScope &scope : scopes)
            units.append(scope.unit);
        // Both depths, and nothing that is not a scope: no dconf.service, no
        // slice, no dbus activation.
        QCOMPARE(units.size(), 8);
        QVERIFY(units.contains(QStringLiteral("app-code-3579042.scope")));
        QVERIFY(units.contains(QStringLiteral("app-Hyprland-chromium-031bdc27.scope")));
        for (const QString &unit : units)
            QVERIFY2(unit.endsWith(QStringLiteral(".scope")), qPrintable(unit));
    }

    void resolvesTheIdWithTheParserOfTheCore()
    {
        const Proc proc(m_tree.path());
        const QVector<AppScope> scopes = proc.scopesFor(kUid);

        const AppScope *chromium =
            find(scopes, QStringLiteral("app-Hyprland-chromium-031bdc27.scope"));
        QVERIFY(chromium);
        QCOMPARE(chromium->id, QStringLiteral("chromium"));
        QCOMPARE(chromium->pidCount, 21);
        QVERIFY(chromium->isLive());

        // The one that only turns up by measuring: without unescaping, the id
        // would come out as `xdg`.
        const AppScope *terminal =
            find(scopes, QStringLiteral("app-Hyprland-xdg\\x2dterminal\\x2dexec-151e8e07.scope"));
        QVERIFY(terminal);
        QCOMPARE(terminal->id, QStringLiteral("xdg-terminal-exec"));
        QCOMPARE(terminal->pidCount, 4);

        // Round 3: the scope carries the flatpak app id, and the executable of
        // three of its four processes is /usr/bin/bwrap.
        const AppScope *flatpak =
            find(scopes, QStringLiteral("app-flatpak-org.freedesktop.Platform-2351381583.scope"));
        QVERIFY(flatpak);
        QCOMPARE(flatpak->id, QStringLiteral("org.freedesktop.Platform"));

        const AppScope *webapp =
            find(scopes, QStringLiteral("app-org.chromium.Chromium-3735302.scope"));
        QVERIFY(webapp);
        QCOMPARE(webapp->id, QStringLiteral("org.chromium.Chromium"));
    }

    void countsTheWholeTreeOfAScope()
    {
        const Proc proc(m_tree.path());
        const AppScope *delegated = find(proc.scopesFor(kUid),
                                         QStringLiteral("app-Hyprland-code-9f2c11ab.scope"));
        QVERIFY(delegated);
        // Two of its own and three in the child. A subtree that is not counted
        // is time somebody spent that nobody debited.
        QCOMPARE(delegated->pidCount, 5);
    }

    // Kept, not dropped. `Policy::evaluate` steps over it -- `isLive` is false
    // without an id -- and `status` reports it as something seen and not named,
    // which is what spec.md §5 asks of everything the model cannot account for.
    void keepsAScopeItCannotName()
    {
        const Proc proc(m_tree.path());
        const AppScope *tmux =
            find(proc.scopesFor(kUid),
                 QStringLiteral("tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope"));
        QVERIFY(tmux);
        QVERIFY(tmux->id.isEmpty());
        QCOMPARE(tmux->pidCount, 18);
        QVERIFY(!tmux->isLive());
    }

    // An empty scope is a unit systemd has not reaped yet, not an app running.
    void reportsAnEmptyScopeAsEmpty()
    {
        const Proc proc(m_tree.path());
        const AppScope *gone =
            find(proc.scopesFor(kUid), QStringLiteral("app-Hyprland-sleep-7865852f.scope"));
        QVERIFY(gone);
        QCOMPARE(gone->id, QStringLiteral("sleep"));
        QCOMPARE(gone->pidCount, 0);
        QVERIFY(!gone->isLive());
    }

    void ordersTheScopesTheSameWayTwice()
    {
        const Proc proc(m_tree.path());
        const QVector<AppScope> first = proc.scopesFor(kUid);
        const QVector<AppScope> second = proc.scopesFor(kUid);
        QCOMPARE(first.size(), second.size());
        for (int i = 0; i < first.size(); ++i)
            QCOMPARE(first.at(i).unit, second.at(i).unit);
    }

    // The blind spot, counted: the compositor's own unit, because the PoC
    // measured `hyprctl dispatch exec` landing inside it, and any scope, because
    // a scope on this side of the tree is something that was launched. Nothing
    // else: pipewire and the portals are units a package declared.
    void countsOnlyWhatCouldBeHidingAnApp()
    {
        const Proc proc(m_tree.path());
        const QVector<SessionUnit> units = proc.sessionSliceUnits(kUid);

        QStringList names;
        for (const SessionUnit &unit : units)
            names.append(unit.unit);
        QCOMPARE(names,
                 QStringList({QStringLiteral("stray-3c1f9a02.scope"),
                              QStringLiteral("wayland-wm@hyprland.desktop.service")}));
        QCOMPARE(proc.sessionSliceProcesses(kUid), 10);
    }

    // A user who is not logged in, which is most users most of the time. Nothing
    // to count, and nothing to complain about.
    void saysNothingAboutAUserWithNoSession()
    {
        const Proc proc(m_tree.path());
        QVERIFY(proc.hasSession(kUid));
        QVERIFY(!proc.hasSession(4242));
        QVERIFY(proc.scopesFor(4242).isEmpty());
        QCOMPARE(proc.sessionSliceProcesses(4242), 0);
    }

    void buildsThePathSpecSectionFiveNames()
    {
        const Proc proc(QStringLiteral("/sys/fs/cgroup"));
        QCOMPARE(proc.appSlicePath(1000),
                 QStringLiteral("/sys/fs/cgroup/user.slice/user-1000.slice/"
                                "user@1000.service/app.slice"));
        QCOMPARE(proc.sessionSlicePath(1000),
                 QStringLiteral("/sys/fs/cgroup/user.slice/user-1000.slice/"
                                "user@1000.service/session.slice"));
    }

    // The default the CLI and the daemon get when nobody says otherwise, and the
    // door the end to end suite comes in through.
    void takesTheRootFromTheEnvironmentWhenThereIsOne()
    {
        qunsetenv("OMAHOUSE_CGROUP_ROOT");
        QCOMPARE(Proc::defaultCgroupRoot(), QStringLiteral("/sys/fs/cgroup"));
        qputenv("OMAHOUSE_CGROUP_ROOT", m_tree.path().toLocal8Bit());
        QCOMPARE(Proc::defaultCgroupRoot(), m_tree.path());
        QCOMPARE(Proc().cgroupRoot(), m_tree.path());
        qunsetenv("OMAHOUSE_CGROUP_ROOT");
        QCOMPARE(Proc::defaultCgroupRoot(), QStringLiteral("/sys/fs/cgroup"));
    }
};

int runProcTests(int argc, char **argv)
{
    ProcTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_proc.moc"
