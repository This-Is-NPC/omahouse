#pragma once

#include "Ledger.h"
#include "Profile.h"

#include <QDateTime>
#include <QPair>
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

// -- what the house spent, as opposed to what this machine spent --------------
//
// The profile's number is the household's number. `session: 120` means two
// hours in the house and not two hours per computer -- so a machine on its own
// enforces the whole thing, which is right, and a house with three of them has
// to add the three up and push the truth back down with `leave`.
//
// The adding is here because it is arithmetic and belongs where the rest of the
// arithmetic is. Getting the other machines' days to this one is transport, and
// transport is somebody else's job.

/// One machine's contribution to one budget.
struct Contribution {
    QString machine;
    int seconds = 0;
};

/// One budget, added up across the house.
struct HouseBudget {
    QString id;
    /// Below zero when the household has no number for this budget.
    ///
    /// Two ways that happens and one meaning. A budget nobody put a limit on
    /// counts and never runs out. A budget that never resets is held per
    /// machine -- the statement cannot carry a pot, docs/design.md §12 -- so
    /// there is no household capacity to print, and `spent` beside it is still
    /// every computer's real spending of its own.
    int limitSeconds = -1;
    /// Every machine's seconds, in the order the machines were given.
    QVector<Contribution> spent;
    int totalSeconds = 0;

    bool hasLimit() const { return limitSeconds >= 0; }
    /// What is left of the household's number. Zero once it is spent, never
    /// below: a house that went over is a house with nothing left, and a
    /// negative would only invite somebody to subtract it twice.
    int leftSeconds() const
    {
        return hasLimit() ? qMax(0, limitSeconds - totalSeconds) : 0;
    }
};

/// The day of one person across the house.
///
/// `days` pairs a machine's name with that machine's ledger for the day. The
/// profile decides which budgets exist and what each is worth; a machine that
/// spent time against a budget the profile no longer names is added all the
/// same, because the seconds happened.
QVector<HouseBudget> consolidate(const Profile &profile,
                                 const QVector<QPair<QString, Ledger>> &days);

} // namespace omahouse
