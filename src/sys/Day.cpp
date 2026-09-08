#include "Day.h"

#include "Paths.h"
#include "Policy.h"

namespace omahouse {

bool readDay(const Profile &profile, const QDate &date, Ledger *out, bool *missing,
             QString *error)
{
    bool absent = false;
    if (!readLedger(paths::ledgerFile(profile.user, date), out, error, &absent))
        return false;
    if (missing)
        *missing = absent;
    if (!absent)
        return true;

    out->user = profile.user;
    out->date = date;

    // Only for a profile that has something to carry. A machine whose budgets
    // all reset never reads a second file, which is most of them, and a day
    // that is missing goes on costing one look.
    bool carries = false;
    for (const Budget &budget : profile.budgets)
        carries = carries || budget.carriesOver();
    if (!carries)
        return true;

    const QString last = paths::lastLedgerFileBefore(profile.user, date);
    if (last.isEmpty())
        return true;

    Ledger previous;
    bool alsoAbsent = false;
    if (!readLedger(last, &previous, error, &alsoAbsent)) {
        // A file that is there and will not parse is not a pot that is empty.
        // Starting a pot from zero on top of a day somebody could still repair
        // is the one failure this whole function exists to prevent, so it is
        // said and not stepped over.
        return false;
    }
    if (alsoAbsent)
        return true;
    *out = carryInto(date, previous, profile);
    return true;
}

} // namespace omahouse
