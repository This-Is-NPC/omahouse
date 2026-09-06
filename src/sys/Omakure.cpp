#include "Omakure.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include <cerrno>
#include <cstring>

#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace omahouse {
namespace {

/// The workspace, which is never the state directory. See Omakure.h.
QString workspace()
{
    const QByteArray set = qgetenv("OMAHOUSE_OMAKURE_WORKSPACE");
    if (!set.isEmpty())
        return QString::fromLocal8Bit(set);
    return QStringLiteral("/var/lib/omakure-workspace");
}

/// Long enough for `node init` to make a key pair on a slow machine, and short
/// enough that a call which is never going to answer does not hold the terminal
/// forever.
constexpr int kStartTimeout = 10000;

QString environmentOr(const char *name, const QString &fallback)
{
    const QByteArray set = qgetenv(name);
    return set.isEmpty() ? fallback : QString::fromLocal8Bit(set);
}

bool accountExists(const QString &name)
{
    return getpwnam(name.toLocal8Bit().constData()) != nullptr;
}

/// Whether this process is already the account every call has to be made as.
bool amTheAccount(const QString &name)
{
    const struct passwd *who = getpwnam(name.toLocal8Bit().constData());
    return who && who->pw_uid == geteuid();
}

} // namespace

QString Omakure::configDir()
{
    return environmentOr("OMAHOUSE_OMAKURE_CONFIG_DIR", QStringLiteral("/etc/omakure"));
}

QString Omakure::stateDir()
{
    return environmentOr("OMAHOUSE_OMAKURE_STATE_DIR", QStringLiteral("/var/lib/omakure"));
}

QString Omakure::nodeConfigFile()
{
    return configDir() + QStringLiteral("/node.toml");
}

QString Omakure::transportCertFile()
{
    return stateDir() + QStringLiteral("/transport.cert");
}

QString Omakure::account()
{
    return environmentOr("OMAHOUSE_OMAKURE_USER", QStringLiteral("omakure"));
}

QString Omakure::binary()
{
    const QByteArray set = qgetenv("OMAHOUSE_OMAKURE_BIN");
    if (!set.isEmpty()) {
        // Checked, and not taken on faith. A variable pointing at nothing is a
        // machine with no Omakure on it, and saying so here is what makes the
        // one question `installed` asks answerable in one sentence instead of
        // failing four verbs later on `cannot run omakure`.
        const QString path = QString::fromLocal8Bit(set);
        return QFileInfo(path).isExecutable() ? path : QString();
    }
    return QStandardPaths::findExecutable(QStringLiteral("omakure"));
}

bool Omakure::installed(QString *why)
{
    if (binary().isEmpty()) {
        if (why)
            *why = QStringLiteral("there is no omakure on this machine");
        return false;
    }
    // The account and not the directory, because the directory is made by the
    // first call and the account is not. Omakure refuses a state directory whose
    // service user does not exist -- "node path is insecure" -- and that refusal
    // is the same one for every verb below, so it is worth answering once here
    // in words that name the installer instead.
    if (!accountExists(account())) {
        if (why)
            *why = QStringLiteral("omakure is installed but its service account (%1) "
                                  "is not")
                           .arg(account());
        return false;
    }
    return true;
}

bool Omakure::run(const QStringList &arguments, QString *output, QString *error,
                  int timeoutMs)
{
    const QString program = binary();
    if (program.isEmpty()) {
        *error = QStringLiteral("there is no omakure on this machine");
        return false;
    }

    // As the node's account, one way or the other. Already being it is the
    // ordinary case inside a Cue; being root is the ordinary case for a person
    // running `machine prepare`. Anything else cannot get there and says so,
    // rather than running as itself and leaving files nobody can clean up.
    QString launcher = program;
    QStringList argv = arguments;
    if (!amTheAccount(account())) {
        if (geteuid() != 0) {
            *error = QStringLiteral("omakure has to be run as %1, and this is neither "
                                    "%1 nor root")
                             .arg(account());
            return false;
        }
        launcher = QStringLiteral("sudo");
        argv = QStringList {QStringLiteral("-n"), QStringLiteral("-u"), account(),
                            QStringLiteral("env"),
                            QStringLiteral("HOME=%1").arg(workspace()), program}
                + arguments;
    }

    QProcess asking;
    asking.setProgram(launcher);
    asking.setArguments(argv);
    asking.start();
    if (!asking.waitForStarted(kStartTimeout)) {
        *error = QStringLiteral("cannot run omakure: %1").arg(asking.errorString());
        return false;
    }
    if (!asking.waitForFinished(timeoutMs)) {
        asking.kill();
        asking.waitForFinished();
        *error = QStringLiteral("omakure %1 did not answer in %2s")
                         .arg(arguments.value(0))
                         .arg(timeoutMs / 1000);
        return false;
    }

    const QString said = QString::fromLocal8Bit(asking.readAllStandardOutput());
    // Both channels. Omakure's refusals are on the second one, and a caller
    // that reads only the first reports a failure with nothing in it.
    const QString complained =
            QString::fromLocal8Bit(asking.readAllStandardError()).trimmed();
    if (output)
        *output = said;
    if (asking.exitStatus() != QProcess::NormalExit || asking.exitCode() != 0) {
        *error = QStringLiteral("omakure %1 failed (%2)%3")
                         .arg(arguments.value(0))
                         .arg(asking.exitCode())
                         .arg(complained.isEmpty()
                                      ? QString()
                                      : QStringLiteral(": %1").arg(complained));
        return false;
    }
    return true;
}

