#include <QtTest>

#include "Version.h"

using namespace omahouse;

// The harness itself, and the one thing there is to assert before the model
// exists: that the suite is linked against the core archive the CLI links, and
// that the version in it came from qmake/version.pri.
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

QTEST_MAIN(SmokeTest)
#include "tst_smoke.moc"
