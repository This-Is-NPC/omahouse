#include <QtTest>

#include "AppScope.h"

using namespace omahouse;

// The identity of an app, which spec.md §5 says is the cgroup scope and not the
// path of the executable. Every unit below is one the PoC actually saw
// (poc/findings.md, rounds 1 to 3), because a parser tested against names
// somebody invented is a parser tested against a format somebody invented.
class ScopeNameTest : public QObject {
    Q_OBJECT

private slots:
    void namesTheAppsThePocMeasured_data()
    {
        QTest::addColumn<QString>("unit");
        QTest::addColumn<QString>("id");

        QTest::newRow("a launcher, an app, a launch id")
            << QStringLiteral("app-Hyprland-chromium-031bdc27.scope")
            << QStringLiteral("chromium");
        // No launcher at all, and a launch id that is all digits: the id is the
        // first field here and the second one in the row above, which is why the
        // trailing field comes off before the rest is split.
        QTest::newRow("no launcher") << QStringLiteral("app-code-3579042.scope")
                                     << QStringLiteral("code");
        // The flatpak app id, dots and all, sitting where the app name goes.
        // Round 3 measured four processes in this scope, three of them
        // /usr/bin/bwrap: the scope names the app and the executable does not.
        QTest::newRow("flatpak")
            << QStringLiteral("app-flatpak-org.freedesktop.Platform-2351381583.scope")
            << QStringLiteral("org.freedesktop.Platform");
        // The one that only turns up by measuring: systemd escapes the dashes of
        // the name, so splitting on '-' without unescaping first yields `xdg`.
        QTest::newRow("escaped dashes")
            << QStringLiteral("app-Hyprland-xdg\\x2dterminal\\x2dexec-151e8e07.scope")
            << QStringLiteral("xdg-terminal-exec");
        QTest::newRow("the sleep of round 2")
            << QStringLiteral("app-Hyprland-sleep-7865852f.scope") << QStringLiteral("sleep");
    }

    void namesTheAppsThePocMeasured()
    {
        QFETCH(QString, unit);
        QFETCH(QString, id);

        QString error = QStringLiteral("untouched");
        QCOMPARE(scopeIdFromUnit(unit, &error), id);
        QVERIFY2(error == QStringLiteral("untouched"),
                 qPrintable(QStringLiteral("a unit it parsed also reported: %1").arg(error)));
    }

    // A refusal and not a guess. An id that came out wrong is worse than no id:
    // it matches some other budget, or falls through to the default verdict
    // under a name nobody wrote, and it does both silently.
    void refusesAUnitThatIsNotAnAppScope_data()
    {
        QTest::addColumn<QString>("unit");

        QTest::newRow("empty") << QString();
        QTest::newRow("session plumbing, not an app")
            << QStringLiteral("wayland-wm@hyprland.desktop.service");
        // The service of round 2's blind spot: same name shape, wrong unit type.
        QTest::newRow("a service, not a scope")
            << QStringLiteral("app-Hyprland-chromium-031bdc27.service");
        QTest::newRow("a slice, not a scope") << QStringLiteral("app-graphical.slice");
        QTest::newRow("no app- prefix") << QStringLiteral("Hyprland-chromium-031bdc27.scope");
        QTest::newRow("nothing between the prefix and the suffix")
            << QStringLiteral("app-.scope");
        QTest::newRow("no launch id") << QStringLiteral("app-chromium.scope");
        QTest::newRow("a launch id that is not hex")
            << QStringLiteral("app-Hyprland-chromium-not.a.launch.scope");
        QTest::newRow("an empty app name") << QStringLiteral("app--031bdc27.scope");
        QTest::newRow("more names than a launcher and an app")
            << QStringLiteral("app-Hyprland-xdg-terminal-exec-151e8e07.scope");
        QTest::newRow("a truncated escape")
            << QStringLiteral("app-Hyprland-xdg\\x2-151e8e07.scope");
        QTest::newRow("an escape that is not hex")
            << QStringLiteral("app-Hyprland-xdg\\xzz-151e8e07.scope");
        QTest::newRow("an escaped newline")
            << QStringLiteral("app-Hyprland-xdg\\x0aterminal-151e8e07.scope");
    }

    void refusesAUnitThatIsNotAnAppScope()
    {
        QFETCH(QString, unit);

        QString error;
        QVERIFY2(scopeIdFromUnit(unit, &error).isEmpty(),
                 qPrintable(QStringLiteral("%1 was given an id").arg(unit)));
        QVERIFY2(!error.isEmpty(), "it refused the unit without saying why");
    }

