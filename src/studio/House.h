#pragma once

#include "Catalog.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>

namespace omahouse {

// Everything the window reads, and nothing it writes.
//
// The read path is direct: this links `libomahousecore.a` and
// `libomahousesys.a` and calls them, because `/etc/omahouse/profiles.json` and
// `/var/lib/omahouse/<user>/<date>.json` are 0644 and the cgroup tree is world
// readable, so nothing about looking needs a privilege. The write path is not
// here at all -- it is `Admin`, and it goes out through `pkexec omahouse`.
//
// One snapshot, rebuilt on a timer and handed to QML whole. Two reasons it is a
// property and not a set of invokable functions. A binding that calls a function
// does not re-run when the machine changes, so a balance drawn that way is the
// balance at the moment the window opened; and a snapshot that is compared
// before it is published means an idle machine emits nothing, so the list the
// keyboard is standing in is not torn down and rebuilt every two seconds under
// the cursor.
//
// The rows carry their own words. `left` is the string "1h20m" beside the
// integer `leftSeconds`, so the QML draws a meter from the number and prints the
// string, and there is one place -- this one -- that decides how long is spelled.
class House : public QObject
{
    Q_OBJECT
    // A QML singleton rather than a context property: a context property is
    // invisible to `qmllint`, so every use of it is an unqualified access the
    // linter can only warn about. A declared singleton is a type the tooling and
    // the engine agree on.
    QML_ELEMENT
    QML_SINGLETON

    /// Whoever opened the window, by real uid -- or the account `$OMAHOUSE_AS`
    /// names, which is how the documentation generator draws both faces from
    /// one login. It moves what is *read* and never what may be *written*: see
    /// `readingAs` in House.cpp.
    Q_PROPERTY(QString user READ user CONSTANT)
    /// `operator` or `subject`. Nobody chooses it: see `face()`.
    Q_PROPERTY(QString face READ face NOTIFY changed)
    /// Why that face, in words, for the header: `in wheel`, `under rules`,
    /// `not under rules`.
    Q_PROPERTY(QString faceReason READ faceReason NOTIFY changed)
    /// What could not be read, if anything. A profiles.json that is corrupt is a
    /// thing to say on the status line, not a thing to draw as an empty list.
    Q_PROPERTY(QString error READ error NOTIFY changed)
    /// The profiles this face is allowed to see. The operator sees every one;
    /// the subject sees theirs and no one else's, because profiles.json is
    /// readable by everybody and a window is not a reason to publish the rest of
    /// the household.
    Q_PROPERTY(QVariantList people READ people NOTIFY changed)
    /// `programs`, `sites`, `today` and `catalog`, each an object keyed by user
    /// name.
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
    /// What a browser policy reaches, in one sentence — `webPolicyReach` in
    /// `src/core/WebPolicy.h`, joined.
    ///
    /// A property and not a string in the QML because the CLI says the same
    /// thing out of the same function: the sites view prints it once, `omahouse
    /// web` prints it once when it writes, and neither composes its own.
    Q_PROPERTY(QString reach READ reach CONSTANT)

public:
    explicit House(QObject *parent = nullptr);

    QString user() const { return m_user; }
    QString face() const { return m_face; }
    QString faceReason() const { return m_faceReason; }
    QString error() const { return m_error; }
    QVariantList people() const { return m_snapshot.value(QStringLiteral("people")).toList(); }
    QVariantMap snapshot() const { return m_snapshot; }
    QString reach() const;

    /// Read the machine again now. The timer does this every two seconds; the
    /// window calls it the moment a write comes back, so the list does not sit
    /// stale for a second and a half after the operator's own action.
    Q_INVOKABLE void reload();

signals:
    void changed();

private:
    void refresh();

    QString m_user;
    QString m_face;
    QString m_faceReason;
    QString m_error;
    QVariantMap m_snapshot;
    /// The installed programs, read once and again when a desktop directory
    /// changes. Walking a few hundred `.desktop` files every two seconds to
    /// find out that nothing has been installed in the last two seconds is
    /// several hundred file reads a second for an answer that changes when a
    /// package does.
    QVector<DesktopApp> m_installed;
    bool m_installedStale = true;
    QTimer m_tick;
    QFileSystemWatcher m_watcher;
    QFileSystemWatcher m_desktops;
};

} // namespace omahouse
