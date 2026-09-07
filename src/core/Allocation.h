#pragma once

#include "Profile.h"
#include "Ledger.h"
#include <QJsonObject>

namespace omahouse {

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