    // The one shape that is not about the name, and is not allowed to be about
    // it: a scope whose last process has gone is not an app somebody is running,
    // and `evaluate` leans on this to keep from billing a scope on its way out.
    //
    // A scope with no id is alive all the same. Requiring the name here was an
    // accounting bug: it took 46 `tmux-spawn-<uuid>.scope` holding more than a
    // hundred processes out of `evaluate` altogether, and an afternoon inside
    // them debited the session nothing. Whether anything can be named is a
    // question for `selectorMatches`, not for whether somebody is at the
    // keyboard.
    void countsAScopeAsLiveWheneverItHasProcesses()
    {
        AppScope running;
        running.id = QStringLiteral("chromium");
        running.unit = QStringLiteral("app-Hyprland-chromium-031bdc27.scope");
        running.pidCount = 21;
        QVERIFY(running.isLive());

        AppScope emptied = running;
        emptied.pidCount = 0;
        QVERIFY(!emptied.isLive());

        AppScope unnamed = running;
        unnamed.unit = QStringLiteral("tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope");
        unnamed.id.clear();
        QVERIFY2(unnamed.isLive(), "a scope full of processes was ignored for having no name");

        AppScope unnamedAndEmpty = unnamed;
        unnamedAndEmpty.pidCount = 0;
        QVERIFY(!unnamedAndEmpty.isLive());
    }

    // The second signal, and the only thing that catches a launcher shim:
    // whether what is running inside the scope backs up the name on it.
    //
    // Every disagreeing row was measured on this machine in round 4 of
    // poc/findings.md, and every agreeing one is a scope this machine had open
    // while the rest of stage 4 was written. Not a list of shims: there is no
    // list anywhere in the tree, because a list of names to keep current is what
    // the PoC killed with baseline.json in round 1.
    void tellsAnIdThatNamesItsAppFromOneThatNamesALauncher_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("exe");
        QTest::addColumn<bool>("agrees");

        QTest::newRow("the plain case")
            << QStringLiteral("chromium") << QStringLiteral("/usr/lib/chromium/chromium") << true;
        QTest::newRow("a directory of its own")
            << QStringLiteral("code") << QStringLiteral("/usr/share/code/code") << true;
        // The id spells the app out in reverse domain form and the file is the
        // last word of it. Neither contains the other whole, and they are the
        // same program.
        QTest::newRow("a chromium web app")
            << QStringLiteral("org.chromium.Chromium")
            << QStringLiteral("/usr/lib/chromium/chromium") << true;
        QTest::newRow("dashes against underscores")
            << QStringLiteral("gnome-calculator") << QStringLiteral("/usr/bin/gnome_calculator")
            << true;

        // Round 4: seven scopes of this name on this machine, and VS Code inside
        // every one of them. Allowing `gtk-launch` is allowing whatever it
        // launches next.
        QTest::newRow("the shim of round 4")
            << QStringLiteral("gtk-launch")
            << QStringLiteral("/usr/share/code/chrome_crashpad_handler") << false;
        QTest::newRow("the terminal, which is also a shim")
            << QStringLiteral("xdg-terminal-exec") << QStringLiteral("/usr/bin/alacritty")
            << false;
        // The opposite failure, and the reason this is not a verdict. The
        // executable of every flatpak on a machine is the same one, so here it is
        // the id that is right and this function that is wrong -- which is why
        // its answer is printed beside both halves rather than acted on.
        QTest::newRow("a flatpak, where the executable is the one that lies")
            << QStringLiteral("org.freedesktop.Platform") << QStringLiteral("/usr/bin/bwrap")
            << false;
        // Two letters found inside a reverse domain name is a coincidence, and a
        // coincidence that says `these agree` is the warning not firing on the
        // scope it was written for.
        QTest::newRow("a short name is not a match")
            << QStringLiteral("org.gnome.Shell") << QStringLiteral("/usr/bin/sh") << false;

        // No executable is nobody having looked, or a process whose executable
        // could not be read. Having no opinion is the honest answer, and it must
        // not read as a disagreement.
        QTest::newRow("nothing read") << QStringLiteral("gtk-launch") << QString() << true;
        QTest::newRow("no id either") << QString() << QStringLiteral("/usr/bin/bwrap") << true;
    }

    void tellsAnIdThatNamesItsAppFromOneThatNamesALauncher()
    {
        QFETCH(QString, id);
        QFETCH(QString, exe);
        QFETCH(bool, agrees);
        QCOMPARE(exeCorroboratesId(id, exe), agrees);
    }
};

int runScopeNameTests(int argc, char **argv)
{
    ScopeNameTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_scopename.moc"
