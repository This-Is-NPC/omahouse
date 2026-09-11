#pragma once

#include <QJsonObject>
#include <QString>

namespace omahouse {

// A version per file, and not one for all of them.
//
// It was one number carried by four: profiles.json, the day's ledger,
// machine.json and machines.json. Which meant a change to any of them invalidated
// every one of the others -- a profile gaining a meaning would throw away the
// day a machine was in the middle of, and the household's list of computers, and
// what the machine says it is. Nobody would connect the upgrade to the loss.
//
// The effect of one number for four is that it is never raised. Raising it is
// always too expensive for what it is about, so the check that exists to refuse
// a document this code would misread sits there never firing -- which is how a
// profile whose `user` is `*` reached a reader that parsed it happily and
// ignored the rules in it. Separately, each is cheap and gets raised when it
// should be.
//
// What the number means is unchanged: a document written by a newer omahouse is
// one this code would have to invent to read, so a reader that meets another
// number says so and stops. **And "would have to invent" is about meaning and
// not only about fields.** `*` added no field; it changed what a valid document
// says, and an older reader applied its rules to an account that cannot exist
// and quietly enforced nothing.

/// `profiles.json`. Raised to 2 when a profile's `user` could be `*`, meaning
/// anybody without one of their own: an older reader parses such a file without
/// complaint and silently binds nobody, and a reader that ignores rules is worse
/// than one that stops.
constexpr int kProfilesSchema = 2;
/// `/var/lib/omahouse/<user>/<date>.json`. Deliberately not moved by the above:
/// a machine in the middle of a day keeps its day.
constexpr int kLedgerSchema = 1;
/// `/etc/omahouse/machine.json` -- what this machine is in the household.
constexpr int kMachineSchema = 1;
/// `/etc/omahouse/machines.json` -- the household's list of computers.
constexpr int kMachinesSchema = 1;
constexpr int kPublicationSchema = 1;

/// Reads a JSON object from `path`. `missing` tells "there is no file yet"
/// apart from "the file is unreadable": the first is the ordinary state of a
/// ledger before the day's first tick, the second is a failure, and a caller
/// that cannot tell them apart either treats a broken file as an empty day or
/// refuses to start on a machine that has never run.
bool readJsonObject(const QString &path, QJsonObject *out, QString *error,
                    bool *missing = nullptr);

/// Writes `object` to `path` through a sibling temporary file that is flushed,
/// synced, and renamed over the target -- docs/design.md §4's tmp + rename.
///
/// The core owns the routine and not the destination: a reader of the ledger is
/// a program that has to see either the whole of the last tick or the whole of
/// the one before it, never the first half of a write that a power cut ended,
/// and that is a property of how the bytes land rather than of which directory
/// they land in. `path` comes in by parameter for the same reason `now` does.
bool writeJsonAtomically(const QString &path, const QJsonObject &object, QString *error);

/// Checks the `schemaVersion` of a document read back from disk against the one
/// that file is on. `what` names the file in the message, because "unknown
/// schema version 3" is only useful to somebody who is told which said it.
///
/// **Exactly the number, and never a range.** Somebody will propose accepting
/// an older one, and the argument will be good: a reader that understands
/// everything an older document can say would read it correctly, so refusing it
/// looks like rigour for its own sake.
///
/// What is wrong with it is not here, it is downstream. A `profiles.json` this
/// omahouse refuses is a `profiles.json` omahouse enforces nothing from. Any
/// other program that read it anyway -- omastore reads this very file and works
/// out its own verdict from it -- would be reporting rules that nothing behind
/// them is going to carry out. The exact comparison is not strictness; it is
/// what makes two readers agree about which file is in force.
bool checkSchemaVersion(const QJsonObject &root, int expected, const QString &what,
                        QString *error);

} // namespace omahouse
