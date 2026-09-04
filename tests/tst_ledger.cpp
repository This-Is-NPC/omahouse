#include <QtTest>

#include "Json.h"
#include "Ledger.h"
#include "Policy.h"
#include "Profile.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTemporaryDir>

#include <sys/stat.h>

using namespace omahouse;

namespace {

// The profiles.json of docs/design.md §4, copied whole. The test reads the spec's own
// bytes rather than the ones this code happens to write, because a round trip
// through a writer and a reader that agree with each other and with nothing else
// is a round trip that proves nothing about the file on disk.
const char *kSpecProfiles = R"({
  "schemaVersion": 1,
  "profiles": [
    {
      "user": "julia",
      "displayName": "Júlia",
      "enabled": true,
      "enforce": true,
      "default": "deny",
      "warnAt": [10, 5, 1],
      "grace": 20,
      "rules": [
        { "match": "minecraft-launcher", "verdict": "allow" },
        { "match": "firefox",            "verdict": "allow" },
        { "match": "gcompris",           "verdict": "allow" }
      ],
      "budgets": [
        { "id": "session",   "match": "*",                   "dailyMinutes": 120, "onExhausted": "logout" },
        { "id": "minecraft", "match": "minecraft-launcher",  "dailyMinutes": 45,  "onExhausted": "close" },
        { "id": "firefox",   "match": "firefox",             "dailyMinutes": 60,  "onExhausted": "close" }
      ]
    }
  ]
})";

const char *kSpecLedger = R"({
  "schemaVersion": 1,
  "user": "julia",
  "date": "2026-09-03",
  "budgets": { "session": 4210, "minecraft": 2400, "firefox": 1830 },
  "grants": [
    { "at": "2026-09-03T19:12:04-03:00", "by": "howl", "budget": "session", "minutes": 10 }
  ],
  "events": [
    { "at": "2026-09-03T19:30:11-03:00", "kind": "exhausted", "budget": "minecraft" }
  ]
})";

QJsonObject parse(const char *text)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(text), &error);
    Q_ASSERT(error.error == QJsonParseError::NoError);
    return document.object();
}

AppScope scope(const QString &id, const QString &unit, int pidCount)
{
    AppScope entry;
    entry.id = id;
    entry.unit = unit;
    entry.pidCount = pidCount;
    return entry;
}

QDateTime at(int hour, int minute, int second = 0)
{
    return QDateTime(QDate(2026, 9, 3), QTime(hour, minute, second));
}

uint inodeOf(const QString &path)
{
    struct stat information = {};
    if (::stat(QFile::encodeName(path).constData(), &information) != 0)
        return 0;
    return static_cast<uint>(information.st_ino);
}

} // namespace

// The day's accounting: what gets debited, when the day starts over, and the two
// files of docs/design.md §4 going out to disk and coming back.
class LedgerTest : public QObject {
    Q_OBJECT

private slots:
    // The rule that keeps a two hour session from evaporating in six minutes.
    // The PoC counted 21 processes in one Chromium scope and 13 in one VS Code
    // scope; a tick that debited per process would spend 34 seconds of somebody's
    // afternoon every two seconds.
    void debitsOncePerBudgetAndNotOncePerProcess()
    {
        Profile profile;
        profile.user = QStringLiteral("julia");
        profile.enforce = true;

        Budget session;
        session.id = QStringLiteral("session");
        session.match = QStringLiteral("*");
        Budget chromium;
        chromium.id = QStringLiteral("chromium");
        chromium.match = QStringLiteral("chromium");
        profile.budgets = {session, chromium};

        // One Chromium window with its twenty one processes, a second Chromium
        // scope, and an editor: three scopes, thirty five processes, two
        // budgets, one tick.
        const QVector<AppScope> scopes = {
            scope(QStringLiteral("chromium"),
                  QStringLiteral("app-Hyprland-chromium-031bdc27.scope"), 21),
            scope(QStringLiteral("chromium"),
                  QStringLiteral("app-Hyprland-chromium-4c1de881.scope"), 1),
            scope(QStringLiteral("code"), QStringLiteral("app-code-3579042.scope"), 13),
        };

        Ledger ledger;
        ledger.user = QStringLiteral("julia");
        ledger.date = QDate(2026, 9, 3);

        const Outcome outcome = evaluate(profile, scopes, ledger, at(19, 0), 2);

        QCOMPARE(outcome.ledger.secondsFor(QStringLiteral("session")), 2);
        QCOMPARE(outcome.ledger.secondsFor(QStringLiteral("chromium")), 2);

        // And a scope on its way out is not somebody's app running.
        const Outcome dying =
            evaluate(profile,
                     {scope(QStringLiteral("code"), QStringLiteral("app-code-3579042.scope"), 0)},
                     outcome.ledger, at(19, 0, 2), 2);
        QCOMPARE(dying.ledger.secondsFor(QStringLiteral("session")), 2);
    }

