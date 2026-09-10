#include "Admin.h"

#include "Paths.h"
#include "Users.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace omahouse {
namespace {

/// The `omahouse` this studio drives.
///
/// The one beside it in the same directory first, so a build tree drives its own
/// CLI and not whatever is installed: a studio out of `build/bin` calling
/// `/usr/bin/omahouse` would be testing yesterday's package. Then the variable,
/// then the path.
QString findCli()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString sibling =
        QCoreApplication::applicationDirPath() + QStringLiteral("/omahouse");
    if (QFileInfo(sibling).isExecutable())
        return QFileInfo(sibling).absoluteFilePath();
    const QString named = env.value(QStringLiteral("OMAHOUSE_CLI"));
    if (!named.isEmpty())
        return named;
    const QString found = QStandardPaths::findExecutable(QStringLiteral("omahouse"));
    return found.isEmpty() ? QStringLiteral("/usr/bin/omahouse") : found;
}

/// Which root a verb writes into. `grant` puts minutes in the day's ledger under
/// /var/lib; everything else writes the profile file under /etc. One question
/// per root and not one for both: moving the ledger out of /var must not quietly
/// excuse a write to /etc.
bool verbWritesState(const QString &verb)
{
    return verb == QLatin1String("grant");
}

QString lastLine(const QByteArray &text)
{
    const QList<QByteArray> lines = text.split('\n');
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QByteArray line = lines.at(i).trimmed();
        if (!line.isEmpty())
            return QString::fromUtf8(line);
    }
    return QString();
}

} // namespace

Admin::Admin(QObject *parent)
    : QObject(parent)
    , m_program(findCli())
{
}

bool Admin::elevates() const
{
    // Already root is not a state this program is supposed to be in -- main()
    // refuses to start there -- but if it somehow were, asking polkit to give it
    // what it has would only add a dialog.
    if (runningAsRoot())
        return false;
    return paths::configDirIsTheSystems() || paths::stateDirIsTheSystems();
}

void Admin::run(const QString &title, const QStringList &arguments)
{
    if (arguments.isEmpty()) {
        complain(QStringLiteral("nothing to run"));
        return;
    }
    if (m_process) {
        complain(QStringLiteral("one at a time — %1 is still running").arg(m_title));
        return;
    }

    const QString verb = arguments.first();
    const bool needsRoot = !runningAsRoot()
        && (verbWritesState(verb) ? paths::stateDirIsTheSystems()
                                  : paths::configDirIsTheSystems());

    m_output.clear();
    m_title = title;
    m_failed = false;
    m_message = needsRoot ? QStringLiteral("%1 — waiting for polkit").arg(title)
                          : QStringLiteral("%1 …").arg(title);

    auto *process = new QProcess(this);
    process->setProgram(needsRoot ? QStringLiteral("pkexec") : m_program);
    process->setArguments(needsRoot ? QStringList{m_program} + arguments : arguments);
    m_process = process;
    emit changed();

    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError code) {
        if (code != QProcess::FailedToStart)
            return;
        m_process = nullptr;
        process->deleteLater();
        settle(QStringLiteral("%1: could not start %2")
                   .arg(m_title, process->program()),
               true);
    });
    connect(process, &QProcess::finished, this,
            [this, process](int code, QProcess::ExitStatus status) {
                const QByteArray err = process->readAllStandardError();
                const QByteArray out = process->readAllStandardOutput();
                m_output.clear();
                for (const auto &line : QString::fromUtf8(out).split(QLatin1Char('\n'))) {
                    if (!line.trimmed().isEmpty())
                        m_output.append(line);
                }
                m_process = nullptr;
                process->deleteLater();

                if (status != QProcess::NormalExit) {
                    settle(QStringLiteral("%1: the command was killed").arg(m_title), true);
                    return;
                }
                if (code == 0) {
                    const QString said = lastLine(out);
                    settle(said.isEmpty() ? QStringLiteral("%1 — done").arg(m_title) : said, false);
                    return;
                }
                // pkexec answers a dismissed dialog and a refused rule with a
                // code and an empty stderr. A window that showed nothing there
                // would be a window where the button silently does not work.
                if (code == 126) {
                    settle(QStringLiteral("%1: cancelled at the password prompt").arg(m_title),
                           true);
                    return;
                }
                if (code == 127 && err.isEmpty()) {
                    settle(QStringLiteral("%1: polkit would not authorise it").arg(m_title), true);
                    return;
                }
                const QString said = lastLine(err.isEmpty() ? out : err);
                settle(said.isEmpty()
                           ? QStringLiteral("%1: failed with %2").arg(m_title).arg(code)
                           : said,
                       true);
            });
    process->start();
}

void Admin::settle(const QString &text, bool failed)
{
    m_message = text;
    m_failed = failed;
    emit changed();
    emit done(!failed);
}

void Admin::say(const QString &text)
{
    m_message = text;
    m_failed = false;
    emit changed();
}

void Admin::complain(const QString &text)
{
    m_message = text;
    m_failed = true;
    emit changed();
}

void Admin::clear()
{
    if (m_message.isEmpty() && m_output.isEmpty())
        return;
    m_output.clear();
    m_message.clear();
    m_failed = false;
    emit changed();
}

} // namespace omahouse
