#pragma once

#include "Profile.h"
#include "Ledger.h"
#include <QJsonObject>

namespace omahouse {

/// What has been handed over to `budget` that the household may count.
///
/// Today's operator credit, and for a budget that never resets the nights'
/// worth folded in beside it. A `leave` adjustment is in neither: it is a
/// machine correcting its own balance and must not feed back into the
/// household's -- and it cannot exist on a pot at all, which is refused where
/// `leave` is typed.
///
/// It is one function because three callers must agree about it or the
/// household says two different things: the plan builds `credit` out of it, the
/// same plan tells each machine how much of that credit came from itself, and
/// the machine adds whatever it has handed over since.
int givenTo(const Budget &budget, const Ledger &ledger);

/// What has been spent against `budget`, out of whichever of the ledger's two
/// counters belongs to it.
///
/// A ledger keeps daily spending and a pot's running total in two maps, for the
/// reason `Ledger::keptSeconds` gives: everything that adds days or machines
/// together reads the first, and a pot carried into every day it touches would
/// be counted once per day there. The cost of that split is that *which* map a
/// budget is in is a question with an answer, and asking it in four places got
/// three of them right. `status` and the window were the fourth: they read the
/// daily map for every budget alike, so a pot showed nothing spent and its
/// whole limit left, every day, however much of it had gone.
///
/// So it is asked here, once, beside the function that works out the other side
/// of the same subtraction.
int spentSeconds(const Budget &budget, const Ledger &ledger);

// A machine owns an absolute portion of the daily allowance. Replaying a
// document never tops up remaining time. Missing/tomorrow's portions are zero.
int allowanceSeconds(const Profile &profile, const Budget &budget,
                     const Ledger &ledger, const QDate &date);
bool validAllocation(const QJsonObject &value, QString *error);
bool applyAllocation(Profile *profile, const QJsonObject &document,
                     const QDate &today, QString *error);

// Reserve before delivery. Membership is frozen for the day and no allocation
// is reclaimed from an offline machine. Extra credit is divided equally.
bool planAllocations(const Profile &profile,
                     const QVector<QPair<QString, Ledger>> &days,
                     const QJsonObject &previous, const QDateTime &now,
                     QJsonObject *plan, QString *error);
}
