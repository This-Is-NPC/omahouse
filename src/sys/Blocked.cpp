#include "Blocked.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace omahouse {

QStringList readBlocked(const QString &path, QString *error, bool *missing)
{
    if (missing)
        *missing = false;
    if (error)
        error->clear();

    QFile file(path);
    if (!file.exists()) {
        if (missing)
            *missing = true;
        return {};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return {};
    }

    QStringList users;
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (const QByteArray &line : lines) {
        const QString name = QString::fromLocal8Bit(line.trimmed());
        if (!name.isEmpty())
            users.append(name);
    }
    return users;
}

bool writeBlocked(const QString &path, const QStringList &users, QString *error)
{
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(directory);
        return false;
    }

    // A sibling of the target, because rename is only atomic inside one
    // filesystem -- the same reason writeJsonAtomically gives, and the same
    // routine, spelled again here because this file is lines and not JSON and
    // the core owns the JSON one.
    QTemporaryFile temporary(directory + QStringLiteral("/.omahouse-blocked-XXXXXX"));
    if (!temporary.open()) {
        if (error) {
            *error = QStringLiteral("cannot write a temporary file in %1: %2")
                         .arg(directory, temporary.errorString());
        }
        return false;
    }

    QByteArray bytes;
    for (const QString &user : users) {
        if (user.trimmed().isEmpty())
            continue;
        bytes += user.trimmed().toLocal8Bit();
        bytes += '\n';
    }
    if (temporary.write(bytes) != bytes.size() || !temporary.flush()) {
        if (error)
            *error = QStringLiteral("cannot write %1: %2").arg(path, temporary.errorString());
        return false;
    }
    if (::fsync(temporary.handle()) != 0) {
        if (error) {
            *error = QStringLiteral("cannot sync %1: %2")
                         .arg(path, QString::fromLocal8Bit(::strerror(errno)));
        }
        return false;
    }
    // 0644: see the header. PAM opens this file in the process that is
    // authenticating, and a mode only root can read is a block that never blocks.
    if (!temporary.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ReadGroup | QFileDevice::ReadOther)) {
        if (error)
            *error = QStringLiteral("cannot set the mode of %1").arg(path);
        return false;
    }

    const QByteArray temporaryPath = QFile::encodeName(temporary.fileName());
    temporary.close();
    if (::rename(temporaryPath.constData(), QFile::encodeName(path).constData()) != 0) {
        if (error) {
            *error = QStringLiteral("cannot rename over %1: %2")
                         .arg(path, QString::fromLocal8Bit(::strerror(errno)));
        }
        return false;
    }
    temporary.setAutoRemove(false);

    const int directoryHandle = ::open(QFile::encodeName(directory).constData(),
                                       O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directoryHandle >= 0) {
        ::fsync(directoryHandle);
        ::close(directoryHandle);
    }
    return true;
}

} // namespace omahouse
