#include <QtTest>

#include "Duration.h"

using namespace omahouse;

// How long, written the way docs/design.md §7 writes it on a command line.
//
// The refusals matter more than the readings. A parser that takes what it does
// not understand for minutes turns `--limit 2h` into a two minute budget, and
// nobody finds out until a session closes at nine in the morning; a parser that
// refuses costs one retyped word. So every shape below that is not one of the
// four the spec writes is a refusal, and each one says which.
class DurationTest : public QObject {
    Q_OBJECT

private slots:
    void readsTheFourShapesTheSpecWrites_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("minutes");

        QTest::newRow("hours") << QStringLiteral("2h") << 120;
        QTest::newRow("minutes") << QStringLiteral("45m") << 45;
        // A bare number is minutes, because minutes is what a budget is counted
        // in: docs/design.md §4 writes `dailyMinutes` and §7 writes `45m` beside it.
        QTest::newRow("a bare number") << QStringLiteral("90") << 90;
        QTest::newRow("both") << QStringLiteral("1h30m") << 90;
        QTest::newRow("upper case") << QStringLiteral("1H30M") << 90;
        QTest::newRow("space around it") << QStringLiteral("  45m  ") << 45;
        QTest::newRow("an hour of minutes") << QStringLiteral("90m") << 90;
        QTest::newRow("one minute") << QStringLiteral("1m") << 1;
        QTest::newRow("the whole day") << QStringLiteral("24h") << 1440;
    }

    void readsTheFourShapesTheSpecWrites()
    {
        QFETCH(QString, text);
        QFETCH(int, minutes);
        int read = 0;
        QString error;
        QVERIFY2(minutesFromDuration(text, &read, &error), qPrintable(error));
        QCOMPARE(read, minutes);
        QVERIFY(error.isEmpty());
    }

    void refusesWhatItDoesNotUnderstand_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("because");

        QTest::newRow("empty") << QString() << QStringLiteral("empty");
        QTest::newRow("only spaces") << QStringLiteral("   ") << QStringLiteral("empty");
        QTest::newRow("a word") << QStringLiteral("forever") << QStringLiteral("no number");
        QTest::newRow("a unit with no number")
            << QStringLiteral("m") << QStringLiteral("no number");
        // Somebody who meant `1h30m` and somebody who meant an hour and thirty
        // hours, in equal measure.
        QTest::newRow("a number with no unit at the end")
            << QStringLiteral("1h30") << QStringLiteral("no unit");
        QTest::newRow("the wrong unit") << QStringLiteral("3d") << QStringLiteral("not h or m");
        // A budget is whole minutes on disk, so seconds would land as zero or as
        // one and neither is what was asked for.
        QTest::newRow("seconds") << QStringLiteral("30s") << QStringLiteral("whole minutes");
        QTest::newRow("a fraction") << QStringLiteral("1.5h") << QStringLiteral("not h or m");
        QTest::newRow("minutes before hours")
            << QStringLiteral("30m1h") << QStringLiteral("come once, and first");
        QTest::newRow("hours twice")
            << QStringLiteral("1h1h") << QStringLiteral("come once, and first");
        QTest::newRow("negative") << QStringLiteral("-45m") << QStringLiteral("no number");
        // `Budget::hasLimit` reads a zero as no limit at all, so a budget of zero
        // minutes would say the opposite of what was typed. What was meant is a
        // verb of its own.
        QTest::newRow("zero") << QStringLiteral("0") << QStringLiteral("omahouse deny");
        QTest::newRow("zero minutes") << QStringLiteral("0m") << QStringLiteral("omahouse deny");
        // A daily amount longer than the day it is spent in.
        QTest::newRow("longer than a day")
            << QStringLiteral("25h") << QStringLiteral("a day is 1440 minutes");
        QTest::newRow("longer than a day in minutes")
            << QStringLiteral("1441") << QStringLiteral("a day is 1440 minutes");
        QTest::newRow("more than a number can hold")
            << QStringLiteral("999999999999999999999") << QStringLiteral("too large");
    }

    void refusesWhatItDoesNotUnderstand()
    {
        QFETCH(QString, text);
        QFETCH(QString, because);
        int read = -1;
        QString error;
        QVERIFY2(!minutesFromDuration(text, &read, &error), qPrintable(text));
        QVERIFY2(error.contains(because), qPrintable(error));
        // Untouched, so that a caller that ignored the answer writes nothing
        // rather than writing a zero.
        QCOMPARE(read, -1);
    }

    // The inverse, for everything the reading accepts: a limit written by the
    // CLI has to read back in the words it was typed in.
    void saysANumberOfMinutesBackTheWayItWasTyped_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("minutes") << QStringLiteral("45m");
        QTest::newRow("hours") << QStringLiteral("2h");
        QTest::newRow("both") << QStringLiteral("1h30m");
        QTest::newRow("one minute") << QStringLiteral("1m");
        QTest::newRow("a day") << QStringLiteral("24h");
    }

    void saysANumberOfMinutesBackTheWayItWasTyped()
    {
        QFETCH(QString, text);
        int minutes = 0;
        QVERIFY(minutesFromDuration(text, &minutes, nullptr));
        QCOMPARE(durationFromMinutes(minutes), text);
    }

    void refusesToWriteATimeThatIsNoTime()
    {
        // Not a round trip: nothing can be typed that reads as zero. It is the
        // answer for a budget with no limit, and the caller that has one prints
        // a dash instead.
        QCOMPARE(durationFromMinutes(0), QStringLiteral("0m"));
        QCOMPARE(durationFromMinutes(-5), QStringLiteral("0m"));
    }
};

int runDurationTests(int argc, char **argv)
{
    DurationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_duration.moc"