    // Midnight. docs/design.md §4 keeps one file per day, so a ledger from yesterday is
    // a balance of nothing today -- and the session that was open across the turn
    // just carries on debiting the new one.
    void startsTheDayOverWhenTheDateTurns()
    {
        Profile profile;
        profile.user = QStringLiteral("julia");
        profile.enforce = true;
        Budget session;
        session.id = QStringLiteral("session");
        session.match = QStringLiteral("*");
        session.dailyMinutes = 120;
        session.onExhausted = OnExhausted::Logout;
        profile.budgets = {session};

        Ledger yesterday;
        yesterday.user = QStringLiteral("julia");
        yesterday.date = QDate(2026, 9, 2);
        yesterday.seconds.insert(QStringLiteral("session"), 120 * 60);
        yesterday.grants.append(Grant{QDateTime(QDate(2026, 9, 2), QTime(19, 12)),
                                      QStringLiteral("howl"), QStringLiteral("session"), 10});
        Event spent;
        spent.at = QDateTime(QDate(2026, 9, 2), QTime(21, 0));
        spent.kind = EventKind::Exhausted;
        spent.budget = QStringLiteral("session");
        yesterday.events.append(spent);

        const Outcome outcome =
            evaluate(profile,
                     {scope(QStringLiteral("chromium"),
                            QStringLiteral("app-Hyprland-chromium-031bdc27.scope"), 21)},
                     yesterday, QDateTime(QDate(2026, 9, 3), QTime(0, 0, 2)), 2);

        QCOMPARE(outcome.ledger.date, QDate(2026, 9, 3));
        QCOMPARE(outcome.ledger.user, QStringLiteral("julia"));
        QCOMPARE(outcome.ledger.secondsFor(QStringLiteral("session")), 2);
        // Yesterday's ten granted minutes were yesterday's, and yesterday's
        // warnings do not count as this morning's.
        QVERIFY(outcome.ledger.grants.isEmpty());
        QVERIFY(outcome.ledger.events.isEmpty());
        QVERIFY2(outcome.decisions.isEmpty(), "yesterday's spent session logged the user out");
    }

