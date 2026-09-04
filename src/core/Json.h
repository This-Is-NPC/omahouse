#pragma once

#include <QJsonObject>
#include <QString>

namespace omahouse {

// The version both files of docs/design.md §4 carry, and the only one this code can
// read. A file from a newer omahouse is a file whose fields this one would have
// to invent, so a reader that meets another number says so and stops.
constexpr int kSchemaVersion = 1;

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

/// Checks the `schemaVersion` of a document read back from disk. `what` names
/// the file in the message, because "unknown schema version 2" is only useful
/// to somebody who is told which of the two files said it.
bool checkSchemaVersion(const QJsonObject &root, const QString &what, QString *error);

} // namespace omahouse
