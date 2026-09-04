#include <QtTest>

#include "Version.h"

using namespace omahouse;

// The harness itself, and the one thing that is not about the model: that the
// suite is linked against the core archive the CLI links, and that the version
// in it came from qmake/version.pri.
class SmokeTest : public QObject {
    Q_OBJECT

private slots:
    void reportsTheVersionFromTheBuild();
};

void SmokeTest::reportsTheVersionFromTheBuild()
{
    const QString version = omahouseVersion();
    QVERIFY(!version.isEmpty());
    QCOMPARE(version, QStringLiteral(OMAHOUSE_VERSION));
}

int runSmokeTests(int argc, char **argv)
{
    SmokeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_smoke.moc"
