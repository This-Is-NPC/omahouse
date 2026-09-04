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

/// Runs one getpw*_r and hands the entry to `take` while the buffer is still
/// alive.
///
/// The two halves have to happen inside the one function. Every string in a
/// `struct passwd` -- `pw_name` among them -- points into the caller's scratch
/// buffer, so returning the entry and reading it afterwards is reading freed
/// memory, and it does not look like it: the first version of this file returned
/// the whole line of /etc/passwd as a user name instead of crashing.
template <typename Query, typename Take>
bool lookup(Query query, Take take)
{
    QVarLengthArray<char, 1024> buffer;
    passwd entry {};
    passwd *found = nullptr;
    for (long size = suggestedBuffer(); size <= kLargestBuffer; size *= 2) {
        buffer.resize(static_cast<int>(size));
        const int status = query(&entry, buffer.data(), static_cast<size_t>(size), &found);
        if (status == ERANGE)
            continue;
        if (status != 0 || found == nullptr)
            return false;
        take(found);
        return true;
    }
    return false;
}

} // namespace

bool uidForUser(const QString &user, uid_t *uid)
{
    if (user.isEmpty())
        return false;
    const QByteArray name = user.toLocal8Bit();
    return lookup(
        [&name](passwd *entry, char *buffer, size_t size, passwd **found) {
            return ::getpwnam_r(name.constData(), entry, buffer, size, found);
        },
        [uid](const passwd *found) {
            if (uid)
                *uid = found->pw_uid;
        });
}

QString userForUid(uid_t uid)
{
    QString name;
    lookup(
        [uid](passwd *entry, char *buffer, size_t size, passwd **found) {
            return ::getpwuid_r(uid, entry, buffer, size, found);
        },
        [&name](const passwd *found) { name = QString::fromLocal8Bit(found->pw_name); });
    return name;
}

QString currentUser()
{
    return userForUid(::getuid());
}

} // namespace omahouse
