#include <QtTest>

#include "Focus.h"
#include "FocusFile.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using namespace omahouse;

namespace {

/// A moment to measure everything else against. Made up on the spot, because
/// nothing in `src/core/Focus.cpp` reads a clock -- that is the whole reason the
/// parsing is over there and the file is over here.
QDateTime noon()
{
    return QDateTime(QDate(2026, 9, 4), QTime(12, 0, 0));
}

QByteArray lineAt(const QDateTime &when, const QByteArray &site)
{
    return QByteArray::number(when.toSecsSinceEpoch()) + ' ' + site + '\n';
}

class FocusTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_tree.isValid());
        m_root = m_tree.path();
    }

    void cleanup()
    {
        qunsetenv("OMAHOUSE_RUNTIME_ROOT");
        QDir(m_root).removeRecursively();
        QDir().mkpath(m_root);
    }

    // -- the registrable domain ----------------------------------------------

    void reducesAHostToTheNameAHouseholdWouldSay_data()
    {
        QTest::addColumn<QString>("host");
        QTest::addColumn<QString>("expected");

        QTest::newRow("already there") << "youtube.com" << "youtube.com";
        QTest::newRow("a subdomain") << "www.youtube.com" << "youtube.com";
        QTest::newRow("two of them") << "m.music.youtube.com" << "youtube.com";
        QTest::newRow("upper case") << "WWW.YouTube.COM" << "youtube.com";
        // The absolute form of the same name. Two rows in a day's report for one
        // site is the failure this line exists to prevent.
        QTest::newRow("a trailing dot") << "www.youtube.com." << "youtube.com";
        QTest::newRow("a Brazilian suffix") << "www.uol.com.br" << "uol.com.br";
        QTest::newRow("a British one") << "www.bbc.co.uk" << "bbc.co.uk";
        QTest::newRow("the suffix on its own") << "co.uk" << "co.uk";
        // Not in the table, and reduced to two labels. The cost is a row with an
        // odd name in it and never a minute in the wrong place, which is the
        // trade `Focus.h` says it is taking.
        QTest::newRow("a suffix nobody listed") << "www.example.co.kr" << "co.kr";
        QTest::newRow("an address") << "192.168.1.10" << "192.168.1.10";
        QTest::newRow("a bracketed address") << "[::1]" << "[::1]";
        QTest::newRow("one word") << "localhost" << "localhost";
        QTest::newRow("nothing") << "" << "";
    }

    void reducesAHostToTheNameAHouseholdWouldSay()
    {
        QFETCH(QString, host);
        QFETCH(QString, expected);
        QCOMPARE(registrableDomain(host), expected);
    }

    // What may become a key in somebody's day. Without this the person being
    // measured chooses what the rows of their own report are called, and a row
    // can be a sentence.
    void refusesAnythingThatIsNotShapedLikeADomain_data()
    {
        QTest::addColumn<QString>("candidate");
        QTest::addColumn<bool>("plausible");

        QTest::newRow("an ordinary one") << "youtube.com" << true;
        QTest::newRow("a hyphen inside") << "my-site.com" << true;
        QTest::newRow("digits") << "3com.com" << true;
        QTest::newRow("nothing") << "" << false;
        QTest::newRow("no dot") << "localhost" << false;
        QTest::newRow("an empty label") << "a..com" << false;
        QTest::newRow("a leading dot") << ".com" << false;
        QTest::newRow("a leading hyphen") << "-bad.com" << false;
        QTest::newRow("a trailing hyphen") << "bad-.com" << false;
        QTest::newRow("a space") << "you tube.com" << false;
        QTest::newRow("a slash") << "youtube.com/watch" << false;
        QTest::newRow("an underscore") << "you_tube.com" << false;
        QTest::newRow("a colon") << "youtube.com:443" << false;
        QTest::newRow("upper case, unreduced") << "YouTube.com" << false;
        QTest::newRow("a label of sixty-four")
            << (QString(64, QLatin1Char('a')) + QStringLiteral(".com")) << false;
        QTest::newRow("longer than a name may be")
            << (QString(120, QLatin1Char('a')) + QLatin1Char('.')
                + QString(200, QLatin1Char('b'))) << false;
    }

    void refusesAnythingThatIsNotShapedLikeADomain()
    {
        QFETCH(QString, candidate);
        QFETCH(bool, plausible);
        QCOMPARE(isPlausibleDomain(candidate), plausible);
    }

    // -- one line ------------------------------------------------------------

    void readsTheLineTheHostWrites()
    {
        const FocusLine line = parseFocusLine(lineAt(noon(), "www.youtube.com").trimmed());
        QVERIFY(line.read);
        QCOMPARE(line.site, QStringLiteral("youtube.com"));
        QCOMPARE(line.at, noon());
    }

    // `-` is the browser saying there is no site in front, and it is a thing it
    // says rather than a thing it withholds. A read line with no site in it, and
    // that is not the same as a line that would not read.
    void readsTheDashAsNothingInFrontAndNotAsRubbish()
    {
        const FocusLine line = parseFocusLine(lineAt(noon(), "-").trimmed());
        QVERIFY(line.read);
        QVERIFY(line.site.isEmpty());
        QCOMPARE(line.at, noon());
    }

    void refusesEveryWayALineCanBeWrong_data()
    {
        QTest::addColumn<QByteArray>("line");

        QTest::newRow("nothing") << QByteArray();
        QTest::newRow("only a space") << QByteArray(" ");
        QTest::newRow("no stamp") << QByteArray("youtube.com");
        QTest::newRow("no site") << QByteArray("1757000000");
        QTest::newRow("no site after the space") << QByteArray("1757000000 ");
        QTest::newRow("a stamp that is a word") << QByteArray("lunchtime youtube.com");
        QTest::newRow("a stamp with a sign") << QByteArray("-1757000000 youtube.com");
        QTest::newRow("a stamp that is a fraction") << QByteArray("1757000000.5 youtube.com");
        QTest::newRow("the epoch itself") << QByteArray("0 youtube.com");
        // Not a clock that is wrong: somebody trying the parser. The answer is
        // the same as the answer to a letter.
        QTest::newRow("a stamp past the year 2100") << QByteArray("99999999999 youtube.com");
        QTest::newRow("a stamp longer than a number")
            << QByteArray("999999999999999999999999 youtube.com");
        QTest::newRow("three fields") << QByteArray("1757000000 youtube.com extra");
        QTest::newRow("a site with a path") << QByteArray("1757000000 youtube.com/watch?v=x");
        QTest::newRow("a site that is a word") << QByteArray("1757000000 localhost");
        // A name that would print as an escape sequence in whoever's terminal ran
        // `report`. Refused here rather than repaired at the far end.
        QTest::newRow("an escape") << QByteArray("1757000000 you\033[2Jtube.com");
        QTest::newRow("a tab") << QByteArray("1757000000 you\ttube.com");
        QTest::newRow("a byte above ASCII")
            << QByteArray("1757000000 youtube\xc3\xa9.com");
        QTest::newRow("longer than a line may be")
            << (QByteArray("1757000000 ") + QByteArray(400, 'a') + ".com");
        QTest::newRow("a whole JSON document")
            << QByteArray("{\"site\":\"youtube.com\",\"t\":1757000000}");
    }

    void refusesEveryWayALineCanBeWrong()
    {
        QFETCH(QByteArray, line);
        QVERIFY(!parseFocusLine(line).read);
    }

    // -- the last line of a blob ---------------------------------------------

    void takesTheLastCompleteLineAndNotThePartialOne()
    {
        // The tail of a file may begin in the middle of a line, and the host may
        // be in the middle of writing the next one. Neither is looked at.
        QByteArray blob = "utube.com\n";
        blob += lineAt(noon(), "wikipedia.org");
        blob += QByteArray::number(noon().toSecsSinceEpoch()) + " half-writ";
        const FocusLine line = lastFocusLine(blob);
        QVERIFY(line.read);
        QCOMPARE(line.site, QStringLiteral("wikipedia.org"));
    }

    void readsAFileOfExactlyOneLine()
    {
        const FocusLine line = lastFocusLine(lineAt(noon(), "wikipedia.org"));
        QVERIFY(line.read);
        QCOMPARE(line.site, QStringLiteral("wikipedia.org"));
    }

    void hasNothingToSayAboutABlobWithNoWholeLineInIt()
    {
        QVERIFY(!lastFocusLine(QByteArray("1757000000 youtube.com")).read);
        QVERIFY(!lastFocusLine(QByteArray()).read);
    }

    // The strict reading, and the reason for it. Scanning backwards for
    // something usable is how a file full of rubbish still bills a site: the
    // person being measured writes one good line and then whatever she likes,
    // and a forgiving reader finds the good one for ever.
    void looksAtTheLastLineOnlyAndNotTheLastUsableOne()
    {
        QByteArray blob = lineAt(noon(), "youtube.com");
        blob += "not a line at all\n";
        QVERIFY(!lastFocusLine(blob).read);
    }

    // -- what to bill --------------------------------------------------------

    void billsAFreshLine()
    {
        const QByteArray blob = lineAt(noon().addSecs(-4), "www.youtube.com");
        QCOMPARE(siteInFrontOf(blob, noon()), QStringLiteral("youtube.com"));
    }

    // The browser closed at 19:00 and must not go on being billed until 19:15.
    // The extension repeats itself every five seconds, so a line older than
    // fifteen is two beats gone.
    void billsNothingForALineThatWentStale()
    {
        QVERIFY(siteInFrontOf(lineAt(noon().addSecs(-16), "youtube.com"), noon()).isEmpty());
        QCOMPARE(siteInFrontOf(lineAt(noon().addSecs(-15), "youtube.com"), noon()),
                 QStringLiteral("youtube.com"));
    }

    // A little ahead of the clock is two unsynchronised processes. A lot of it is
    // somebody having written the file by hand to buy a quarter of an hour.
    void billsNothingForAStampInTheFuture()
    {
        QCOMPARE(siteInFrontOf(lineAt(noon().addSecs(2), "youtube.com"), noon()),
                 QStringLiteral("youtube.com"));
        QVERIFY(siteInFrontOf(lineAt(noon().addSecs(600), "youtube.com"), noon()).isEmpty());
    }

    void billsNothingWhenTheBrowserSaysThereIsNothingInFront()
    {
        QVERIFY(siteInFrontOf(lineAt(noon(), "-"), noon()).isEmpty());
    }

    void billsNothingForRubbishAndNothingForAnEmptyFile()
    {
        QVERIFY(siteInFrontOf(QByteArray("nonsense\n"), noon()).isEmpty());
        QVERIFY(siteInFrontOf(QByteArray(), noon()).isEmpty());
    }

    // The writer and the reader are two functions in one file so that they cannot
    // come to disagree about the format. This is the assertion that says so.
    void whatTheHostWritesIsWhatTheDaemonReads()
    {
        QCOMPARE(siteInFrontOf(focusLineFor(noon(), QStringLiteral("youtube.com")), noon()),
                 QStringLiteral("youtube.com"));
        QCOMPARE(focusLineFor(noon(), QString()).right(2), QByteArray("-\n"));
    }

    // -- the file on the machine ---------------------------------------------

    void thePathIsUnderTheUsersOwnRuntimeDirectory()
    {
        QCOMPARE(focusFileFor(QStringLiteral("/run/user"), 1001),
                 QStringLiteral("/run/user/1001/omahouse/focus"));
        QCOMPARE(systemRuntimeRoot(), QStringLiteral("/run/user"));
        QVERIFY(runtimeRootIsTheSystems());
        qputenv("OMAHOUSE_RUNTIME_ROOT", QFile::encodeName(m_root));
        QCOMPARE(runtimeRoot(), m_root);
        QVERIFY(!runtimeRootIsTheSystems());
    }

    void theHostWritesAndTheSourceReadsBack()
    {
        const QString path = focusFileFor(m_root, ::getuid());
        QString error;
        QVERIFY2(appendFocusLine(path, noon().addSecs(-2), QStringLiteral("youtube.com"),
                                 &error),
                 qPrintable(error));
        QVERIFY(appendFocusLine(path, noon(), QStringLiteral("wikipedia.org"), &error));

        FileFocusSource source(m_root);
        QCOMPARE(siteInFrontOf(source.tail(::getuid()), noon()),
                 QStringLiteral("wikipedia.org"));

        // 0700 on the directory: the file names the sites somebody visited, so it
        // is theirs and root's and nobody else's on the machine.
        const QFile::Permissions mode = QFile(focusDirFor(m_root, ::getuid())).permissions();
        QVERIFY(!(mode & QFile::ReadGroup));
        QVERIFY(!(mode & QFile::ReadOther));
    }

    void aUserWithNoFileHasNothingToSay()
    {
        FileFocusSource source(m_root);
        QVERIFY(source.tail(::getuid()).isEmpty());
        // And a directory that is there with no file in it, which is what a
        // browser that has never been opened leaves.
        QVERIFY(QDir().mkpath(focusDirFor(m_root, ::getuid())));
        QVERIFY(source.tail(::getuid()).isEmpty());
    }

    // The child owns the directory, so she can put a link there pointing at
    // whatever she would like root to read. `O_NOFOLLOW` on both components is
    // what makes that nothing rather than a file somebody else's.
    void refusesToFollowALinkTheUserPutThere()
    {
        const QString elsewhere = m_root + QStringLiteral("/secret");
        QFile bait(elsewhere);
        QVERIFY(bait.open(QIODevice::WriteOnly));
        bait.write(lineAt(noon(), "youtube.com"));
        bait.close();

        const QString directory = focusDirFor(m_root, ::getuid());
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(QFile::link(elsewhere, directory + QStringLiteral("/focus")));

        FileFocusSource source(m_root);
        QVERIFY(source.tail(::getuid()).isEmpty());
    }

    void refusesToFollowALinkedDirectoryEither()
    {
        const QString elsewhere = m_root + QStringLiteral("/elsewhere");
        QVERIFY(QDir().mkpath(elsewhere));
        QFile bait(elsewhere + QStringLiteral("/focus"));
        QVERIFY(bait.open(QIODevice::WriteOnly));
        bait.write(lineAt(noon(), "youtube.com"));
        bait.close();

        QVERIFY(QDir().mkpath(m_root + QLatin1Char('/')
                              + QString::number(static_cast<qulonglong>(::getuid()))));
        QVERIFY(QFile::link(elsewhere, focusDirFor(m_root, ::getuid())));

        FileFocusSource source(m_root);
        QVERIFY(source.tail(::getuid()).isEmpty());
    }

    // A fifo in place of the file would hold the whole cycle open waiting for a
    // writer that is never coming, and everybody else's day would stop being
    // counted. `O_NONBLOCK` on the open and `S_ISREG` after it.
    void refusesAFifoRatherThanWaitingOnIt()
    {
        const QString directory = focusDirFor(m_root, ::getuid());
        QVERIFY(QDir().mkpath(directory));
        const QByteArray path = QFile::encodeName(directory + QStringLiteral("/focus"));
        QCOMPARE(::mkfifo(path.constData(), 0600), 0);

        FileFocusSource source(m_root);
        QVERIFY(source.tail(::getuid()).isEmpty());
    }

    // A file grown to fill the tmpfs is a bound on what root reads per tick, not
    // a reason to read all of it.
    void readsABoundedTailOfAFileHoweverBigItIs()
    {
        const QString path = focusFileFor(m_root, ::getuid());
        QVERIFY(QDir().mkpath(focusDirFor(m_root, ::getuid())));
        QFile handle(path);
        QVERIFY(handle.open(QIODevice::WriteOnly));
        handle.write(QByteArray(200 * 1024, 'x'));
        handle.write("\n");
        handle.write(lineAt(noon(), "youtube.com"));
        handle.close();

        FileFocusSource source(m_root);
        const QByteArray tail = source.tail(::getuid());
        QVERIFY(tail.size() <= kFocusTailBytes);
        QCOMPARE(siteInFrontOf(tail, noon()), QStringLiteral("youtube.com"));
    }

    // Nothing reads more than the last line, so there is no history here to keep
    // and no reason to pay for keeping it. A file that grows without a ceiling in
    // a tmpfs is a session that eventually cannot write anything at all.
    void theHostStartsTheFileAgainRatherThanLettingItGrow()
    {
        const QString path = focusFileFor(m_root, ::getuid());
        QVERIFY(QDir().mkpath(focusDirFor(m_root, ::getuid())));
        QFile handle(path);
        QVERIFY(handle.open(QIODevice::WriteOnly));
        handle.write(QByteArray(kFocusMostBytes + 1024, '\n'));
        handle.close();

        QString error;
        QVERIFY2(appendFocusLine(path, noon(), QStringLiteral("youtube.com"), &error),
                 qPrintable(error));
        QVERIFY(QFileInfo(path).size() < kFocusMostBytes);
        FileFocusSource source(m_root);
        QCOMPARE(siteInFrontOf(source.tail(::getuid()), noon()),
                 QStringLiteral("youtube.com"));
    }

private:
    QTemporaryDir m_tree;
    QString m_root;
};

} // namespace

int runFocusTests(int argc, char **argv)
{
    FocusTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_focus.moc"