bool Omakure::runJson(const QStringList &arguments, QJsonObject *data, QString *error,
                      int timeoutMs)
{
    QString said;
    if (!run(QStringList {QStringLiteral("--json")} + arguments, &said, error, timeoutMs))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(said.toUtf8());
    if (!document.isObject()) {
        *error = QStringLiteral("omakure %1 did not answer in JSON").arg(arguments.value(0));
        return false;
    }
    *data = document.object().value(QStringLiteral("data")).toObject();
    return true;
}

bool Omakure::identity(QString *nodeId, QString *publicKey, QString *error)
{
    QJsonObject data;
    if (!runJson({QStringLiteral("node"), QStringLiteral("status")}, &data, error))
        return false;
    const QJsonObject who = data.value(QStringLiteral("identity")).toObject();
    *nodeId = who.value(QStringLiteral("node_id")).toString();
    *publicKey = who.value(QStringLiteral("public_key")).toString();
    return true;
}

bool Omakure::initialise(QString *error)
{
    QString existing;
    QString key;
    QString ignored;
    // A `node status` that fails is a node with nothing to read yet, which is
    // exactly the state `node init` is for -- so its failure is not one.
    if (identity(&existing, &key, &ignored) && !existing.isEmpty()) {
        *error = QStringLiteral("this machine already has an Omakure identity (%1). "
                                "Initialising again would replace the identity other "
                                "machines already trust")
                         .arg(existing);
        return false;
    }

    QString said;
    if (!run({QStringLiteral("node"), QStringLiteral("init")}, &said, error))
        return false;
    if (!identity(&existing, &key, error))
        return false;
    if (existing.isEmpty()) {
        // `node status` answers happily with a null identity, so the exit code
        // of `init` alone is not the question.
        *error = QStringLiteral("omakure node init reported success but this machine "
                                "still has no identity");
        return false;
    }
    return true;
}

bool Omakure::transportCertificate(QString *hex, QString *error)
{
    QFile file(transportCertFile());
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read %1: %2")
                         .arg(transportCertFile(), file.errorString());
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();
    *hex = QString::fromLatin1(raw.toHex());
    if (!looksLikeCertificate(*hex)) {
        *error = QStringLiteral("%1 holds no usable transport certificate")
                         .arg(transportCertFile());
        return false;
    }
    return true;
}

bool Omakure::describe(const QString &endpoint, Pairing *out, QString *error)
{
    Pairing pairing;
    pairing.endpoint = endpoint;
    if (!identity(&pairing.nodeId, &pairing.publicKey, error))
        return false;
    if (pairing.nodeId.isEmpty()) {
        *error = QStringLiteral("this machine has no Omakure identity yet");
        return false;
    }
    if (!transportCertificate(&pairing.transportCert, error))
        return false;
    *out = pairing;
    return true;
}

bool Omakure::trust(const Pairing &peer, const QString &role, QString *error)
{
    QString said;
    // The capabilities omahouse uses and no others. A trust line is the widest
    // door in this design, and every capability granted here is one a peer can
    // use for something nobody asked for.
    return run({QStringLiteral("node"), QStringLiteral("trust"),
                QStringLiteral("--node-id"), peer.nodeId,
                QStringLiteral("--public-key"), peer.publicKey,
                QStringLiteral("--transport-certificate"), peer.transportCert,
                QStringLiteral("--role"), role,
                QStringLiteral("--capability"), QStringLiteral("inventory-health"),
                QStringLiteral("--capability"), QStringLiteral("notifications"),
                QStringLiteral("--capability"), QStringLiteral("remote-run"),
                QStringLiteral("--actor"), QStringLiteral("household"),
                QStringLiteral("--reason"), QStringLiteral("household"),
                QStringLiteral("--confirmed")},
               &said, error);
}

