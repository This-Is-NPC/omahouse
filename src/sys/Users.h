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

/// Whoever asked for this, which is not always whoever is running it. Under
/// `pkexec` the process is root and the person is not, and polkit says who they
/// were in `$PKEXEC_UID`. That is the name a grant is signed with, because "root
/// gave kid ten minutes" is not the line an operator wants to read back in a
/// month.
QString operatorUser();

/// Whether this process can write /etc and /var, which is the euid and not the
/// uid: `pkexec` hands over an effective root and nothing else.
bool runningAsRoot();

/// Whether `user` administers this machine, and in `why` the reason in words --
/// `is root`, `is in wheel`.
///
/// docs/design.md §1 makes the operator "whoever is in wheel", so a profile for one of
/// them is a person fiscalising themselves by accident, which `profile add`
/// refuses. Both halves of the group are looked at: the primary gid and the
/// member list, because `usermod -aG wheel` and a fresh account with wheel as
/// its own group are the same fact written two ways.
bool isAdministrator(const QString &user, QString *why = nullptr);

/// The program `createAccount` runs: `/usr/sbin/useradd`, or
/// `$OMAHOUSE_USERADD`.
///
/// The variable is the whole reason the account can be created by a verb that is
/// never allowed to create one here. plan.md says it in as many words:
/// `useradd` is irreversible enough never to be exercised outside the VM, so
/// stage 5 writes the verb and stage 7 is what runs it in the box. The suite
/// points the variable at a script that records the call, which asserts that the
/// right command is built without a new account appearing on the machine that
/// built it.
QString useraddProgram();

/// `useradd -m <user>`, and nothing else -- docs/design.md §7. False with the sentence
/// the command printed, or with why it could not be started.
bool createAccount(const QString &user, QString *error);

} // namespace omahouse