    void readsTheProfileOfTheSpecAndWritesItBack()
    {
        QVector<Profile> profiles;
        QString error;
        QVERIFY2(profilesFromJson(parse(kSpecProfiles), &profiles, &error), qPrintable(error));
        QCOMPARE(profiles.size(), 1);

        const Profile julia = profiles.at(0);
        QCOMPARE(julia.user, QStringLiteral("julia"));
        QCOMPARE(julia.displayName, QString::fromUtf8("Júlia"));
        QVERIFY(julia.enabled);
        QVERIFY(julia.enforce);
        QCOMPARE(julia.defaultVerdict, Verdict::Deny);
        QCOMPARE(julia.warnAt, QVector<int>({10, 5, 1}));
        QCOMPARE(julia.graceSeconds, 20);
        QCOMPARE(julia.rules.size(), 3);
        QCOMPARE(julia.rules.at(0).match, QStringLiteral("minecraft-launcher"));
        QCOMPARE(julia.rules.at(0).verdict, Verdict::Allow);
        QCOMPARE(julia.budgets.size(), 3);
        QCOMPARE(julia.budgets.at(0).id, QStringLiteral("session"));
        QCOMPARE(julia.budgets.at(0).match, QStringLiteral("*"));
        QCOMPARE(julia.budgets.at(0).dailyMinutes, 120);
        QCOMPARE(julia.budgets.at(0).onExhausted, OnExhausted::Logout);
        QCOMPARE(julia.budgets.at(1).onExhausted, OnExhausted::Close);

        // Out and back in: the writer's own file has to read as the same profile,
        // or an operator editing a profile loses part of it every time.
        QVector<Profile> again;
        QVERIFY2(profilesFromJson(profilesToJson(profiles), &again, &error), qPrintable(error));
        QCOMPARE(again.size(), 1);
        QCOMPARE(again.at(0).toJson(), julia.toJson());

        // A budget with no limit survives as one, rather than coming back as a
        // budget of zero minutes that is over before the day starts.
        Profile counted;
        counted.user = QStringLiteral("howl");
        Budget watched;
        watched.id = QStringLiteral("chromium");
        watched.match = QStringLiteral("chromium");
        counted.budgets = {watched};
        Profile reread;
        QVERIFY2(Profile::fromJson(counted.toJson(), &reread, &error), qPrintable(error));
        QCOMPARE(reread.budgets.at(0).dailyMinutes, 0);
        QVERIFY(!reread.budgets.at(0).hasLimit());
    }

    void readsTheLedgerOfTheSpecAndWritesItBack()
    {
        Ledger ledger;
        QString error;
        QVERIFY2(Ledger::fromJson(parse(kSpecLedger), &ledger, &error), qPrintable(error));

        QCOMPARE(ledger.user, QStringLiteral("julia"));
        QCOMPARE(ledger.date, QDate(2026, 9, 3));
        QCOMPARE(ledger.secondsFor(QStringLiteral("session")), 4210);
        QCOMPARE(ledger.secondsFor(QStringLiteral("minecraft")), 2400);
        QCOMPARE(ledger.grants.size(), 1);
        QCOMPARE(ledger.grants.at(0).by, QStringLiteral("howl"));
        QCOMPARE(ledger.grants.at(0).minutes, 10);
        QCOMPARE(ledger.grantedSeconds(QStringLiteral("session")), 600);
        // The offset of the spec's own stamp, kept as the instant it names
        // rather than as whatever that clock time would mean here.
        QCOMPARE(ledger.grants.at(0).at,
                 QDateTime::fromString(QStringLiteral("2026-09-03T19:12:04-03:00"), Qt::ISODate));
        QCOMPARE(ledger.events.size(), 1);
        QCOMPARE(ledger.events.at(0).kind, EventKind::Exhausted);
        QCOMPARE(ledger.exhaustedAt(QStringLiteral("minecraft")),
                 QDateTime::fromString(QStringLiteral("2026-09-03T19:30:11-03:00"), Qt::ISODate));

        Ledger again;
        QVERIFY2(Ledger::fromJson(ledger.toJson(), &again, &error), qPrintable(error));
        QCOMPARE(again.toJson(), ledger.toJson());
        QCOMPARE(again.grants.at(0).at, ledger.grants.at(0).at);
        QCOMPARE(again.events.at(0).at, ledger.events.at(0).at);

        // The marks a warning leaves behind survive the file too, because they
        // are the only thing that keeps the next tick from saying it again.
        Ledger marked;
        marked.user = QStringLiteral("julia");
        marked.date = QDate(2026, 9, 3);
        Event warned;
        warned.at = at(19, 25);
        warned.kind = EventKind::Warn;
        warned.budget = QStringLiteral("minecraft");
        warned.minutes = 5;
        marked.events.append(warned);
        Event refused;
        refused.at = at(19, 26);
        refused.kind = EventKind::Denied;
        refused.scope = QStringLiteral("app-Hyprland-steam-031bdc27.scope");
        marked.events.append(refused);

        Ledger back;
        QVERIFY2(Ledger::fromJson(marked.toJson(), &back, &error), qPrintable(error));
        QVERIFY(back.hasWarned(QStringLiteral("minecraft"), 5));
        QVERIFY(!back.hasWarned(QStringLiteral("minecraft"), 1));
        QVERIFY(back.hasDenied(QStringLiteral("app-Hyprland-steam-031bdc27.scope")));
    }

