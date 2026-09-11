#pragma once

#include <QStringList>

namespace omahouse {

// What *this* machine starts for itself, beyond the two `src/core/Furniture.h`
// was measured with.
//
// The pure half knows the list Omarchy was watched starting. This half knows
// the file an operator edits, because the built-in list is about somebody
// else's desktop and that desktop moves. Read every cycle rather than at
// startup, which is the discipline `/etc/omahouse/blocked` already keeps: a
// line added takes effect on the next tick, and there is nothing to remember to
// restart.
//
// Missing is the ordinary state and means the built-in list stands. Unreadable
// is the same answer, deliberately: this file can only ever *widen* what is
// held out of the count, so failing to read it fails towards counting more, and
// counting one thing too many is an afternoon that reads long rather than a
// door nobody chose to open.

/// The names in `<configDir>/furniture`, blank lines and `#` comments skipped.
QStringList furnitureOfThisMachine();

} // namespace omahouse
