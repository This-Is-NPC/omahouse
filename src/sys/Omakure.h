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
    static QString workspace();
    static bool runBatteryScript(const QString &script, const QStringList &arguments,
        QString *output, QString *stderr, int *exitCode, QString *error, int timeoutMs = 120000);
    /// The binary, resolved through PATH. Empty when there is none, which is
    /// the answer `machine prepare` turns into "install Omakure first".
    static QString binary();

    /// Whether there is an Omakure on this machine at all: a binary, and the
    /// service account its state directory belongs to. Both, because a binary
    /// with no account is a node that cannot read its own state, and every call
    /// below would fail on the same wrong thing in a different sentence.
    static bool installed(QString *why = nullptr);

    /// Make the account and the directories a node needs, if they are not there.
    ///
    /// omahouse does this because omahouse is what promised it. A household is
    /// told that one command links a computer and that they will not have to go
    /// and find anything: the package brings the binary, and the account and the
    /// 0700 state directory are the rest of what "brings" has to mean.
    ///
    /// It never touches an account that already exists, so a machine where
    /// Omakure's own installer has run is left exactly as that installer left
    /// it. The modes are the ones that installer sets, and they are not
    /// decoration: the state directory is a secrets directory, and a `node init`
    /// that ran before the account existed leaves root-owned locks in it that
    /// every later run refuses.
    ///
    /// The home is the workspace and never the state directory. An omakure
    /// command run as this account without `OMAKURE_SCRIPTS_DIR` resolves its
    /// scratch files under `$HOME`, so making the state directory the home
    /// points the product's own scratch files at the one place that bricks it.
    static bool provision(QString *error);

    /// Register and sync the public Battery's master, then install its runtime
    /// adapters in the service workspace. Repeating pairing refreshes them;
    /// schedules remain an explicit operator choice.
    static bool installBattery(const QString &name, QString *error);

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
    /// must not silently lose its wire. Three things are overridden and every
    /// one is deliberate.
    ///
    /// `--allow-non-loopback-direct`, because the wire binds every address and
    /// Omakure refuses that by default -- a machine is never on the network
    /// because somebody forgot a flag. `NoNewPrivileges=no`, because the account
    /// that answers a Cue reaches omahouse through one sudoers line and
    /// `NoNewPrivileges` makes that line a no-op.
    ///
    /// And `--bind ... --allow-non-loopback` when `consoleOnTheNetwork`, which
    /// is the only way there is: Omakure will not load a config whose `api.bind`
    /// is not loopback, so a console that answers the household is opened here
    /// or nowhere. Passed in rather than inferred, because the caller is the
    /// only one that knows -- `machine prepare` opens it and `machine invite`
    /// does not, and a machine that guessed would be a door nobody chose.
    static bool enableService(bool consoleOnTheNetwork, QString *error);

    /// A bearer for this node's own API, and the hashed line for its tokens
    /// file. `node serve` refuses to start without one at all -- "auth required:
    /// set OMAKURE_TOKENS_FILE" -- which reads like a peering problem and is a
    /// startup one.
    static bool generateToken(const QString &id, QString *token, QString *fileEntry,
                              QString *error);

    /// Where the bearer is kept: `<omahouse config>/omakure-token`, 0600 root.
    ///
    /// On disk and never on screen. The hashed half goes in Omakure's tokens
    /// file, and the half that opens the door has to live somewhere a script
    /// can read it -- printing it instead would put a working credential in a
    /// terminal's scrollback, an operator's clipboard and, sooner or later, a
    /// screenshot of a pairing that went well.
    static QString tokenFile();

    /// Both halves in one act: the hashed entry into Omakure's tokens file and
    /// the bearer into ours.
    static bool provisionToken(QString *error);

    /// A second bearer, for the operator's machine to read this one with, and
    /// the hashed entry appended beside the first.
    ///
    /// Scoped to running declared scripts and reading what they printed, and
    /// never to `node:write`. It is the same power the Cue path already grants
    /// -- the declared scripts are the same scripts -- so this widens the way
    /// in and not what can be done once in. Withholding `node:write` is what
    /// keeps a stolen bearer from re-trusting peers or rewriting the config,
    /// which are the two things that would make it permanent.
    static bool provisionReadingToken(QString *token, QString *error);
};

} // namespace omahouse
