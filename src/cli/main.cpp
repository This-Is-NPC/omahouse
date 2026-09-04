#include "Version.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTextStream>

#ifndef OMAHOUSE_VERSION
#error "OMAHOUSE_VERSION is not defined -- qmake/version.pri was not included."
#endif

using namespace omahouse;

namespace {

constexpr int kOk = 0;
constexpr int kUsage = 1;

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

// The verbs are not here yet: stage 4 of plan.md brings `status`, `report` and
// `profile`, stage 5 the ones that write, stage 6 `watch`. This screen says what
// the binary does today rather than promising a table it cannot honour.
void printHelp()
{
    out() << "omahouse " << omahouseVersion() << "\n"
          << "House rules for the accounts on an Omarchy machine.\n"
          << "\n"
          << "Usage: omahouse [--version] [--help]\n"
          << "\n"
          << "  --version, -V    print the version and exit\n"
          << "  --help, -h       print this screen and exit\n"
          << "\n"
          << "No verb exists yet. See plan.md for the order they arrive in.\n";
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("omahouse"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OMAHOUSE_VERSION));

    const QStringList args = QCoreApplication::arguments().mid(1);

    // Asking for help is not a usage error, and neither is asking for the
    // version. Anything else is: a word this binary does not know is a typo or a
    // verb from a stage that has not been written, and a script has to be able
    // to tell that from success.
    for (const QString &arg : args) {
        if (arg == QLatin1String("--version") || arg == QLatin1String("-V")) {
            out() << "omahouse " << omahouseVersion() << "\n";
            out().flush();
            return kOk;
        }
        if (arg == QLatin1String("--help") || arg == QLatin1String("-h")) {
            printHelp();
            out().flush();
            return kOk;
        }
    }

    if (args.isEmpty()) {
        printHelp();
        out().flush();
        return kOk;
    }

    err() << "omahouse: unknown argument '" << args.first() << "'\n"
          << "Try 'omahouse --help'.\n";
    err().flush();
    return kUsage;
}