    // Presence rides in the day's file beside the budgets, and it is absent from
    // every ledger written before it was ever measured -- which is every ledger
    // already on a machine this ships to. Absent is empty and never an error,
    // and a day with nothing to say about presence writes no key at all rather
    // than an empty object that would rewrite every file on disk to say nothing.
    void presenceRidesBesideTheBudgetsAndIsOptional()
    {
        Ledger old;
        QString error;
        QVERIFY2(Ledger::fromJson(parse(kSpecLedger), &old, &error), qPrintable(error));
        QVERIFY(old.presence.isEmpty());
        QVERIFY(!old.toJson().contains(QStringLiteral("presence")));

        Ledger day;
        day.user = QStringLiteral("julia");
        day.date = QDate(2026, 9, 3);
        day.addSeconds(QStringLiteral("session"), 600);
        day.addPresenceSeconds(QStringLiteral("using"), 400);
        day.addPresenceSeconds(QStringLiteral("screen-off"), 200);
        day.addPresenceSeconds(QStringLiteral("screen-off"), 2);
        QCOMPARE(day.presenceSecondsFor(QStringLiteral("screen-off")), 202);
        QCOMPARE(day.presenceSecondsFor(QStringLiteral("locked")), 0);

        // Ten minutes of the session budget, and only four hundred seconds of
        // anybody in front of the machine. The two numbers disagreeing is the
        // whole point of measuring the second one, and neither has touched the
        // other.
        QCOMPARE(day.secondsFor(QStringLiteral("session")), 600);

        Ledger back;
        QVERIFY2(Ledger::fromJson(day.toJson(), &back, &error), qPrintable(error));
        QCOMPARE(back.toJson(), day.toJson());
        QCOMPARE(back.presenceSecondsFor(QStringLiteral("using")), 400);
        QCOMPARE(back.presenceSecondsFor(QStringLiteral("screen-off")), 202);
        QCOMPARE(back.secondsFor(QStringLiteral("session")), 600);

        // A presence that is not an object is a file somebody edited into
        // something this cannot read, and it is refused rather than ignored:
        // guessing here would be inventing a day.
        QJsonObject broken = day.toJson();
        broken.insert(QStringLiteral("presence"), QStringLiteral("all afternoon"));
        Ledger nothing;
        QVERIFY(!Ledger::fromJson(broken, &nothing, &error));
        QVERIFY(error.contains(QStringLiteral("presence")));
    }

    // tmp + rename, docs/design.md §4. A reader of the ledger sees the whole of one
    // tick or the whole of the one before it, never the first half of a write
    // that a power cut ended.
    void writesTheLedgerByRename()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        // The daemon writes /var/lib/omahouse/<user>/<date>.json, and the
        // directory of a user who has never been counted does not exist yet.
        const QString path =
            root.filePath(QStringLiteral("var/lib/omahouse/julia/2026-09-03.json"));

        Ledger first;
        first.user = QStringLiteral("julia");
        first.date = QDate(2026, 9, 3);
        first.seconds.insert(QStringLiteral("session"), 4210);

        QString error;
        QVERIFY2(writeLedger(path, first, &error), qPrintable(error));
        QVERIFY(QFile::exists(path));
        const uint before = inodeOf(path);
        QVERIFY(before != 0);

