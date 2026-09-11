#pragma once

#include <QString>
#include <QStringList>

namespace omahouse {

// The session's own furniture: what Omarchy starts so that the machine works,
// as opposed to what somebody sat down and opened.
//
// Omarchy's `autostart.lua` brings `udiskie` and `omarchy-hyprland-monitor-watch`
// up through `uwsm-app --`, so they are born as app scopes and are
// indistinguishable, to everything downstream, from a browser the child opened.
// That cost two separate things:
//
//   - Under `default: deny` with no rule naming them they were closed two
//     seconds after login, so every profile had to carry two rules about
//     programs nobody chose to run.
//   - `udiskie` on its own is a live scope, so a session budget -- which matches
//     everything -- ran **from login onwards**, on an idle machine with no
//     window open. The day somebody read was not the day the child spent.
//
// So furniture is not judged and is not evidence. It is never denied, never
// closed, and never on its own makes a `*` budget spend; a budget that names one
// of these by id still counts it, because that is somebody asking for exactly
// this number and it is not this file's business to refuse.
//
// **Unknown is not furniture.** A scope nobody listed is the child's, which is
// the direction that fails safe: counting one thing too many is an afternoon
// that reads long, and letting one thing through is a door nobody chose to open.
//
// The built-in list is what was measured on real Omarchy and nothing else. A
// machine that starts something else says so in `/etc/omahouse/furniture`, one
// name per line, the shape `/etc/omahouse/blocked` already has -- because a
// hard-coded list about somebody else's moving desktop is a list that goes
// wrong quietly.

/// What Omarchy itself starts, measured rather than guessed.
QStringList furnitureOfOmarchy();

/// Whether `scopeId` is session furniture, given whatever the machine added.
///
/// An empty id is never furniture: a scope whose unit name the parser refused
/// has processes in it, and that is what "somebody is using this machine" means.
/// Naming it would be guessing in the one direction that costs something.
bool isFurniture(const QString &scopeId, const QStringList &alsoFurniture = {});

} // namespace omahouse
