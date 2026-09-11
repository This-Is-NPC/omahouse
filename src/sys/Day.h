#pragma once

#include "Ledger.h"
#include "Profile.h"

#include <QDate>
#include <QString>

namespace omahouse {

/// One day of one person, read off the disk, with what a pot holds carried in.
///
/// The plain read is `readLedger(paths::ledgerFile(user, date))` and it is not
/// enough on its own, because docs/design.md §4 keeps one file per day and the
/// name of the file is the date. Nothing hands the reader yesterday. The first
/// look at a new day meets a file that is not there, calls it a day with
/// nothing spent yet -- true of every daily budget and false of a pot, whose
/// running total is in the last file that touched it and nowhere else.
///
/// So a missing day is not empty until the last day this person has has been
/// looked at. That file is found by name, which is what makes a machine that
/// was off for a week behave the same as one that was off for a night: the last
/// day is the last day, and there is no arithmetic on dates here.
///
/// `missing` is still about *this* date and stays true when the carry found
/// something -- a caller asking whether anything was spent today is asking a
/// different question from what a pot holds, and both answers are wanted.
///
/// **For today, and never for a day in the past.** `report` reads a range and
/// wants each day as it was written; a day it walks past that has no file is a
/// day nobody spent, full stop. Carrying there would put a pot's running total
/// into a day the pot was not touched, and print it.
bool readDay(const Profile &profile, const QDate &date, Ledger *out, bool *missing,
             QString *error);

} // namespace omahouse
