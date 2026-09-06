#pragma once

#include "Pairing.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace omahouse {

// The one place that runs the `omakure` binary.
//
// omahouse does not reimplement any of this. Identity, trust, the wire and the
// tokens are Omakure's, and every one of them is a subprocess call here --
// which is the whole reason this file is small and lives in `src/sys`. What
// omahouse owns is the *order*: which of those calls, with what, and in which
// sequence, so that a household ends up with two machines that speak instead of
// seven manual steps and a certificate typed by hand.
//
// Everything runs as the node's own account. That is not politeness: the state
// directory is 0700 and owned by the service user, and an `omakure` command run
// as root leaves root-owned lock files in it that every later run refuses --
// "is owned by 0:0 but must be owned by 964:964". The refusal is correct, and it
// is unrecoverable without deleting the identity, which is why the account is
// not a detail this class lets a caller get wrong.
//
// The paths are overridable for the same reason every other root in `src/sys`
// is: the end to end suite has to be able to prove the sequence without an
// Omakure on the machine, and without touching /etc.

class Omakure {
public:
    /// `/etc/omakure`, or `$OMAHOUSE_OMAKURE_CONFIG_DIR`.
    static QString configDir();
    /// `/var/lib/omakure`, or `$OMAHOUSE_OMAKURE_STATE_DIR`. The secrets
    /// directory, and never a workspace: an omakure command run with its HOME
    /// pointed here writes scratch files into the one place that bricks a node.
    static QString stateDir();
    /// `<configDir>/node.toml`.
    static QString nodeConfigFile();
    /// `<stateDir>/transport.cert`.
    static QString transportCertFile();
    /// The account every call is made as. `omakure`, or
    /// `$OMAHOUSE_OMAKURE_USER`.
    static QString account();
    /// The binary, resolved through PATH. Empty when there is none, which is
    /// the answer `machine prepare` turns into "install Omakure first".
    static QString binary();

    /// Whether there is an Omakure on this machine at all: a binary, and the
    /// service account its state directory belongs to. Both, because a binary
    /// with no account is an install that never ran `--install-node-service`,
    /// and every call below would fail on the same wrong thing.
    static bool installed(QString *why = nullptr);

    /// Run `omakure <arguments>` as the node's account, and hand back what it
    /// said. `stdout` and `stderr` both, because Omakure's refusals are on the
    /// second one and a caller that only reads the first reports an empty
    /// failure.
    static bool run(const QStringList &arguments, QString *output, QString *error,
                    int timeoutMs = 60000);

    /// The same, parsing `--json`'s `data` object out of the answer.
    static bool runJson(const QStringList &arguments, QJsonObject *data, QString *error,
                        int timeoutMs = 60000);

    /// This node's identity, or empty when it has none yet.
    ///
    /// `node status` answers happily with a null identity, so the exit code
    /// alone is not the question -- the question is whether there is a node.
    static bool identity(QString *nodeId, QString *publicKey, QString *error);

    /// `node init`, and then a check that it worked. Refuses to run twice: the
    /// second `node init` is the one that cannot be undone without deleting an
    /// identity two machines already trust.
    static bool initialise(QString *error);

    /// The transport certificate as hex, read off disk. Omakure prints it
    /// nowhere, so this is a file read -- and it is here rather than in the CLI
    /// because the path is Omakure's business, not omahouse's.
    static bool transportCertificate(QString *hex, QString *error);

    /// Everything this machine has to tell another, in one call.
    static bool describe(const QString &endpoint, Pairing *out, QString *error);

    /// `node trust`, with the capabilities omahouse actually uses and none
    /// besides. `role` is `conductor` for the machine that gives orders and
    /// `performer` for the one that takes them; naming it at the call site is
    /// what keeps a child's machine from being able to cue its parent's.
    static bool trust(const Pairing &peer, const QString &role, QString *error);

    /// Write a file root owns and the node reads. `/etc/omakure` is root's and
    /// the node's account only reads it, which is what the shipped installer
    /// leaves and what `node init` needs to already be true: left to create its
    /// own config, it answers `io_failed: Permission denied`.
    static bool writeSystemFile(const QString &path, const QString &contents,
                                const QString &group, int mode, QString *error);

    /// The drop-in that makes the shipped unit answer on the wire, and the
    /// reload and enable that follow.
    ///
    /// A drop-in and never an edit of the unit: the unit is Omakure's file, it
    /// is rewritten by every reinstall, and a household that upgrades Omakure
    /// must not silently lose its wire. Two things are overridden and both are
    /// deliberate. `--allow-non-loopback-direct`, because the wire binds every
    /// address and Omakure refuses that by default -- a machine is never on the
    /// network because somebody forgot a flag. And `NoNewPrivileges=no`, because
    /// the account that answers a Cue reaches omahouse through one sudoers line
    /// and `NoNewPrivileges` makes that line a no-op.
    static bool enableService(QString *error);

    /// A bearer for this node's own API, and the hashed line for its tokens
    /// file. `node serve` refuses to start without one at all -- "auth required:
    /// set OMAKURE_TOKENS_FILE" -- which reads like a peering problem and is a
    /// startup one.
    static bool generateToken(const QString &id, QString *token, QString *fileEntry,
                              QString *error);
};

} // namespace omahouse
