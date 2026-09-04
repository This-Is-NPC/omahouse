#pragma once

#include "WebPolicy.h"

#include <QString>

namespace omahouse {

// The half of docs/design.md §11 that touches the machine: putting the managed
// policy file on disk, and taking it off again.
//
// What goes *in* the file is worked out by `chromiumPolicyFor` in `src/core`,
// which has no idea where the file lives. This side has no idea what the rules
// mean. That is the same line `Policy` and `Proc` are on either side of, and it
// is what lets every interesting case -- profiles that disagree, the last rule
// being taken back -- be proved by a test that never writes a byte.
//
// Two things this file is careful about, and both are about a directory omahouse
// does not own:
//
// It writes one file of its own name and never edits a shared one. Omarchy's
// `browser-policy.sh` writes `policies.json` in the same directory; Chromium
// merges every file it finds there, so `omahouse.json` beside it is the whole
// of the integration. Nothing here reads, rewrites or removes a file with
// another name -- which is also what makes `pacman -R` able to undo this
// completely, by removing one path.
//
// And it takes the file away rather than emptying it. `ChromiumPolicy::needed()`
// going false is the last web rule on the machine being taken back, and the
// machine has to end up where it was before the first one was written: no file
// at all. An empty managed policy left behind is a machine that still looks
// managed, and the whole reason this is a first-class path is that a parental
// control which leaves restrictions behind is worse than none.

/// Puts `policy` at `path`, atomically and 0644.
///
/// 0644 because Chromium reads the managed policy as whoever started the
/// browser, which is never root -- a mode only root can read is a policy that
/// does not apply. Atomically because a browser starting during the write must
/// see the whole of the last decision or the whole of the one before, never the
/// first half of a JSON document; a policy file that will not parse is one
/// Chromium ignores, and a rule that silently is not in force is the failure
/// this whole slice exists to prevent.
bool writeChromiumPolicy(const QString &path, const ChromiumPolicy &policy, QString *error);

/// Takes the file away. A file that is not there is success and not a failure:
/// the caller is asking for a machine with no omahouse policy on it, and that is
/// what it already has.
bool removeChromiumPolicy(const QString &path, QString *error);

/// Whether `path` already says exactly `policy`, so a decision that has not
/// changed is not written again.
///
/// Anything else at all -- no file, a file that will not parse, a file with
/// other keys in it, a file somebody edited by hand -- is `false`, and the
/// caller writes. This file has omahouse's name on it, so the answer to finding
/// something unexpected in it is to make it say what the profiles say, not to
/// leave it alone and let the machine disagree with `omahouse status`.
bool chromiumPolicyIsAlready(const QString &path, const ChromiumPolicy &policy);

} // namespace omahouse
