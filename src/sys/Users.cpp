#include "Users.h"

#include <QByteArray>
#include <QProcess>
#include <QVarLengthArray>
#include <QVector>

#include <grp.h>
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

QString operatorUser()
{
    // Two doors to root and one question: who is behind this. `pkexec` is the
    // window's, `sudo` is the terminal's, and both say who asked in the
    // environment because neither leaves it in the uid -- sudo sets the real
    // uid to root as well as the effective one, so `getuid()` below answers
    // `root` for a person who typed their own password a second ago.
    //
    // Without the second door the answer was `root` for every write from a
    // terminal, which is the line docs/cli.md already refuses about a grant:
    // `root gave kid ten minutes` is not what an operator wants to read back
    // in a month.
    //
    // Neither variable is trusted for anything but the record. Whoever can set
    // them is already running this as root and has no need of a forged name;
    // what they buy is a truthful answer on the ordinary path, and the file
    // they would be writing into is one they could write by hand.
    for (const char *asked : {"PKEXEC_UID", "SUDO_UID"}) {
        bool ok = false;
        const uint uid = qgetenv(asked).toUInt(&ok);
        if (!ok)
            continue;
        const QString name = userForUid(static_cast<uid_t>(uid));
        if (!name.isEmpty())
            return name;
    }
    return currentUser();
}

bool runningAsRoot()
{
    return ::geteuid() == 0;
}

bool isAdministrator(const QString &user, QString *why)
{
    if (why)
        why->clear();
    uid_t uid = 0;
    gid_t primary = 0;
    if (!lookup(
            [&](passwd *entry, char *buffer, size_t size, passwd **found) {
                const QByteArray name = user.toLocal8Bit();
                return ::getpwnam_r(name.constData(), entry, buffer, size, found);
            },
            [&](const passwd *found) {
                uid = found->pw_uid;
                primary = found->pw_gid;
            })) {
        // No such account is nobody, and nobody administers anything. It is
        // also the ordinary case for `profile add --create-user`, where the
        // question is asked before the account exists.
        return false;
    }

    if (uid == 0) {
        if (why)
            *why = QStringLiteral("is root");
        return true;
    }

    // The group by name, because the gid of `wheel` is 998 here and 10 on the
    // next machine.
    gid_t wheel = 0;
    bool haveWheel = false;
    {
        QVarLengthArray<char, 1024> buffer;
        group entry {};
        group *found = nullptr;
        for (long size = 1024; size <= 64 * 1024; size *= 2) {
            buffer.resize(static_cast<int>(size));
            const int status =
                ::getgrnam_r("wheel", &entry, buffer.data(), static_cast<size_t>(size), &found);
            if (status == ERANGE)
                continue;
            if (status == 0 && found != nullptr) {
                wheel = found->gr_gid;
                haveWheel = true;
            }
            break;
        }
    }
    // A machine with no wheel group at all is a machine where nobody is in it.
    // Not an error: `sudo` on a Debian is a different group, and answering "I
    // do not know, so nobody" here would be answering a question about a machine
    // this code is not on. docs/design.md §1 says wheel, so wheel is what is asked.
    if (!haveWheel)
        return false;

    if (primary == wheel) {
        if (why)
            *why = QStringLiteral("has wheel as its own group");
        return true;
    }

    // Every group the account is in, primary included. getgrouplist over the
    // member list of wheel alone, because a name can be in the list of one and
    // reached through the other.
    int count = 32;
    QVector<gid_t> groups(count);
    const QByteArray name = user.toLocal8Bit();
    if (::getgrouplist(name.constData(), primary, groups.data(), &count) < 0) {
        groups.resize(count > 0 ? count : 0);
        if (count <= 0 || ::getgrouplist(name.constData(), primary, groups.data(), &count) < 0)
            return false;
    }
    groups.resize(count);
    for (gid_t gid : groups) {
        if (gid == wheel) {
            if (why)
                *why = QStringLiteral("is in wheel");
            return true;
        }
    }
    return false;
}

QString useraddProgram()
{
    const QByteArray fromEnvironment = qgetenv("OMAHOUSE_USERADD");
    if (!fromEnvironment.isEmpty())
        return QString::fromLocal8Bit(fromEnvironment);
    return QStringLiteral("/usr/sbin/useradd");
}

bool createAccount(const QString &user, QString *error)
{
    QProcess useradd;
    useradd.setProgram(useraddProgram());
    useradd.setArguments({QStringLiteral("-m"), user});
    useradd.setProcessChannelMode(QProcess::MergedChannels);
    useradd.start();
    if (!useradd.waitForStarted()) {
        if (error) {
            *error = QStringLiteral("cannot run %1: %2")
                         .arg(useraddProgram(), useradd.errorString());
        }
        return false;
    }
    // A minute, and not forever: useradd on a machine with a directory service
    // can take seconds, and a CLI that hangs on it with nothing on the screen is
    // a CLI somebody kills halfway through an account being created.
    if (!useradd.waitForFinished(60 * 1000)) {
        useradd.kill();
        useradd.waitForFinished();
        if (error)
            *error = QStringLiteral("%1 did not finish in a minute").arg(useraddProgram());
        return false;
    }
    if (useradd.exitStatus() != QProcess::NormalExit || useradd.exitCode() != 0) {
        if (error) {
            const QString said = QString::fromLocal8Bit(useradd.readAll()).trimmed();
            *error = QStringLiteral("%1 -m %2 failed (%3)%4")
                         .arg(useraddProgram(), user)
                         .arg(useradd.exitCode())
                         .arg(said.isEmpty() ? QString() : QStringLiteral(": %1").arg(said));
        }
        return false;
    }
    return true;
}

} // namespace omahouse
