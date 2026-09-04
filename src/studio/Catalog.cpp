#include "Catalog.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcessEnvironment>
#include <QTextStream>

#include <algorithm>

namespace omahouse {
namespace {

QStringList dataDirs()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList roots;
    QString home = env.value(QStringLiteral("XDG_DATA_HOME"));
    if (home.isEmpty())
        home = QDir::homePath() + QStringLiteral("/.local/share");
    roots << home;
    QString shared = env.value(QStringLiteral("XDG_DATA_DIRS"));
    if (shared.isEmpty())
        shared = QStringLiteral("/usr/local/share:/usr/share");
    const QStringList parts = shared.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString &part : parts)
        roots << part;
    // The one door a test opens, and the reason the walk is not hard wired to
    // /usr/share: asserting that the picker lists a program should not mean
    // installing one.
    const QString override = env.value(QStringLiteral("OMAHOUSE_DESKTOP_DIRS"));
    if (!override.isEmpty())
        roots = override.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    return roots;
}

/// The `[Desktop Entry]` group of one file, flattened. Later groups --
/// `[Desktop Action new-window]` and its like -- carry a `Name` and an `Exec` of
/// their own, and reading past the first group would take the last action's name
/// for the program's.
QHash<QString, QString> readEntry(const QString &path)
{
    QHash<QString, QString> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return values;
    QTextStream stream(&file);
    bool inside = false;
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            if (inside)
                break;
            inside = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inside || line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const QString key = line.left(equals).trimmed();
        // `Name[pt_BR]` is a translation of `Name`, and taking whichever came
        // last would label the list in whatever languages the machine happens to
        // have installed. The plain key is the one every entry has.
        if (key.contains(QLatin1Char('[')))
            continue;
        values.insert(key, line.mid(equals + 1).trimmed());
    }
    return values;
}

/// The program an `Exec` line starts: the first word, without the field codes
/// (`%f`, `%U`) and without the quoting, and without `env VAR=x` in front of it.
QString programOf(const QString &exec)
{
    const QStringList words = exec.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &word : words) {
        QString candidate = word;
        if (candidate.startsWith(QLatin1Char('"')) || candidate.startsWith(QLatin1Char('\'')))
            candidate = candidate.mid(1);
        if (candidate.endsWith(QLatin1Char('"')) || candidate.endsWith(QLatin1Char('\'')))
            candidate.chop(1);
        if (candidate.startsWith(QLatin1Char('%')))
            continue;
        if (candidate == QLatin1String("env"))
            continue;
        if (candidate.contains(QLatin1Char('=')) && !candidate.contains(QLatin1Char('/')))
            continue;
        return candidate;
    }
    return QString();
}

} // namespace

QStringList applicationDirs()
{
    QStringList directories;
    const QStringList roots = dataDirs();
    for (const QString &root : roots)
        directories << root + QStringLiteral("/applications");
    return directories;
}

QVector<DesktopApp> installedApps()
{
    QHash<QString, DesktopApp> byId;
    QStringList order;
    const QStringList roots = applicationDirs();
    for (const QString &applications : roots) {
        if (!QFileInfo(applications).isDir())
            continue;
        QDirIterator walk(applications, QStringList{QStringLiteral("*.desktop")}, QDir::Files,
                          QDirIterator::Subdirectories);
        while (walk.hasNext()) {
            const QString path = walk.next();
            const QString id = QFileInfo(path).completeBaseName();
            // First directory wins, which is the XDG rule: what is in the home
            // directory overrides what the packages shipped.
            if (byId.contains(id))
                continue;
            const QHash<QString, QString> values = readEntry(path);
            if (values.value(QStringLiteral("Type")) != QLatin1String("Application"))
                continue;
            if (values.value(QStringLiteral("NoDisplay")) == QLatin1String("true"))
                continue;
            if (values.value(QStringLiteral("Hidden")) == QLatin1String("true"))
                continue;
            DesktopApp app;
            app.id = id;
            app.name = values.value(QStringLiteral("Name"), id);
            app.exec = programOf(values.value(QStringLiteral("Exec")));
            if (app.exec.isEmpty())
                app.exec = values.value(QStringLiteral("TryExec"));
            byId.insert(id, app);
            order << id;
        }
    }

    QVector<DesktopApp> apps;
    apps.reserve(order.size());
    for (const QString &id : std::as_const(order))
        apps.append(byId.value(id));
    std::sort(apps.begin(), apps.end(), [](const DesktopApp &a, const DesktopApp &b) {
        const int byName = a.name.compare(b.name, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.id < b.id;
    });
    return apps;
}

} // namespace omahouse
