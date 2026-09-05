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
///
/// **`outOfTime` is the sites a budget has run out on right now**, and it is a
/// parameter rather than something read out of the profiles because it is not a
/// rule -- it is today, and it stops being true tomorrow. Every one of them is
/// blocked and none of them may end up in the allowlist, whatever any profile's
/// rules say about it: a site somebody has spent their thirty minutes on is
/// blocked *because* of the thirty minutes, and a profile that allows it is
/// allowing it in general and not for the thirty-first.
///
/// Nothing here remembers them, and nothing has to remove them. The caller works
/// the list out afresh from today's ledger every time -- which is the same
/// discipline `/etc/omahouse/blocked` keeps in docs/design.md §2 -- so the turn
/// of the day empties it, a grant empties it, `enforce --off` empties it, and
/// removing the profile empties it, with no verb having to know this file
/// exists.
///
/// A machine where the only thing to say is that something ran out still needs a
/// file: `needed()` is true for an `outOfTime` on its own, and the file goes
/// away again the moment nothing is out of time. That is what makes a site limit
/// on a machine with no web rules at all still work, and still leave nothing
/// behind at midnight.
ChromiumPolicy chromiumPolicyFor(const QVector<Profile> &profiles,
                                 const QStringList &outOfTime = {});

/// The reach of this file, in words, written in exactly one place.
///
/// One policy file decides for every account that opens Chromium on the machine,
/// the operator's included, and docs/design.md §11 records that this was weighed
/// and taken rather than overlooked. Everything that owes the sentence prints
/// these lines and composes none of its own -- `omahouse web` when it writes,
/// `omahouse status` under `SITES`, and the studio's sites view -- so the
/// program cannot come to say it two different ways. It is said **once** per run
/// and once per screen, never per rule: a tool that re-argues a settled decision
/// every time it is used is a tool people stop reading.
///
/// Here rather than in the CLI because there are now two front ends that owe it,
/// and a second copy in the window is exactly how the two would drift. Broken
/// into fragments rather than handed over as one paragraph because a terminal
/// wraps by hand and a window wraps by itself: the CLI prints one fragment per
/// line, the studio joins them with spaces and lets the label wrap.
QStringList webPolicyReach();

} // namespace omahouse
