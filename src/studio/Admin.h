#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace omahouse {

// The only way this window changes anything: `pkexec omahouse <verb> …`.
//
// The studio is never root. It is a Qt Quick program with a QML engine, a
// filesystem watcher and a theme reader in it, and running that as root to save
// a password prompt would be putting all of it inside the privilege. So the
// window reads directly and writes by asking the CLI, which is the same program
// an operator would have typed, with the same argument checking and the same
// refusals -- and polkit is what decides whether it may.
//
// One command at a time. Two `pkexec` prompts stacked over each other is a
// person authenticating something they can no longer see, and the second write
// would land on a profiles.json the first had already changed.
//
// A refusal is a sentence on the status bar and never a silence. `pkexec`
// answers a dismissed dialog and a denied rule with an exit code and nothing on
// stderr, so those two get their words here; everything else is the CLI's own
// message, which is the one an operator would have got in a terminal.
class Admin : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// The last outcome, in words, for the status bar. Empty until something
    /// has been asked for.
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool failed READ failed NOTIFY changed)
    /// Whether a write goes out through `pkexec`. False on a tree that was
    /// pointed somewhere else by `$OMAHOUSE_CONFIG_DIR`, which is not a
    /// loophole: `Paths::configDirIsTheSystems` exists to tell "writing needs
    /// root" from "writing needs whatever the filesystem says", and asking for
    /// a root password to write in somebody's own scratch directory is asking
    /// for a privilege nothing needs.
    Q_PROPERTY(bool elevates READ elevates CONSTANT)
    /// The CLI this drives, for the header and for the error messages.
    Q_PROPERTY(QString program READ program CONSTANT)

public:
    explicit Admin(QObject *parent = nullptr);

    bool busy() const { return m_process != nullptr; }
    QString message() const { return m_message; }
    bool failed() const { return m_failed; }
    bool elevates() const;
    QString program() const { return m_program; }

    /// Run one verb. `title` is what the window was trying to do, in the words
    /// it put on the button, so a failure can name the action rather than the
    /// argument list.
    Q_INVOKABLE void run(const QString &title, const QStringList &arguments);
    /// Say something that did not come from a process: "nothing is selected",
    /// "that is not a duration". Same line, same colour rules.
    Q_INVOKABLE void say(const QString &text);
    Q_INVOKABLE void complain(const QString &text);
    Q_INVOKABLE void clear();

signals:
    void changed();
    /// Emitted once a command has finished, so the window can read the machine
    /// again immediately instead of waiting for the next tick.
    void done(bool ok);

private:
    void settle(const QString &text, bool failed);

    QString m_program;
    QString m_message;
    QString m_title;
    bool m_failed = false;
    QProcess *m_process = nullptr;
};

} // namespace omahouse