bool Omakure::writeSystemFile(const QString &path, const QString &contents,
                              const QString &group, int mode, QString *error)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        *error = QStringLiteral("cannot make %1").arg(info.absolutePath());
        return false;
    }

    // Written beside and moved into place, so that a machine interrupted here
    // has either the old file or the new one. A half-written node.toml is a
    // node that will not start, and the interruption that produces it is a
    // household closing the laptop.
    const QString beside = path + QStringLiteral(".omahouse-new");
    QFile file(beside);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("cannot write %1: %2").arg(beside, file.errorString());
        return false;
    }
    if (file.write(contents.toUtf8()) < 0) {
        *error = QStringLiteral("cannot write %1: %2").arg(beside, file.errorString());
        file.close();
        QFile::remove(beside);
        return false;
    }
    file.close();

    // Ownership before the move, so the file is never briefly readable by the
    // wrong account under its real name.
    gid_t gid = 0;
    if (!group.isEmpty()) {
        const struct group *found = getgrnam(group.toLocal8Bit().constData());
        if (!found) {
            *error = QStringLiteral("there is no %1 group on this machine").arg(group);
            QFile::remove(beside);
            return false;
        }
        gid = found->gr_gid;
    }
    if (chown(beside.toLocal8Bit().constData(), 0, gid) != 0
            || chmod(beside.toLocal8Bit().constData(), static_cast<mode_t>(mode)) != 0) {
        *error = QStringLiteral("cannot set the owner of %1: %2")
                         .arg(beside, QString::fromLocal8Bit(strerror(errno)));
        QFile::remove(beside);
        return false;
    }
    if (rename(beside.toLocal8Bit().constData(), path.toLocal8Bit().constData()) != 0) {
        *error = QStringLiteral("cannot put %1 in place: %2")
                         .arg(path, QString::fromLocal8Bit(strerror(errno)));
        QFile::remove(beside);
        return false;
    }
    return true;
}

bool Omakure::enableService(QString *error)
{
    const QString unit = QStringLiteral("omakure-node.service");
    const QString dropIn = environmentOr("OMAHOUSE_SYSTEMD_DIR",
                                         QStringLiteral("/etc/systemd/system"))
            + QStringLiteral("/") + unit + QStringLiteral(".d/omahouse.conf");
    const QString contents = QStringLiteral(
            "# Written by omahouse. The unit itself is Omakure's and is left alone:\n"
            "# it is rewritten by every reinstall, and a household that upgrades\n"
            "# Omakure must not silently lose its wire.\n"
            "[Service]\n"
            "ExecStart=\n"
            "ExecStart=%1 node serve --allow-non-loopback-direct --workers 1\n"
            "NoNewPrivileges=no\n")
            .arg(binary());
    if (!writeSystemFile(dropIn, contents, QString(), 0644, error))
        return false;

    // Best effort, and said as such by the caller. A machine with no systemd --
    // a container, the end to end suite -- has a node.toml, an identity and a
    // trust registry, all of which are the parts that are hard to get right;
    // what it does not have is something to start the service, and that is not
    // a reason to call the pairing failed.
    QString said;
    QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("daemon-reload")});
    if (QProcess::execute(QStringLiteral("systemctl"),
                          {QStringLiteral("enable"), QStringLiteral("--now"), unit}) != 0) {
        *error = QStringLiteral("%1 was configured but would not start").arg(unit);
        return false;
    }
    return true;
}

bool Omakure::generateToken(const QString &id, QString *token, QString *fileEntry,
                            QString *error)
{
    QJsonObject data;
    if (!runJson({QStringLiteral("token"), QStringLiteral("generate"),
                  QStringLiteral("--id"), id,
                  QStringLiteral("--scope"), QStringLiteral("node:read"),
                  QStringLiteral("--scope"), QStringLiteral("node:write")},
                 &data, error))
        return false;
    *token = data.value(QStringLiteral("token")).toString();
    *fileEntry = data.value(QStringLiteral("tokens_file_entry")).toString();
    if (token->isEmpty() || fileEntry->isEmpty()) {
        *error = QStringLiteral("omakure token generate answered without a token");
        return false;
    }
    return true;
}

} // namespace omahouse
