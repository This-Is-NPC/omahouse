#include "Duration.h"

namespace omahouse {

namespace {

/// A day. A daily budget longer than the day it is spent in is a number
/// somebody mistyped -- `--limit 1200` meant twenty hours to nobody -- and it is
/// also what keeps `dailyMinutes * 60` inside an int no matter what is typed.
constexpr int kLongestMinutes = 24 * 60;

bool refuse(const QString &text, const QString &why, QString *error)
{
    if (error)
        *error = QStringLiteral("'%1' is not a length of time: %2").arg(text, why);
    return false;
}

} // namespace

bool minutesFromDuration(const QString &text, int *minutes, QString *error)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return refuse(text, QStringLiteral("it is empty"), error);

    // Every shape in one pass: a run of digits, then the unit it is in, then
    // maybe another pair. `90` is the pair with no unit, and it is minutes
    // because minutes is what a budget is counted in -- docs/design.md §4 writes
    // `dailyMinutes`, and the CLI of §7 writes `45m` beside it.
    qint64 total = 0;
    int index = 0;
    bool sawHours = false;
    bool sawMinutes = false;
    while (index < trimmed.size()) {
        const int digitsFrom = index;
        while (index < trimmed.size() && trimmed.at(index).isDigit())
            ++index;
        if (index == digitsFrom) {
            return refuse(text,
                          QStringLiteral("there is no number in front of '%1'")
                              .arg(trimmed.mid(index)),
                          error);
        }
        bool ok = false;
        const qint64 value = trimmed.mid(digitsFrom, index - digitsFrom).toLongLong(&ok);
        if (!ok || value > kLongestMinutes * 60LL) {
            return refuse(text, QStringLiteral("the number in it is too large for a day"),
                          error);
        }

        const QChar unit = index < trimmed.size() ? trimmed.at(index).toLower() : QChar();
        if (unit.isNull()) {
            // A bare number, and only ever the whole of the string: `1h30`
            // would be somebody who meant `1h30m` and somebody who meant an
            // hour and thirty hours in equal measure, so it is refused instead
            // of guessed at.
            if (sawHours || sawMinutes) {
                return refuse(text, QStringLiteral("it ends in a number with no unit"), error);
            }
            total = value;
            break;
        }
        if (unit == QLatin1Char('h')) {
            if (sawHours || sawMinutes)
                return refuse(text, QStringLiteral("the hours come once, and first"), error);
            sawHours = true;
            total += value * 60;
        } else if (unit == QLatin1Char('m')) {
            if (sawMinutes)
                return refuse(text, QStringLiteral("the minutes come once"), error);
            sawMinutes = true;
            total += value;
        } else if (unit == QLatin1Char('s')) {
            // Not a rounding to make. A budget is counted in minutes on disk,
            // and `30s` would land as either zero minutes or one, neither of
            // which is what was asked for.
            return refuse(text, QStringLiteral("a budget is counted in whole minutes"), error);
        } else {
            return refuse(text, QStringLiteral("'%1' is not h or m").arg(unit), error);
        }
        ++index;
    }

    if (total <= 0) {
        return refuse(text,
                      QStringLiteral("no time at all is not a limit; that is what "
                                     "`omahouse deny` says"),
                      error);
    }
    if (total > kLongestMinutes) {
        return refuse(text,
                      QStringLiteral("a day is %1 minutes, and this is a daily amount")
                          .arg(kLongestMinutes),
                      error);
    }
    if (minutes)
        *minutes = static_cast<int>(total);
    return true;
}

QString durationFromMinutes(int minutes)
{
    if (minutes <= 0)
        return QStringLiteral("0m");
    if (minutes < 60)
        return QStringLiteral("%1m").arg(minutes);
    if (minutes % 60 == 0)
        return QStringLiteral("%1h").arg(minutes / 60);
    return QStringLiteral("%1h%2m").arg(minutes / 60).arg(minutes % 60);
}

} // namespace omahouse
