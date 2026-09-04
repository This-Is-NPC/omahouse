#include <QtTest>

#include "Paths.h"

using namespace omahouse;

// Where the two files of spec.md §4 are looked for, and the two doors that let a
// suite look for them somewhere it may write.
//
// The defaults are asserted as literals on purpose. `/etc/omahouse/profiles.json`
// is a path the packaging, the polkit policy and the systemd unit all name, so a
// change to it that nothing objects to is a change that ships broken.
class PathsTest : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        qunsetenv("OMAHOUSE_CONFIG_DIR");
        qunsetenv("OMAHOUSE_STATE_DIR");
    }

    void cleanupTestCase() { init(); }

    void looksInEtcAndVarByDefault()
    {
        QCOMPARE(paths::configDir(), QStringLiteral("/etc/omahouse"));
        QCOMPARE(paths::profilesFile(), QStringLiteral("/etc/omahouse/profiles.json"));
        QCOMPARE(paths::stateDir(), QStringLiteral("/var/lib/omahouse"));
        QCOMPARE(paths::userStateDir(QStringLiteral("julia")),
                 QStringLiteral("/var/lib/omahouse/julia"));
        QCOMPARE(paths::ledgerFile(QStringLiteral("julia"), QDate(2026, 9, 3)),
                 QStringLiteral("/var/lib/omahouse/julia/2026-09-03.json"));
    }

    // Both roots move on their own, because a run that reads a real
    // /etc/omahouse and writes its ledgers to a temporary directory is a run
    // somebody will want.
    void takesEitherRootFromTheEnvironment()
    {
        qputenv("OMAHOUSE_CONFIG_DIR", "/tmp/omahouse-config");
        QCOMPARE(paths::profilesFile(), QStringLiteral("/tmp/omahouse-config/profiles.json"));
        QCOMPARE(paths::stateDir(), QStringLiteral("/var/lib/omahouse"));

        qputenv("OMAHOUSE_STATE_DIR", "/tmp/omahouse-state");
        QCOMPARE(paths::ledgerFile(QStringLiteral("julia"), QDate(2026, 12, 31)),
                 QStringLiteral("/tmp/omahouse-state/julia/2026-12-31.json"));
    }

    // Zero padded, and the same string the writer of that day will build. A
    // ledger written to `2026-9-3.json` and looked for at `2026-09-03.json` is a
    // day that silently starts over.
    void spellsTheDateTheWaySpecSectionFourDoes()
    {
        qputenv("OMAHOUSE_STATE_DIR", "/state");
        QCOMPARE(paths::ledgerFile(QStringLiteral("j"), QDate(2026, 1, 2)),
                 QStringLiteral("/state/j/2026-01-02.json"));
    }
};

int runPathsTests(int argc, char **argv)
{
    PathsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_paths.moc"
