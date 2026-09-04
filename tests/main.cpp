#include <QCoreApplication>

// One binary, one archive, three suites and the smoke test.
//
// QTEST_MAIN writes a main() of its own, so it can only appear once; each suite
// hands out a function instead and this runs them in turn. The alternative --
// a .pro and a binary per suite -- links libomahousecore.a four times to assert
// four groups of things about the same library.
int runSmokeTests(int argc, char **argv);
int runScopeNameTests(int argc, char **argv);
int runPolicyTests(int argc, char **argv);
int runLedgerTests(int argc, char **argv);

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    // Every suite runs even when an earlier one fails: a run that stops at the
    // first failure hides how much else broke with it.
    int failures = 0;
    failures += runSmokeTests(argc, argv);
    failures += runScopeNameTests(argc, argv);
    failures += runPolicyTests(argc, argv);
    failures += runLedgerTests(argc, argv);
    return failures == 0 ? 0 : 1;
}
