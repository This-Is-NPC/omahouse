#pragma once

#include "Profile.h"

#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace omahouse {

// The managed policy a Chromium reads, worked out from the profiles and nothing
// else -- docs/design.md §11.
//
// Pure, and in `core` for the same reason `Policy::evaluate` is: the interesting
// part is the composition of profiles that disagree, and that is arithmetic over
// a list of structures. It needs no disk, no root and no browser to be proved,
// and the thing that really writes the file lives in `src/sys/Chromium.h` on the
// other side of the line the gate checks.
//
// **The policy is per machine, and this is the whole of why.** Chromium's policy
// directory is a compile-time constant -- `docs/proposal-browser.md` §3.1 reads
// it out of `policy_paths.cc` -- and there is no per-account version of it short
// of a managed cloud account. So one file decides for every account that opens
// that browser, including the operator's. That was weighed and accepted rather
// than worked around: §11 says so once, `omahouse web` says so when it writes,
// and `omahouse status` says so when it prints. Nothing else in the tree
// pretends otherwise.

/// What the file holds, before anybody has decided where the file is.
///
/// The three keys are the whole of this slice. `URLBlocklist` and `URLAllowlist`
/// are Chromium's own pair, and the allowlist is only ever an exception carved
/// out of the blocklist: on its own it blocks nothing at all. That is not a
/// guess -- `.temp/spike-extension.md` §7 measured the same trap one policy
/// over, where a `NativeMessagingAllowlist` with no blocklist beside it let
/// through exactly the host it was supposed to keep out.
struct ChromiumPolicy {
    /// `*` first when everything is blocked, then the named domains, sorted.
    /// Sorted and not in the order somebody typed them, because this file is
    /// compared with what is already on disk to decide whether to write at all,
    /// and two orderings of one decision would rewrite it forever.
    QStringList blocklist;
    QStringList allowlist;
    /// `IncognitoModeAvailability: 1`. False means the key is left out
    /// altogether rather than written as `0`: see the note on `Web`.
    bool incognitoDenied = false;

    /// Whether there should be a file on the machine at all.
    ///
    /// False is not "write an empty policy" -- it is "take the file away". A
    /// managed policy file that exists and says nothing is indistinguishable, to
    /// anybody looking at the machine, from one that is about to say something,
    /// and docs/design.md §11 makes taking the last rule away and removing the
    /// package land in the same place.
    bool needed() const
    {
        return !blocklist.isEmpty() || !allowlist.isEmpty() || incognitoDenied;
    }

    QJsonObject toJson() const;
    bool operator==(const ChromiumPolicy &other) const
    {
        return blocklist == other.blocklist && allowlist == other.allowlist
            && incognitoDenied == other.incognitoDenied;
    }
};

/// The one policy that serves every profile at once.
///
/// **How profiles that disagree are composed: the most restrictive wins, and
/// there is no precedence.** A domain is allowed on this machine only if every
/// profile that has web rules allows it; one profile blocking it blocks it for
/// all of them, and one profile's `--only-listed` puts `*` in the blocklist for
/// all of them. Incognito is switched off if any profile asks for it off.
///
/// Inventing a precedence -- first profile wins, most specific wins, the child's
/// beats the adult's -- would mean a rule an operator wrote and can see in
/// `omahouse profile show` silently not happening, and there is no reading of
/// that file that would tell them why. Being more restrictive than one profile
/// asked for is visible the moment somebody opens the site, and `omahouse web`
/// prints who else has a say. So the direction of the surprise is chosen: it
/// lands where somebody will notice it, not where they will not.
///
/// A profile with `enabled: false` is not consulted. Switching a profile off has
/// to switch off what it does to the machine, or "disabled" would be a word for
/// something still in force.
ChromiumPolicy chromiumPolicyFor(const QVector<Profile> &profiles);

} // namespace omahouse
