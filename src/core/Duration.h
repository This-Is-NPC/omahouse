#pragma once

#include <QString>

namespace omahouse {

// How long, written the way spec.md §7 writes it on a command line: `45m`,
// `2h`, `1h30m`, and a bare `90` for the minutes it is all counted in.
//
// In the core because it is a pure reading of a string, and it is a reading two
// front ends have to agree on: `--limit 45m` on the command line and the number
// the studio of stage 8 sends through `pkexec` must mean the same thing.

/// Minutes from a duration, or false with a sentence in `error`.
///
/// Anything it does not understand is refused rather than taken for minutes.
/// Silently reading `2h` as two minutes would hand somebody a two minute budget
/// that they will not find out about until their session closes at nine in the
/// morning; refusing costs them one retyped word.
///
/// Zero is refused too. `Budget::hasLimit` reads a zero as "no limit at all",
/// so a budget of zero minutes would say the opposite of what somebody typing
/// `--limit 0m` clearly meant, and what they meant is `omahouse deny`.
bool minutesFromDuration(const QString &text, int *minutes, QString *error);

/// A number of minutes as the CLI says it back: `45m`, `2h`, `1h30m`. The
/// inverse of the above for everything the above accepts, which is what lets a
/// profile be read back in the same words it was written in.
QString durationFromMinutes(int minutes);

} // namespace omahouse
