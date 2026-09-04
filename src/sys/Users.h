#pragma once

#include <QString>

#include <sys/types.h>

namespace omahouse {

// The account table, which is the machine's and not the model's.
//
// A profile names a user by name because that is what an operator types and what
// `/etc/omahouse/profiles.json` has to still mean after a reinstall. The cgroup
// tree is laid out by uid. This is the one hop between the two, and it is here
// rather than in `src/core` because it reads NSS.

/// The uid of `user`. False is "the system has no such account", which is a
/// thing to report and not a thing to fail on: a profile can outlive the account
/// it was written for.
bool uidForUser(const QString &user, uid_t *uid);

/// The name of `uid`, or an empty string. Used for the header of a `status` that
/// was asked about nobody in particular.
QString userForUid(uid_t uid);

/// Whoever is running this. The real uid rather than `$USER`, which is a
/// variable a shell sets and an `su` does not always update.
QString currentUser();

} // namespace omahouse
