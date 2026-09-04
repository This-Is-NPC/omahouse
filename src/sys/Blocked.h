#pragma once

#include <QString>
#include <QStringList>

namespace omahouse {

// `/etc/omahouse/blocked`, and the other half of `logout` -- spec.md §2.
//
// `loginctl terminate-user` on its own is theatre. poc/findings.md round 2
// measured it: the session went down in seconds and the tty1 autologin brought
// it straight back up, Hyprland and all, which on a household machine is the
// ordinary case rather than the odd one. So ending a session and refusing the
// next one are one action with two halves, and round 3 measured the second half
// as a stock PAM module and no code of ours:
//
//     account required pam_listfile.so item=user sense=deny \
//             file=/etc/omahouse/blocked onerr=succeed
//
// `onerr=succeed` is not optional and is not this file's decision to make -- it
// is the packaging's, and it is written down in omahouse.install -- but it is
// the reason this file can be missing without anything breaking: a `blocked`
// that is not there, or not readable, lets everybody in. The failure that leaves
// the rules soft is recoverable and the one that leaves them hard is not, which
// is the same choice `default: allow` makes in spec.md §4.
//
// The format is not ours. `pam_listfile` reads one name per line, so this is the
// one file omahouse writes whose shape somebody else chose, and it is written
// the way the ledger is: a sibling temporary, fsync, rename. A PAM stack that
// read half a line during a login would refuse somebody for a name that never
// existed.
//
// The content is the whole answer and never a diff. `watch` works out, every
// cycle, exactly which accounts should be refused right now, and writes that
// set. That is what makes the name come out again on its own -- at the turn of
// the day, when the balance resets; when an operator grants ten minutes; when
// enforcement is switched off; when the profile is removed altogether -- without
// any of those verbs having to know that this file exists.

/// The names in `path`, in the order they were written, without blanks.
///
/// A file that is not there is an empty list and not a failure: it is the state
/// of every machine where nobody has run out of time yet. A file that cannot be
/// read is a failure with `error` set -- and it is also, by `onerr=succeed`, a
/// file that is refusing nobody, so the caller's job is to say so rather than to
/// guess at what was in it.
QStringList readBlocked(const QString &path, QString *error, bool *missing = nullptr);

/// Writes exactly `users`, one per line, 0644.
///
/// 0644 because PAM reads this file as whoever is logging in -- `pam_listfile`
/// opens it in the process that is authenticating, before that process has
/// become anybody -- and a mode nobody but root can read is `onerr=succeed`
/// letting everybody in for the rest of the day.
///
/// An empty list writes an empty file rather than removing it. The file that is
/// there and empty is a file whose mode and ownership are already right for the
/// next time somebody has to go in it, and removing it would make the ordinary
/// state of the machine indistinguishable from a package that never installed.
bool writeBlocked(const QString &path, const QStringList &users, QString *error);

} // namespace omahouse
