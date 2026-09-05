#include "FocusFile.h"

#include "Focus.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace omahouse {

namespace {

const char *const kSystemRuntimeRoot = "/run/user";

QString fromEnvironmentOr(const char *variable, const char *fallback)
{
    const QByteArray value = qgetenv(variable);
    if (!value.isEmpty())
        return QString::fromLocal8Bit(value);
    return QString::fromLatin1(fallback);
}

/// One component, opened without following a link.
int openInside(int base, const char *name, int flags)
{
    return ::openat(base, name, flags | O_NOFOLLOW | O_CLOEXEC);
}

} // namespace

FocusSource::~FocusSource() = default;

QString runtimeRoot()
{
    return fromEnvironmentOr("OMAHOUSE_RUNTIME_ROOT", kSystemRuntimeRoot);
}

QString systemRuntimeRoot()
{
    return QString::fromLatin1(kSystemRuntimeRoot);
}

bool runtimeRootIsTheSystems()
{
    return runtimeRoot() == QString::fromLatin1(kSystemRuntimeRoot);
}

QString focusDirFor(const QString &root, uid_t uid)
{
    return root + QLatin1Char('/') + QString::number(static_cast<qulonglong>(uid))
        + QStringLiteral("/omahouse");
}

QString focusFileFor(const QString &root, uid_t uid)
{
    return focusDirFor(root, uid) + QStringLiteral("/focus");
}

FileFocusSource::FileFocusSource(const QString &root)
    : m_root(root)
{
}

QByteArray FileFocusSource::tail(uid_t uid)
{
    // The runtime directory itself, which is systemd's and not the child's.
    // Opened with O_NOFOLLOW like everything else here, because a check that is
    // only applied where it is thought to be needed is a check somebody will one
    // day move.
    const QByteArray userDir =
        QFile::encodeName(m_root + QLatin1Char('/')
                          + QString::number(static_cast<qulonglong>(uid)));
    const int base = ::open(userDir.constData(),
                            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (base < 0)
        return {};

    // And the two components below it, which are hers. A symlink here is a child
    // asking root to read something else, and it does not get to be an error
    // worth a line -- it gets to be nothing at all, like every other way this
    // can come back empty.
    const int inside = openInside(base, "omahouse", O_RDONLY | O_DIRECTORY);
    ::close(base);
    if (inside < 0)
        return {};

    // O_NONBLOCK: a fifo in place of the file would otherwise hold the whole
    // cycle open waiting for a writer that is never coming, and everybody else's
    // day would stop being counted.
    const int file = openInside(inside, "focus", O_RDONLY | O_NONBLOCK);
    ::close(inside);
    if (file < 0)
        return {};

    struct stat info {};
    if (::fstat(file, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != uid) {
        // A fifo, a device, a directory, or a regular file that belongs to
        // somebody else -- which on a shared tmpfs is the one that matters, since
        // a file placed there by another account is another account deciding what
        // this one's report says.
        ::close(file);
        return {};
    }

    const off_t size = info.st_size;
    const off_t from = size > kFocusTailBytes ? size - kFocusTailBytes : 0;
    QByteArray blob(static_cast<int>(size - from), Qt::Uninitialized);
    if (blob.isEmpty()) {
        ::close(file);
        return {};
    }
    const ssize_t got = ::pread(file, blob.data(), static_cast<size_t>(blob.size()), from);
    ::close(file);
    if (got <= 0)
        return {};
    blob.truncate(static_cast<int>(got));
    return blob;
}

bool appendFocusLine(const QString &path, const QDateTime &at, const QString &site,
                     QString *error)
{
    const QString directory = QFileInfo(path).absolutePath();
    // 0700: the file names the sites somebody visited, so it is theirs and root's
    // and nobody else's on the machine. Root reads it because root can read
    // anything; another account on the same machine may not.
    if (!QDir().mkpath(directory)) {
        if (error)
            *error = QStringLiteral("cannot make %1").arg(directory);
        return false;
    }
    QFile::setPermissions(directory,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    const QByteArray encoded = QFile::encodeName(path);
    const int file = ::open(encoded.constData(),
                            O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (file < 0) {
        if (error)
            *error = QStringLiteral("cannot open %1: %2")
                         .arg(path, QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }

    // Bounded, and started again rather than rotated. Nothing reads more than the
    // last line of this file, so there is no history here to keep and no reason
    // to pay for keeping it -- and a file that grows without a ceiling in a tmpfs
    // is a session that eventually cannot write anything at all.
    struct stat info {};
    if (::fstat(file, &info) == 0 && info.st_size > kFocusMostBytes) {
        if (::ftruncate(file, 0) != 0) {
            ::close(file);
            if (error)
                *error = QStringLiteral("cannot start %1 again").arg(path);
            return false;
        }
    }

    // One `write` of one short line to an O_APPEND file. The reader takes only
    // complete lines, so a partial write is a line the next tick does not see
    // rather than a line the next tick misreads.
    const QByteArray line = focusLineFor(at, site);
    const ssize_t written = ::write(file, line.constData(), static_cast<size_t>(line.size()));
    ::close(file);
    if (written != line.size()) {
        if (error)
            *error = QStringLiteral("could not write the whole line to %1").arg(path);
        return false;
    }
    return true;
}

} // namespace omahouse
