#include "Users.h"

#include <QByteArray>
#include <QVarLengthArray>

#include <pwd.h>
#include <unistd.h>

#include <cerrno>

namespace omahouse {

namespace {

// getpwnam_r wants a scratch buffer and says so with ERANGE when the one it was
// given is short. sysconf gives the suggested size; the loop is what makes the
// suggestion advisory, since a user in a directory service can carry more than
// the local files ever would.
constexpr long kFirstBuffer = 1024;
constexpr long kLargestBuffer = 64 * 1024;

long suggestedBuffer()
{
    const long suggested = ::sysconf(_SC_GETPW_R_SIZE_MAX);
    return suggested > 0 ? suggested : kFirstBuffer;
}

template <typename Lookup>
bool lookup(Lookup query, passwd *entry, passwd **found)
{
    QVarLengthArray<char, 1024> buffer;
    for (long size = suggestedBuffer(); size <= kLargestBuffer; size *= 2) {
        buffer.resize(static_cast<int>(size));
        const int status = query(entry, buffer.data(), static_cast<size_t>(size), found);
        if (status == 0)
            return *found != nullptr;
        if (status != ERANGE)
            return false;
    }
    return false;
}

} // namespace

bool uidForUser(const QString &user, uid_t *uid)
{
    if (user.isEmpty())
        return false;
    const QByteArray name = user.toLocal8Bit();
    passwd entry {};
    passwd *found = nullptr;
    const bool ok = lookup(
        [&name](passwd *entry, char *buffer, size_t size, passwd **found) {
            return ::getpwnam_r(name.constData(), entry, buffer, size, found);
        },
        &entry, &found);
    if (!ok)
        return false;
    if (uid)
        *uid = found->pw_uid;
    return true;
}

QString userForUid(uid_t uid)
{
    passwd entry {};
    passwd *found = nullptr;
    const bool ok = lookup(
        [uid](passwd *entry, char *buffer, size_t size, passwd **found) {
            return ::getpwuid_r(uid, entry, buffer, size, found);
        },
        &entry, &found);
    if (!ok)
        return {};
    return QString::fromLocal8Bit(found->pw_name);
}

QString currentUser()
{
    return userForUid(::getuid());
}

} // namespace omahouse
