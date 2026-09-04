#include "Json.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QTemporaryFile>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace omahouse {

bool readJsonObject(const QString &path, QJsonObject *out, QString *error, bool *missing)
{
    if (missing)
        *missing = false;
    if (out)
        *out = QJsonObject();

    QFile file(path);
    if (!file.exists()) {
        if (missing)
            *missing = true;
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("malformed json in %1 at offset %2: %3")
                         .arg(path)
                         .arg(parseError.offset)
                         .arg(parseError.errorString());
        }
        return false;
    }
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("%1 is not a json object").arg(path);
        return false;
    }
    if (out)
        *out = doc.object();
    return true;
}

bool writeJsonAtomically(const QString &path, const QJsonObject &object, QString *error)
{
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(directory);
        return false;
    }

    // The temporary file is a sibling of the target: `rename` is only atomic
    // within one filesystem, and /tmp is routinely a different one from
    // /var/lib.
    QTemporaryFile temporary(directory + QStringLiteral("/.omahouse-XXXXXX"));
    if (!temporary.open()) {
        if (error) {
            *error = QStringLiteral("cannot write a temporary file in %1: %2")
                         .arg(directory, temporary.errorString());
        }
        return false;
    }

    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (temporary.write(bytes) != bytes.size() || !temporary.flush()) {
        if (error)
            *error = QStringLiteral("cannot write %1: %2").arg(path, temporary.errorString());
        return false;
    }
    // Flushing moves the bytes out of Qt's buffer and into the kernel's; only
    // fsync puts them on the disk. Renaming over the old file before that is
    // how a crash leaves a file that exists, has the right name, and is empty.
    if (::fsync(temporary.handle()) != 0) {
        if (error)
            *error = QStringLiteral("cannot sync %1: %2")
                         .arg(path, QString::fromLocal8Bit(::strerror(errno)));
        return false;
    }
    // 0644, spec.md §4: root writes both files and everybody reads them, so the
    // fiscalised user can run `omahouse status` and see what is left. A
    // temporary file is created 0600, so this is a widening and has to be said
    // out loud rather than left to the umask of whoever started the daemon.
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
    // QFile::rename refuses an existing target, and removing it first is the
    // window this whole routine exists to close, so the rename is the C one.
    // The file is gone from under the temporary object by now; letting it try
    // to unlink the name would only race the next writer.
    temporary.setAutoRemove(false);

    // The rename itself is a change to the directory, and it is durable only
    // once the directory is synced too.
    const int directoryHandle = ::open(QFile::encodeName(directory).constData(),
                                       O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directoryHandle >= 0) {
        ::fsync(directoryHandle);
        ::close(directoryHandle);
    }
    return true;
}

bool checkSchemaVersion(const QJsonObject &root, const QString &what, QString *error)
{
    const QJsonValue value = root.value(QStringLiteral("schemaVersion"));
    if (!value.isDouble()) {
        if (error)
            *error = QStringLiteral("%1 has no numeric schemaVersion").arg(what);
        return false;
    }
    if (value.toInt() != kSchemaVersion) {
        if (error) {
            *error = QStringLiteral("%1 is schema version %2, and this omahouse reads %3")
                         .arg(what)
                         .arg(value.toInt())
                         .arg(kSchemaVersion);
        }
        return false;
    }
    return true;
}

} // namespace omahouse
