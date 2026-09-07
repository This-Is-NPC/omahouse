#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace omahouse {

// What this machine is in the household: on its own, the one that manages, or
// one that is managed.
//
// It was implicit until now -- a machine became a manager by running `machine
// invite` and became managed by running `machine prepare` -- and implicit is
// exactly wrong for this. Every later verb reads differently depending on it:
// `house` is the manager's question, `day` is a managed machine's answer, and a
// console open to the household belongs on one of them and not the other. A
// person who cannot ask a machine which it is cannot check any of that.
//
// `Alone` is the default and it is not a lesser state. Most households are one
// computer, and one computer under rules is the whole product working: the
// budgets are enforced, the browser is held, the day is written. Linking is what
// a second computer needs, not what the first one was missing.
//
// **A managed machine gets the whole of omahouse, not an agent.** That is the
// decision this file is really about. A thin client would be smaller and would
// fail the one situation it exists for: when the manager cannot be reached, the
// operator logs into the managed machine as root and fixes it there. A machine
// that could only be administered from somewhere else is a machine that is
// unadministrable exactly when something has gone wrong.

enum class Kind {
    /// No other computer, and nothing missing. The ordinary state.
    Alone,
    /// The household's own console: it holds the fleet, it collects the days,
    /// and nothing may be cued at it.
    Manager,
    /// Under a manager. It still enforces its own rules on its own, and it
    /// still answers to whoever has root on it.
    Managed,
};

QString kindName(Kind kind);
bool kindFromName(const QString &name, Kind *out);

/// A sentence a person can read, for the verb that prints it.
QString kindSaid(Kind kind);

// -- what this machine writes down about itself -------------------------------

/// `/etc/omahouse/machine.json`, whole.
///
/// Deliberately not part of `machines.json`. That file is the household's list
/// of *other* computers and a machine can be in somebody else's list without
/// knowing it; this is what this machine says about itself, and conflating the
/// two would mean a manager's own row appearing in its own fleet.
struct ThisMachine {
    Kind kind = Kind::Alone;
    /// What this machine calls itself. The household's word, not a hostname:
    /// hostnames get changed and the household's word does not.
    QString name;
    /// The manager's Omakure identity, when there is one. Empty on a manager
    /// and on a machine that is alone.
    QString managedBy;
    /// When the kind was last set. Not when it was last seen -- this file is a
    /// standing fact and not a heartbeat.
    QDateTime since;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, ThisMachine *out, QString *error);
};

/// Read it, or say why not. A file that is not there is a machine that is
/// `Alone`, which is the ordinary state and never an error -- `missing` says so
/// while `out` comes back with the default.
bool readThisMachine(const QString &path, ThisMachine *out, QString *error,
                     bool *missing = nullptr);
bool writeThisMachine(const QString &path, const ThisMachine &machine, QString *error);

} // namespace omahouse
