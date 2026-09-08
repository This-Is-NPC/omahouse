#include <QCoreApplication>

// One binary, two archives, fifteen suites and the smoke test.
//
// QTEST_MAIN writes a main() of its own, so it can only appear once; each suite
// hands out a function instead and this runs them in turn. The alternative --
// a .pro and a binary per suite -- links the same two archives sixteen times
// to assert fifteen groups of things about them.
int runSmokeTests(int argc, char **argv);
int runScopeNameTests(int argc, char **argv);
int runPolicyTests(int argc, char **argv);
int runDurationTests(int argc, char **argv);
int runFocusTests(int argc, char **argv);
int runLedgerTests(int argc, char **argv);
int runProcTests(int argc, char **argv);
int runPathsTests(int argc, char **argv);
int runPresenceTests(int argc, char **argv);
int runWatchTests(int argc, char **argv);
int runEnforceTests(int argc, char **argv);
int runWebPolicyTests(int argc, char **argv);
int runFleetTests(int argc, char **argv);
int runPairingTests(int argc, char **argv);
int runKindTests(int argc, char **argv);
int runPolkitTests(int argc, char **argv);

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    // Every suite runs even when an earlier one fails: a run that stops at the
    // first failure hides how much else broke with it.
    int failures = 0;
    failures += runSmokeTests(argc, argv);
    failures += runScopeNameTests(argc, argv);
    failures += runPolicyTests(argc, argv);
    failures += runDurationTests(argc, argv);
    failures += runFocusTests(argc, argv);
    failures += runLedgerTests(argc, argv);
    failures += runProcTests(argc, argv);
    failures += runPathsTests(argc, argv);
    failures += runPresenceTests(argc, argv);
    failures += runWatchTests(argc, argv);
    failures += runEnforceTests(argc, argv);
    failures += runWebPolicyTests(argc, argv);
    failures += runFleetTests(argc, argv);
    failures += runPairingTests(argc, argv);
    failures += runKindTests(argc, argv);
    failures += runPolkitTests(argc, argv);
    return failures == 0 ? 0 : 1;
}