        Ledger second = first;
        second.seconds.insert(QStringLiteral("session"), 4212);
        QVERIFY2(writeLedger(path, second, &error), qPrintable(error));

        // A different inode under the same name is the rename. An in-place
        // rewrite would keep it, and would have a window in which the file was
        // shorter than either version of itself.
        QVERIFY2(inodeOf(path) != before, "the ledger was rewritten in place, not renamed over");

        Ledger read;
        QVERIFY2(readLedger(path, &read, &error), qPrintable(error));
        QCOMPARE(read.secondsFor(QStringLiteral("session")), 4212);

        // Nothing left over: a temporary that survives is a directory that fills
        // up with two files every two seconds.
        const QStringList leftovers =
            QDir(QFileInfo(path).absolutePath())
                .entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
        QCOMPARE(leftovers, QStringList{QStringLiteral("2026-09-03.json")});

        // 0644: root writes it and the fiscalised user reads it to see what is
        // left (docs/design.md §4).
        const QFileDevice::Permissions mode = QFile(path).permissions();
        QVERIFY(mode.testFlag(QFileDevice::ReadOwner));
        QVERIFY(mode.testFlag(QFileDevice::WriteOwner));
        QVERIFY(mode.testFlag(QFileDevice::ReadOther));
        QVERIFY(!mode.testFlag(QFileDevice::WriteOther));
        QVERIFY(!mode.testFlag(QFileDevice::WriteGroup));

        // A file that is not there is a day with nothing spent, not a failure.
        Ledger empty;
        bool missing = false;
        QVERIFY(readLedger(root.filePath(QStringLiteral("nobody/2026-09-03.json")), &empty, &error,
                           &missing));
        QVERIFY(missing);
        QVERIFY(empty.user.isEmpty());

        // And the same routine for the other file of §4.
        const QString profilePath = root.filePath(QStringLiteral("etc/omahouse/profiles.json"));
        QVector<Profile> profiles;
        QVERIFY2(profilesFromJson(parse(kSpecProfiles), &profiles, &error), qPrintable(error));
        QVERIFY2(writeProfiles(profilePath, profiles, &error), qPrintable(error));
        QVector<Profile> reread;
        QVERIFY2(readProfiles(profilePath, &reread, &error), qPrintable(error));
        QCOMPARE(reread.size(), 1);
        QCOMPARE(reread.at(0).toJson(), profiles.at(0).toJson());
    }

    // A schema version this code does not know is a file whose missing fields it
    // would have to invent. It says so instead.
    void refusesASchemaVersionItDoesNotKnow()
    {
        QJsonObject profiles = parse(kSpecProfiles);
        profiles.insert(QStringLiteral("schemaVersion"), 2);
        QVector<Profile> parsed;
        QString error;
        QVERIFY2(!profilesFromJson(profiles, &parsed, &error), "it read a profiles.json from the future");
        QVERIFY(error.contains(QStringLiteral("schema version 2")));

        QJsonObject ledger = parse(kSpecLedger);
        ledger.insert(QStringLiteral("schemaVersion"), 99);
        Ledger read;
        error.clear();
        QVERIFY2(!Ledger::fromJson(ledger, &read, &error), "it read a ledger from the future");
        QVERIFY(error.contains(QStringLiteral("schema version 99")));

        // No version at all is refused the same way. A file with no version is
        // not a file from version 1; it is a file nobody can date.
        ledger = parse(kSpecLedger);
        ledger.remove(QStringLiteral("schemaVersion"));
        error.clear();
        QVERIFY2(!Ledger::fromJson(ledger, &read, &error), "it read a ledger with no version");
        QVERIFY(!error.isEmpty());

        // And a version that is not a number is not a version.
        ledger = parse(kSpecLedger);
        ledger.insert(QStringLiteral("schemaVersion"), QStringLiteral("1"));
        error.clear();
        QVERIFY(!Ledger::fromJson(ledger, &read, &error));
    }
};

int runLedgerTests(int argc, char **argv)
{
    LedgerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_ledger.moc"
