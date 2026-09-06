#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace omahouse {

// The machines of one household, and nothing about what runs on them.
//
// omahouse knows accounts. It has never known machines, and every fleet thing
// proved so far -- the wire, the trust gates, a Cue that lands -- was driven by
// a test harness holding addresses in its own memory. This is where a household
// writes down which computers are its own.
//
// It is deliberately thin. A machine here is a name, the node identity to reach
// it by, and where it answers. What the machine *does* is `profiles.json`'s
// business and lives on the machine itself, because a central that held the
// rules would be a central whose absence is a machine with no rules.
//
// The name is the household's word for it -- "the kitchen laptop" -- and the
// node id is Omakure's. Both are kept because they answer different questions:
// one is what a person says out loud, the other is what a peer is checked
// against, and conflating them means renaming a computer breaks its trust.

struct Machine {
    /// What the household calls it. Unique, and the handle every verb takes.
    QString name;
    /// The Omakure node identity, `omk1_<hex>`. Empty until the machine has
    /// been paired -- a machine can be written down before it is reachable,
    /// which is the state between "we own that computer" and "it answers".
    QString nodeId;
    /// Where its wire listens, `host:port`. Empty for the same reason.
    QString endpoint;
    /// When it was written down. Not when it was last seen: this file is the
    /// household's list and not a health record, and mixing the two would make
    /// every heartbeat a write to a file the operator edits.
    QDateTime addedAt;

    /// Whether this entry is enough to reach the machine. A name on its own is
    /// a note to self; a name with an identity and an endpoint is a peer.
    bool reachable() const { return !nodeId.isEmpty() && !endpoint.isEmpty(); }

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Machine *out, QString *error);
};

/// The whole of `/etc/omahouse/machines.json`.
QJsonObject machinesToJson(const QVector<Machine> &machines);
bool machinesFromJson(const QJsonObject &root, QVector<Machine> *out, QString *error);

/// The one this file is about, by name. `-1` when there is none.
int indexOfMachine(const QVector<Machine> &machines, const QString &name);

/// Read `machines.json`, or say why not. A file that is not there is not a
/// failure: it is a household of one computer, which is most of them, and
/// `missing` says so while `out` comes back empty.
bool readMachines(const QString &path, QVector<Machine> *out, QString *error,
                  bool *missing = nullptr);
bool writeMachines(const QString &path, const QVector<Machine> &machines, QString *error);

} // namespace omahouse
