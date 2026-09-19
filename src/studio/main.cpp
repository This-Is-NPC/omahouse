#include "Users.h"
#include "Version.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTextStream>
#include <QUrl>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("omahouse-studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OMAHOUSE_VERSION));
    QGuiApplication::setQuitOnLastWindowClosed(true);

    // The studio is never root, and this is where that is enforced rather than
    // merely intended.
    //
    // Everything it reads is 0644 or world readable, so there is nothing here
    // that a privilege would unlock; everything it writes goes out through
    // `pkexec omahouse`, which is one small program with argument checking in
    // it. A `sudo omahouse-studio` would put a QML engine, a filesystem watcher
    // and a theme reader inside the privilege instead, and would do it for a
    // window that would then work -- so nobody would find out. Refusing costs a
    // retyped command.
    if (omahouse::runningAsRoot()) {
        QTextStream err(stderr);
        err << QStringLiteral("omahouse-studio: this window is never root. Run it as yourself; "
                              "it asks polkit when it needs to write.")
            << Qt::endl;
        return 2;
    }

    if (QCoreApplication::arguments().size() > 1) {
        QTextStream err(stderr);
        err << QStringLiteral("omahouse-studio: takes no arguments (the verbs are omahouse's)")
            << Qt::endl;
        return 2;
    }

    // Nothing is registered here. `Theme`, `House` and `Admin` carry
    // QML_ELEMENT and QML_SINGLETON, so the build registers them and writes the
    // type description `qmllint` reads -- rather than this file and the test
    // harness keeping two identical blocks of registrations in step by memory,
    // and rather than context properties, which the linter cannot see at all.
    QQmlApplicationEngine engine;
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, &app,
                     [] { QCoreApplication::exit(0); });
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [](QObject *object, const QUrl &) {
            if (!object)
                QCoreApplication::exit(1);
        },
        Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
