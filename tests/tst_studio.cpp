// The studio, driven twice over: once entirely by the keyboard, once entirely by
// the mouse, with no screen involved.
//
// This file exists because of one promise, and it is the promise stage 8 was
// asked for: **everything is on the keyboard, and the mouse does exactly the
// same thing.** That is not a property a screenshot shows or a unit test on a
// model can reach. It is a property of a window under a person's hands, so the
// suite puts the real window under real key events and real clicks and asks the
// real state what happened.
//
// `theWholeJobOnTheKeyboard` and `theWholeJobOnTheMouse` do the same work --
// put an account under rules, release two programs with a limit each, set the
// day's total and hand over ten minutes -- through the two doors, and assert the
// same profile came out. `everyCommandIsBothAKeyAndAChip` is the general form of
// it: for every row of the window's one command table, the chip exists and the
// key answers.
//
// Nothing here needs root and nothing here touches /etc. Both roots are pointed
// at a temporary tree, which is also what makes the write path exercisable:
// `Admin` asks polkit only when it is writing the machine's own files --
// `Paths::configDirIsTheSystems` -- so pointed elsewhere it runs the same CLI
// with the same arguments directly, and what is asserted is the profile the real
// verbs wrote.
//
// The operator face is what is driven here. The subject face is the same window
// with the command table cut to `open` and `back`, and a read-only window has no
// parity to violate: there is no action for one door to have and the other to
// lack. `theSubjectFaceHasNothingToPress` pins that shape from the other end.
//
// Two cases at the foot of the file do a second job with the same machinery:
// `writesTheOperatorShots` and `writesTheSubjectShots` drive the window over
// every screen it can draw and save each one into `$OMAHOUSE_SHOTS`, which is
// how `docs/img` and `docs/screens.md` are made. They skip when nothing asked
// for pictures, so the gate never writes any.

#include "Admin.h"
#include "Catalog.h"
#include "House.h"
#include "Ledger.h"
#include "Fleet.h"
#include "Kind.h"
#include "Profile.h"
#include "Theme.h"
#include "Paths.h"
#include "Users.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStyleHints>
#include <QDirIterator>
#include <QTemporaryDir>
#include <QtQml>
#include <QtTest>

using namespace omahouse;

namespace {

/// Everything on screen under a given objectName.
///
/// Not QObject::findChild: a delegate made by a Repeater has no QObject parent
/// at all, only a visual one, so searching the object tree finds the handful of
/// items written directly in Main.qml and none of the ones a model produced --
/// which is every command chip in this window.
QQuickItem *itemNamed(QQuickItem *from, const QString &name)
{
    if (!from)
        return nullptr;
    if (from->objectName() == name)
        return from;
    const QList<QQuickItem *> children = from->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = itemNamed(child, name))
            return found;
    }
    return nullptr;
}

void writeDesktopEntry(const QString &directory, const QString &id, const QString &name,
                       const QString &exec)
{
    QFile file(directory + QLatin1Char('/') + id + QStringLiteral(".desktop"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nExec=%2 %%U\n")
                   .arg(name, exec)
                   .toUtf8());
}

/// A cgroup, as far as this walk is concerned: a directory with a `cgroup.procs`
/// in it holding one pid per line. The same fixture shape tst_proc.cpp uses.
void makeCgroup(const QString &path, const QVector<int> &pids)
{
    QVERIFY2(QDir().mkpath(path), qPrintable(path));
    QFile file(path + QStringLiteral("/cgroup.procs"));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Text), qPrintable(path));
    for (int pid : pids)
        file.write(QByteArray::number(pid) + '\n');
}

void makeProcess(const QString &procRoot, int pid, const QString &executable)
{
    const QString directory = QStringLiteral("%1/%2").arg(procRoot).arg(pid);
    QVERIFY2(QDir().mkpath(directory), qPrintable(directory));
    // A pid that already has one is left alone rather than failing: the two
    // shot cases seed the same tree, and `QFile::link` will not write over an
    // existing name.
    if (QFile::exists(directory + QStringLiteral("/exe")))
        return;
    QVERIFY2(QFile::link(executable, directory + QStringLiteral("/exe")), qPrintable(directory));
}

/// Whether there is anything drawn in that frame, as opposed to a flat colour.
///
/// The one question a picture can be asked without pinning a layout to a number.
/// A window that renders nothing at all still answers every other case in this
/// file correctly -- the keys work, the profile is written -- and leaves a black
/// rectangle where the balance is; and a generator without this check fills
/// docs/img with those and nobody notices, which is the specific way a picture
/// in a document goes wrong.
bool moreThanOneColour(const QImage &frame)
{
    QSet<QRgb> colours;
    for (int y = 0; y < frame.height() && colours.size() < 8; y += 3) {
        for (int x = 0; x < frame.width() && colours.size() < 8; x += 3)
            colours.insert(frame.pixel(x, y));
    }
    return colours.size() > 4;
}

} // namespace

class TestStudio : public QObject
{
    Q_OBJECT

private slots:
    void everyQuietWordIsReadableOnEveryTheme();
    void initTestCase();
    void cleanupTestCase();
    void init();

    void opensOnTheFaceOfWhoeverRanIt();
    void theWholeJobOnTheKeyboard();
    void theWholeJobOnTheMouse();
    void theWebHalfOnBothDoors();
    void everyCommandIsBothAKeyAndAChip();
    void everyKeyOnTheSheetIsAnswered();
    void aRefusalIsASentenceAndNotASilence();
    void saysWhatIsReallyInsideAShim();
    void drawsItself();
    void fleetPanelShowsMissingMachinesAndUsesTheKeyboard();
    void theTodayViewAddsUpTheHouse();
    void theFilterNarrowsOnlyTheListItWasTypedOn();
    void theSubjectFaceHasNothingToPress();
    void theWindowNeverWritesToTheMachine();
    void writesTheOperatorShots();
    void writesTheSubjectShots();

private:
    QQuickWindow *window() const;
    QObject *root() const;
    void settle();
    void key(char ch, Qt::KeyboardModifiers mods = Qt::NoModifier);
    void key(Qt::Key code, Qt::KeyboardModifiers mods = Qt::NoModifier);
    void type(const QString &text);
    void typeInto(const QString &fieldName, const QString &text);
    void click(const QString &objectName);
    void clickItem(QQuickItem *item);
    QQuickItem *awaitItem(const QString &objectName, int milliseconds = 4000);
    QQuickItem *awaitRow(const QString &listName, int index, int milliseconds = 4000);
    bool waitForWrite();
    /// Every file under a directory, by path and by bytes.
    ///
    /// A fingerprint of a tree, so that "nothing was written" can be asserted
    /// as one comparison rather than as a list of files somebody remembered.
    static QByteArray treeUnder(const QString &directory);
    void emptyTheHouse();
    QString shotsDir() const;
    void shoot(const QString &name);
    void seedTheExampleHousehold();
    void seedTheExampleSession(uid_t uid);
    QVariantList people() const;
    QVariantMap personNamed(const QString &user) const;
    QVariantList programsOf(const QString &user) const;
    QVariantList sitesOf(const QString &user) const;
    QVariantList todayOf(const QString &user) const;
    QVariantMap siteNamed(const QString &user, const QString &domain) const;

    QTemporaryDir m_tree;
    QQmlApplicationEngine *m_engine = nullptr;
    House *m_house = nullptr;
    Admin *m_admin = nullptr;
    QString m_cli;
    /// How many commands have finished, and how many of those a `waitForWrite`
    /// has already accounted for.
    ///
    /// Counted from a connection made once, rather than by a QSignalSpy created
    /// after the click that starts the command. `omahouse profile default` is a
    /// JSON file rewritten in a couple of milliseconds, and the click that asks
    /// for it pumps the event loop on its way out -- so the process can be over
    /// before a spy made afterwards exists to hear it, and the wait would then
    /// sit out its whole timeout waiting for something that had already
    /// happened. That was a suite that failed about one run in ten, always on a
    /// different case, which is the worst kind of green.
    int m_finished = 0;
    int m_accounted = 0;
    bool m_lastWriteOk = false;
};

void TestStudio::initTestCase()
{
    QVERIFY(m_tree.isValid());
    const QString root = m_tree.path();

    // The text caret stops blinking. Zero is Qt's own word for "does not flash",
    // and without it a field is on screen for half a second out of every one --
    // so two grabs of a window nothing is happening in differ, and the pictures
    // in docs/img would carry a caret or not depending on when the machine got
    // round to them.
    QGuiApplication::styleHints()->setCursorFlashTime(0);

    // Both roots somewhere of this suite's own. Not a convenience: it is the
    // seam Paths.h documents -- a run pointed at a tree of its own is writing
    // where it was told to write, and demanding root for that would be demanding
    // root to write in somebody's home directory.
    qputenv("OMAHOUSE_CONFIG_DIR", (root + "/etc").toLocal8Bit());
    qputenv("OMAHOUSE_STATE_DIR", (root + "/var").toLocal8Bit());
    qputenv("OMAHOUSE_CGROUP_ROOT", (root + "/cgroup").toLocal8Bit());
    qputenv("OMAHOUSE_PROC_ROOT", (root + "/proc").toLocal8Bit());
    qputenv("OMAHOUSE_DESKTOP_DIRS", (root + "/share").toLocal8Bit());
    // The third root, and the one with the shortest fuse -- docs/design.md §11,
    // "The refusal that protects the developer's own browser". This suite runs
    // the real `omahouse web` verbs, and every verb that writes a profile
    // reconciles the browser policy on its way out. Left unset, that write would
    // be refused for the right reason and would say so on the status bar, which
    // is a sentence in every picture `mise run shots` takes; pointed here, the
    // file is written where it was told to write and the window is drawn at
    // rest.
    qputenv("OMAHOUSE_CHROMIUM_POLICY_DIR", (root + "/chromium").toLocal8Bit());
    QVERIFY(QDir().mkpath(root + "/etc"));
    QVERIFY(QDir().mkpath(root + "/var"));
    QVERIFY(QDir().mkpath(root + "/chromium"));
    QVERIFY(QDir().mkpath(root + "/share/applications"));

    // The picker is fed from these. Two programs, and one of them a launcher
    // shim: `gtk-launch` is the case poc/findings.md round 4 measured, and the
    // window has to say what is inside it rather than let a rule be written
    // blind.
    writeDesktopEntry(root + "/share/applications", QStringLiteral("code"),
                      QStringLiteral("Code"), QStringLiteral("/usr/share/code/code"));
    writeDesktopEntry(root + "/share/applications", QStringLiteral("firefox"),
                      QStringLiteral("Firefox"), QStringLiteral("/usr/lib/firefox/firefox"));

    m_cli = QFileInfo(QCoreApplication::applicationDirPath()
                      + QStringLiteral("/../../build/bin/omahouse"))
                .absoluteFilePath();
    QVERIFY2(QFileInfo(m_cli).isExecutable(),
             qPrintable(QStringLiteral("no CLI at %1 — run mise run build first").arg(m_cli)));
    qputenv("OMAHOUSE_CLI", m_cli.toLocal8Bit());

    // Nothing is registered here. All three types are declared in their headers
    // and registered by the build, and the engine makes the singletons on first
    // use -- which is during `load`, after the environment above is in place, so
    // they see the temporary tree and never the machine's own.
    m_engine = new QQmlApplicationEngine(this);
    m_engine->load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    QVERIFY2(!m_engine->rootObjects().isEmpty(), "Main.qml did not load");

    m_house = m_engine->singletonInstance<House *>(QStringLiteral("omahouse"),
                                                   QStringLiteral("House"));
    m_admin = m_engine->singletonInstance<Admin *>(QStringLiteral("omahouse"),
                                                   QStringLiteral("Admin"));
    QVERIFY(m_house && m_admin);
    connect(m_admin, &Admin::done, this, [this](bool ok) {
        ++m_finished;
        m_lastWriteOk = ok;
    });
    QVERIFY(window());
    QVERIFY(QTest::qWaitForWindowExposed(window()));
}

void TestStudio::cleanupTestCase()
{
    delete m_engine;
    m_engine = nullptr;
}

void TestStudio::init()
{
    // Whatever the last case left open, shut. A case that failed with a prompt
    // up would otherwise hand the next one a window where `blocked` is true and
    // no key does anything, and the failure would land on the wrong test.
    for (int i = 0; i < 6 && root()->property("blocked").toBool(); ++i)
        key(Qt::Key_Escape);
    m_accounted = m_finished;
    emptyTheHouse();
    // Every case starts on the first view with every cursor at the top. A case
    // that left one three rows down would hand the next one a window standing
    // somewhere it never asked to stand, and the failure would land on the
    // wrong test.
    root()->setProperty("cursorPeople", 0);
    root()->setProperty("cursorPrograms", 0);
    root()->setProperty("cursorSites", 0);
    root()->setProperty("cursorToday", 0);
    // Every list's own needle, because each of them keeps it now: a case that
    // left one typed would hand the next one a narrowed list it never asked
    // for.
    for (const char *needle : {"filterPeople", "filterPrograms", "filterToday",
                               "filterSites", "filterFleet"}) {
        root()->setProperty(needle, QString());
    }
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(1)));
    settle();
}

QQuickWindow *TestStudio::window() const
{
    if (!m_engine || m_engine->rootObjects().isEmpty())
        return nullptr;
    return qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
}

QObject *TestStudio::root() const
{
    return m_engine->rootObjects().first();
}

void TestStudio::settle()
{
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

void TestStudio::key(char ch, Qt::KeyboardModifiers mods)
{
    QTest::keyClick(window(), ch, mods);
    settle();
}

void TestStudio::key(Qt::Key code, Qt::KeyboardModifiers mods)
{
    QTest::keyClick(window(), code, mods);
    settle();
}

void TestStudio::type(const QString &text)
{
    for (const QChar &ch : text)
        QTest::keyClick(window(), ch.toLatin1());
    settle();
}

/// Click the middle of an item, once it has stopped moving.
///
/// The wait for a settled position is not politeness. A Repeater builds its
/// items at their implicit size and the Row that holds them positions them on
/// the next polish pass, so an item read the instant it appears is an item at
/// x = 0 -- and the click computed from that lands on whichever chip is
/// leftmost. That is a test that presses `back` while believing it pressed
/// `release`, and then fails four seconds later somewhere else entirely.
void TestStudio::clickItem(QQuickItem *item)
{
    QVERIFY(item);
    QVERIFY2(item->isVisible(), qPrintable(item->objectName()));

    // Every Row and Column above it lays out now, rather than on the polish
    // pass that has not happened yet. `forceLayout` is the positioners' own
    // answer to this question and it is invokable; on anything that is not one
    // the call simply does not match, which is the "ignore" this wants.
    const auto layOut = [item] {
        for (QQuickItem *up = item; up; up = up->parentItem()) {
            // Asked for by name only where there is one to ask. Calling it
            // blindly works, and prints a warning per ancestor per frame for
            // every item that is not a positioner -- which buries the output of
            // whatever the case was actually about.
            if (up->metaObject()->indexOfMethod("forceLayout()") >= 0)
                QMetaObject::invokeMethod(up, "forceLayout");
        }
    };

    layOut();
    QPointF centre = item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
    QDeadlineTimer deadline(4000);
    int steady = 0;
    forever {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        layOut();
        const QPointF now =
            item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
        steady = now == centre ? steady + 1 : 0;
        centre = now;
        if (steady >= 3)
            break;
        if (deadline.hasExpired()) {
            QTest::qFail(qPrintable(QStringLiteral("%1 will not stop moving")
                                        .arg(item->objectName())),
                         __FILE__, __LINE__);
            return;
        }
    }

    QTest::mouseClick(window(), Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    settle();
}

void TestStudio::click(const QString &objectName)
{
    QQuickItem *item = awaitItem(objectName);
    if (QTest::currentTestFailed())
        return;
    clickItem(item);
}

/// The item under that name, once it is on screen.
///
/// Waited for rather than looked up once. A sheet is a Rectangle that becomes
/// visible and a list is delegates a view creates when it next lays itself out,
/// and neither is finished by the time the click that asked for it returns --
/// least of all offscreen, where nothing is pacing the frames. Looking once and
/// asserting was a suite that failed about one run in ten, always somewhere
/// different, and never for a reason the window had anything to do with.
QQuickItem *TestStudio::awaitItem(const QString &objectName, int milliseconds)
{
    QDeadlineTimer deadline(milliseconds);
    forever {
        QQuickItem *item = itemNamed(window()->contentItem(), objectName);
        if (item && item->isVisible() && item->width() > 0 && item->height() > 0)
            return item;
        if (deadline.hasExpired())
            break;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QTest::qFail(qPrintable(QStringLiteral("%1 never came up").arg(objectName)),
                 __FILE__, __LINE__);
    return nullptr;
}

QQuickItem *TestStudio::awaitRow(const QString &listName, int index, int milliseconds)
{
    QQuickItem *list = awaitItem(listName, milliseconds);
    if (!list)
        return nullptr;
    QDeadlineTimer deadline(milliseconds);
    forever {
        QQuickItem *row = nullptr;
        QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row),
                                  Q_ARG(int, index));
        if (row && row->isVisible() && row->height() > 0)
            return row;
        if (deadline.hasExpired())
            break;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QTest::qFail(qPrintable(QStringLiteral("%1 has no row %2 to click")
                                .arg(listName).arg(index)),
                 __FILE__, __LINE__);
    return nullptr;
}

/// Type into a field, once the field has the keyboard.
///
/// The wait is the point. A character sent while the sheet is still opening goes
/// to the window instead, where `m` is a command and `45m` is three keystrokes
/// aimed at whatever the window thought it was doing -- which is a test that
/// fails by driving the program correctly.
void TestStudio::typeInto(const QString &fieldName, const QString &text)
{
    QQuickItem *field = awaitItem(fieldName);
    if (!field)
        return;
    QDeadlineTimer deadline(4000);
    while (!field->property("editing").toBool() && !field->hasActiveFocus()
           && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (!field->property("editing").toBool() && !field->hasActiveFocus()) {
        // Say where the keyboard went instead. "the field never took it" is the
        // symptom of half a dozen different causes, and the item holding it is
        // the one fact that tells them apart.
        const QQuickItem *holder = window()->activeFocusItem();
        QTest::qFail(qPrintable(QStringLiteral("%1 never took the keyboard; it is on %2")
                                    .arg(fieldName,
                                         holder ? (holder->objectName().isEmpty()
                                                       ? QString::fromUtf8(holder->metaObject()
                                                                               ->className())
                                                       : holder->objectName())
                                                : QStringLiteral("nothing"))),
                     __FILE__, __LINE__);
        return;
    }
    type(text);
}

/// Wait for the command that was just asked for to come back.
///
/// It is a real `omahouse` in a real process, and it is the point: what this
/// suite asserts about is the profile the shipped verbs wrote, not a JSON
/// document the test made up.
bool TestStudio::waitForWrite()
{
    QDeadlineTimer deadline(15000);
    while (m_finished <= m_accounted && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (m_finished <= m_accounted)
        return false;
    ++m_accounted;
    settle();
    m_house->reload();
    settle();
    return m_lastWriteOk;
}

void TestStudio::emptyTheHouse()
{
    QFile::remove(paths::profilesFile());
    QDir(paths::stateDir()).removeRecursively();
    QDir().mkpath(paths::stateDir());
    m_house->reload();
    settle();
    QVERIFY(people().isEmpty());
}

QVariantList TestStudio::people() const
{
    return m_house->people();
}

QVariantMap TestStudio::personNamed(const QString &user) const
{
    const QVariantList all = people();
    for (const QVariant &row : all) {
        const QVariantMap person = row.toMap();
        if (person.value(QStringLiteral("user")).toString() == user)
            return person;
    }
    return QVariantMap();
}

QVariantList TestStudio::programsOf(const QString &user) const
{
    return m_house->snapshot().value(QStringLiteral("programs")).toMap().value(user).toList();
}

QVariantList TestStudio::sitesOf(const QString &user) const
{
    return m_house->snapshot().value(QStringLiteral("sites")).toMap().value(user).toList();
}

QVariantList TestStudio::todayOf(const QString &user) const
{
    return m_house->snapshot().value(QStringLiteral("today")).toMap().value(user).toList();
}

QVariantMap TestStudio::siteNamed(const QString &user, const QString &domain) const
{
    const QVariantList all = sitesOf(user);
    for (const QVariant &row : all) {
        const QVariantMap site = row.toMap();
        if (site.value(QStringLiteral("id")).toString() == domain)
            return site;
    }
    return QVariantMap();
}

// ---------------------------------------------------------------------------

void TestStudio::opensOnTheFaceOfWhoeverRanIt()
{
    // Nobody chooses it, and there is no switch on screen to look for. It is
    // wheel or it is not -- docs/design.md §1 -- and this machine's answer is whatever
    // it is; what is asserted is that the window agrees with the account table
    // rather than with a default.
    QString why;
    const bool administers = isAdministrator(currentUser(), &why);
    QCOMPARE(m_house->face(), administers ? QStringLiteral("operator")
                                          : QStringLiteral("subject"));
    QCOMPARE(root()->property("operating").toBool(), administers);
    if (!administers)
        QSKIP("the rest of this suite drives the operator face, and this account is not in wheel");
}

void TestStudio::theWholeJobOnTheKeyboard()
{
    if (!root()->property("operating").toBool())
        QSKIP("not in wheel");

    // Put an account under rules. `n`, the name, Enter.
    key('n');
    typeInto(QStringLiteral("promptField"), QStringLiteral("tstjulia"));
    key(Qt::Key_Return);
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));
    QCOMPARE(people().size(), 1);
    QCOMPARE(personNamed(QStringLiteral("tstjulia")).value(QStringLiteral("user")).toString(),
             QStringLiteral("tstjulia"));

    // Only what is listed runs. `d` flips the profile from a denylist to an
    // allowlist, which is what "released programs" means.
    key('d');
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));
    QVERIFY(personNamed(QStringLiteral("tstjulia")).value(QStringLiteral("allowlist")).toBool());

    // Two programs, each with a limit. `2` for the programs, `a` to release,
    // the query, Enter to pick, the duration, Enter.
    key('2');
    key('a');
    typeInto(QStringLiteral("pickerQuery"), QStringLiteral("code"));
    key(Qt::Key_Return);
    typeInto(QStringLiteral("promptField"), QStringLiteral("45m"));
    key(Qt::Key_Return);
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));

    key('a');
    typeInto(QStringLiteral("pickerQuery"), QStringLiteral("firefox"));
    key(Qt::Key_Return);
    typeInto(QStringLiteral("promptField"), QStringLiteral("1h"));
    key(Qt::Key_Return);
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));

    const QVariantList programs = programsOf(QStringLiteral("tstjulia"));
    QCOMPARE(programs.size(), 2);
    QSet<QString> released;
    for (const QVariant &row : programs) {
        const QVariantMap program = row.toMap();
        QVERIFY(program.value(QStringLiteral("released")).toBool());
        released.insert(program.value(QStringLiteral("id")).toString() + QLatin1Char('=')
                        + program.value(QStringLiteral("limit")).toString());
    }
    QCOMPARE(released, QSet<QString>({QStringLiteral("code=45m"), QStringLiteral("firefox=1h")}));

    // The day's total, and then ten minutes handed over on top of it.
    key('3');
    key('s');
    typeInto(QStringLiteral("promptField"), QStringLiteral("2h"));
    key(Qt::Key_Return);
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));

    QVariantMap julia = personNamed(QStringLiteral("tstjulia"));
    QVERIFY(julia.value(QStringLiteral("hasSessionBudget")).toBool());
    QCOMPARE(julia.value(QStringLiteral("session")).toMap().value(QStringLiteral("left")).toString(),
             QStringLiteral("2h"));

    // The cursor is on the session, which the day's list puts first.
    QCOMPARE(todayOf(QStringLiteral("tstjulia")).first().toMap().value(QStringLiteral("session")).toBool(),
             true);
    key('+');
    typeInto(QStringLiteral("promptField"), QStringLiteral("10m"));
    key(Qt::Key_Return);
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));

    // And the balance says so, which is the whole errand: a grant that did not
    // show up here would read as though it had gone nowhere.
    julia = personNamed(QStringLiteral("tstjulia"));
    const QVariantMap session = julia.value(QStringLiteral("session")).toMap();
    QCOMPARE(session.value(QStringLiteral("left")).toString(), QStringLiteral("2h10m"));
    QCOMPARE(session.value(QStringLiteral("granted")).toString(), QStringLiteral("10m"));

    // Not one pointer event was sent in this test.
}

void TestStudio::theWholeJobOnTheMouse()
{
    if (!root()->property("operating").toBool())
        QSKIP("not in wheel");

    // The same errand through the other door. Text still arrives from the
    // keyboard, because a name and a duration are typed by anybody on any
    // machine -- what is under test is that every *action* has a target to click.
    click(QStringLiteral("command-new"));
    typeInto(QStringLiteral("promptField"), QStringLiteral("tstjulia"));
    click(QStringLiteral("promptOk"));
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));
    QCOMPARE(people().size(), 1);

    click(QStringLiteral("command-policy"));
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));
    QVERIFY(personNamed(QStringLiteral("tstjulia")).value(QStringLiteral("allowlist")).toBool());

    click(QStringLiteral("viewChip2"));
    QCOMPARE(root()->property("view").toInt(), 2);

    for (const QString &pair : {QStringLiteral("code|45m"), QStringLiteral("firefox|1h")}) {
        const QString id = pair.section(QLatin1Char('|'), 0, 0);
        const QString limit = pair.section(QLatin1Char('|'), 1, 1);
        click(QStringLiteral("command-release"));
        typeInto(QStringLiteral("pickerQuery"), id);
        // The row itself, not a key: the picker's list is walked and chosen with
        // the pointer here.
        QQuickItem *first = awaitRow(QStringLiteral("pickerList"), 0);
        QVERIFY2(first, qPrintable(QStringLiteral("the picker has no row for %1").arg(id)));
        clickItem(first);
        typeInto(QStringLiteral("promptField"), limit);
        click(QStringLiteral("promptOk"));
        QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));
    }

    QCOMPARE(programsOf(QStringLiteral("tstjulia")).size(), 2);

    click(QStringLiteral("viewChip3"));
    click(QStringLiteral("command-day"));
    typeInto(QStringLiteral("promptField"), QStringLiteral("2h"));
    click(QStringLiteral("promptOk"));
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));

    click(QStringLiteral("command-grant"));
    typeInto(QStringLiteral("promptField"), QStringLiteral("10m"));
    click(QStringLiteral("promptOk"));
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));

    // The same profile the keyboard wrote, from the other door.
    const QVariantMap session =
        personNamed(QStringLiteral("tstjulia")).value(QStringLiteral("session")).toMap();
    QCOMPARE(session.value(QStringLiteral("left")).toString(), QStringLiteral("2h10m"));
    QCOMPARE(session.value(QStringLiteral("granted")).toString(), QStringLiteral("10m"));
}

// The sites view, through both doors — docs/design.md §11 and §5.3.
//
// The three things the CLI could do and the window could not: block a site and
// let it open again, put a clock on one, and switch incognito off. They are here
// as a case of their own rather than folded into `theWholeJobOnTheKeyboard`
// because what they write is a different half of the profile, and because the
// interesting assertion is not "the profile changed" but "the window's own
// reading of the machine changed with it" — a row that goes on saying `opens`
// after the rule is written is exactly the failure a snapshot rebuilt from the
// wrong list would produce.
//
// Both doors, in one errand rather than two: the keyboard writes the block and
// the clock, the mouse takes the block back and switches incognito off. Every
// one of the four is a row of the one table, so either door could have done any
// of them, and `everyCommandIsBothAKeyAndAChip` is the general proof of that;
// this is the proof that pressing them writes what the CLI would have written.
void TestStudio::theWebHalfOnBothDoors()
{
    if (!root()->property("operating").toBool())
        QSKIP("not in wheel");

    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("profile"), QStringLiteral("add"), QStringLiteral("tstjulia")});
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));

    // `4`, `b`, the domain, Enter.
    key('4');
    QCOMPARE(root()->property("view").toInt(), 4);
    key('b');
    typeInto(QStringLiteral("promptField"), QStringLiteral("youtube.com"));
    key(Qt::Key_Return);
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));

    QVariantMap site = siteNamed(QStringLiteral("tstjulia"), QStringLiteral("youtube.com"));
    QVERIFY2(!site.isEmpty(), "the site the window just blocked is not on the sites view");
    QVERIFY2(site.value(QStringLiteral("blocked")).toBool(),
             "the row still says the site opens");
    QVERIFY(site.value(QStringLiteral("asked")).toBool());

    // A clock on it. `m` on the row, and the verb underneath has to be
    // `limit --site` and not `limit --budget`: the CLI refuses the second for a
    // domain rather than guessing, so a window that sent the wrong one would
    // fail loudly here and silently nowhere else.
    key('m');
    typeInto(QStringLiteral("promptField"), QStringLiteral("30m"));
    key(Qt::Key_Return);
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));

    site = siteNamed(QStringLiteral("tstjulia"), QStringLiteral("youtube.com"));
    QVERIFY2(site.value(QStringLiteral("hasBudget")).toBool(),
             "the site has no clock, so `m` wrote something else");
    QCOMPARE(site.value(QStringLiteral("limit")).toString(), QStringLiteral("30m"));
    // And the sentence about running out is the site's own. `stops opening` and
    // not `only warns`: a switch whose fourth value fell into the default arm
    // said the second for a whole release.
    QCOMPARE(site.value(QStringLiteral("ending")).toString(), QStringLiteral("stops opening"));

    // A site budget is not a program. It used to draw a row on the programs
    // view, with a verdict read out of the app rules and an `x` that would have
    // written `omahouse deny tstjulia youtube.com`.
    for (const QVariant &row : programsOf(QStringLiteral("tstjulia"))) {
        QVERIFY2(row.toMap().value(QStringLiteral("id")).toString()
                     != QStringLiteral("youtube.com"),
                 "the site budget turned up on the programs view");
    }

    // The other door. The chip takes the block back, and the row says so.
    click(QStringLiteral("command-unblock"));
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));
    site = siteNamed(QStringLiteral("tstjulia"), QStringLiteral("youtube.com"));
    QVERIFY2(!site.value(QStringLiteral("blocked")).toBool(),
             "the site is still blocked after `let it open` came back green");

    // And incognito, which is the one switch with no counterpart on the app
    // side. Three states: this asserts the two the window can move between.
    QVERIFY(!personNamed(QStringLiteral("tstjulia"))
                 .value(QStringLiteral("incognitoStated")).toBool());
    click(QStringLiteral("command-incognito"));
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));
    QVariantMap julia = personNamed(QStringLiteral("tstjulia"));
    QVERIFY(julia.value(QStringLiteral("incognitoStated")).toBool());
    QVERIFY2(julia.value(QStringLiteral("incognitoDenied")).toBool(),
             "the chip said `incognito off` and incognito is still open");

    click(QStringLiteral("command-incognito"));
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));
    QVERIFY(!personNamed(QStringLiteral("tstjulia"))
                 .value(QStringLiteral("incognitoDenied")).toBool());

    // The reach is on the view, once, and it is the CLI's own sentence rather
    // than a second one composed here.
    QQuickItem *reach = itemNamed(window()->contentItem(), QStringLiteral("siteReach"));
    QVERIFY2(reach, "the sites view does not say what a browser policy reaches");
    QVERIFY(reach->isVisible());
    QCOMPARE(reach->property("text").toString(), m_house->reach());
    QVERIFY(m_house->reach().contains(QStringLiteral("including you")));
}

void TestStudio::everyCommandIsBothAKeyAndAChip()
{
    if (!root()->property("operating").toBool())
        QSKIP("not in wheel");

    // Something to have commands about.
    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("profile"), QStringLiteral("add"), QStringLiteral("tstjulia")});
    QVERIFY(waitForWrite());
    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("allow"), QStringLiteral("tstjulia"), QStringLiteral("code"),
                  QStringLiteral("--limit"), QStringLiteral("45m")});
    QVERIFY(waitForWrite());
    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("limit"), QStringLiteral("tstjulia"), QStringLiteral("--session"),
                  QStringLiteral("2h")});
    QVERIFY(waitForWrite());

    // Something to have site commands about, so the fourth view is not an empty
    // list with a cursor at -1 and half its table unusable.
    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("web"), QStringLiteral("block"), QStringLiteral("tstjulia"),
                  QStringLiteral("youtube.com")});
    QVERIFY(waitForWrite());

    // The general form of the promise. Every row of the one table, on all four
    // views: a chip on screen with that id, carrying that key on its face.
    //
    // The bound is four and not three since the sites view arrived, and that is
    // the whole of what this case needed to gain: an action the CLI has and the
    // window does not escapes this proof entirely, because there is no row in
    // the table for it to be a row of. That is how `web block`, `limit --site`
    // and the incognito switch went a whole release without a button.
    // At the narrowest the window says it works. `minimumWidth` is a promise,
    // and the chips are the part of it most likely to be broken by one more
    // command: at the default width there is room to spare and the bar can be
    // wrong for a year without showing it.
    const int wasWide = window()->width();
    window()->setWidth(window()->minimumWidth());
    settle();

    QSet<QString> keysSeen;
    QMap<QString, bool> reachable;
    for (int view = 1; view <= 5; ++view) {
        QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(view)));
        settle();
        const QVariantList commands = root()->property("commands").toList();
        QVERIFY(!commands.isEmpty());
        QSet<QString> keysHere;
        for (const QVariant &row : commands) {
            const QVariantMap command = row.toMap();
            const QString id = command.value(QStringLiteral("id")).toString();
            const QString shortcut = command.value(QStringLiteral("key")).toString();

            QVERIFY2(!shortcut.isEmpty(),
                     qPrintable(QStringLiteral("%1 has no key").arg(id)));
            QVERIFY2(!command.value(QStringLiteral("label")).toString().isEmpty(),
                     qPrintable(QStringLiteral("%1 has no label").arg(id)));

            // No two actions on one view may answer to the same key, or one of
            // them is a chip nothing on the keyboard reaches.
            QVERIFY2(!keysHere.contains(shortcut),
                     qPrintable(QStringLiteral("%1 and something else both answer to %2")
                                    .arg(id, shortcut)));
            keysHere.insert(shortcut);
            keysSeen.insert(shortcut);

            QQuickItem *chip = itemNamed(window()->contentItem(),
                                         QStringLiteral("command-") + id);
            QVERIFY2(chip, qPrintable(QStringLiteral("%1 has no chip to click").arg(id)));
            QCOMPARE(chip->property("key").toString(), shortcut);
            QCOMPARE(chip->property("usable").toBool(),
                     command.value(QStringLiteral("usable")).toBool());

            // Whether the pointer can reach it, which existing is not.
            //
            // The bar clips on purpose -- it says so where it is drawn -- and
            // `:` lists whatever did not fit, so a chip past the right edge is
            // not an unreachable action. What would make it one is the palette
            // itself going the same way, and that is the thing worth holding:
            // the door to everything clipped has to stay clickable at the
            // narrowest width this window says it works at.
            //
            // Asserting instead that every chip is inside the window is what I
            // tried first, and it fails honestly at `minimumWidth` on a layout
            // that is behaving as designed. The omastore window hit the real
            // version of this defect -- a bar with no palette behind it -- and
            // that is what sent me to measure mine.
            const QRectF box = chip->mapRectToScene(
                    QRectF(0, 0, chip->width(), chip->height()));
            reachable.insert(id, box.right() <= window()->width() + 0.5
                                         && box.left() >= -0.5);

            // And the window agrees that the key belongs to that action.
            QVariant found;
            QMetaObject::invokeMethod(root(), "commandFor", Q_RETURN_ARG(QVariant, found),
                                      Q_ARG(QVariant, QVariant(shortcut)));
            QCOMPARE(found.toMap().value(QStringLiteral("id")).toString(), id);
        }
    }

    // The door to whatever the bar could not fit.
    //
    // The first half of this is the one that keeps the second honest: at least
    // one chip has to be clipped at this width, or the check below is a
    // sentence about a situation that never happens. It is clipped -- measured,
    // and the reason this case narrows the window at all.
    //
    // The second half guards something that is structurally safe today: the
    // palette chip is anchored in the header and not inside the clipping row,
    // so narrowing does not move it -- tried at 260 and it stays. What it would
    // catch is somebody putting it in that row, which is the change that would
    // make every clipped action unreachable by pointer at once.
    QVERIFY2(reachable.values().contains(false),
             "nothing was clipped at the narrowest width, so this proves nothing "
             "about what happens when something is");
    QQuickItem *palette = itemNamed(window()->contentItem(),
                                    QStringLiteral("paletteChip"));
    QVERIFY2(palette, "there is no palette chip, so a clipped action has no door");
    const QRectF door = palette->mapRectToScene(
            QRectF(0, 0, palette->width(), palette->height()));
    QVERIFY2(door.right() <= window()->width() + 0.5 && door.left() >= -0.5,
             qPrintable(QStringLiteral("the palette chip sits at %1..%2 and the "
                                       "window is %3 wide, so everything the bar "
                                       "clipped is out of the pointer's reach")
                                .arg(door.left()).arg(door.right())
                                .arg(window()->width())));

    // The window's own keys are not in the table, and they have chips of their
    // own in the header. Same promise, written by hand because they are about
    // the window and not about a profile.
    //
    // Measured here, still narrow, and that is the whole of what this loop
    // gained. It used to run after the width was put back and it used to ask
    // only whether each chip existed -- so the chips nobody thinks to measure,
    // because they are always on screen, were the ones nothing measured. The
    // omastore window had the identical hole and its `keysChip` was the one
    // running past the edge; mine has a `keysChip` too, which is why this is
    // being written rather than reasoned about.
    for (const QString &name : {QStringLiteral("viewChip1"), QStringLiteral("viewChip2"),
                                QStringLiteral("viewChip3"), QStringLiteral("viewChip4"),
                                QStringLiteral("filterChip"), QStringLiteral("paletteChip"),
                                QStringLiteral("keysChip")}) {
        QQuickItem *chip = itemNamed(window()->contentItem(), name);
        QVERIFY2(chip, qPrintable(name));
        QVERIFY2(!chip->property("key").toString().isEmpty(), qPrintable(name));
        const QRectF box = chip->mapRectToScene(
                QRectF(0, 0, chip->width(), chip->height()));
        QVERIFY2(box.left() >= -0.5 && box.right() <= window()->width() + 0.5,
                 qPrintable(QStringLiteral("at %1px wide, %2 runs from %3 to %4")
                                    .arg(window()->width()).arg(name)
                                    .arg(box.left()).arg(box.right())));
    }

    window()->setWidth(wasWide);
    settle();
}

void TestStudio::everyKeyOnTheSheetIsAnswered()
{
    if (!root()->property("operating").toBool())
        QSKIP("not in wheel");

    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("profile"), QStringLiteral("add"), QStringLiteral("tstjulia")});
    QVERIFY(waitForWrite());

    // Every key the sheet promises, and what it takes to keep the promise.
    //
    // The generated half is checked against the table it was generated from. The
    // written half is checked one at a time, because "j moves the cursor" is not
    // a thing a table can say -- and a reference that lists a key nothing
    // answers is worse than no reference, since you stop looking.
    const QVariantList groups = root()->property("keymap").toList();
    QVERIFY(groups.size() >= 3);
    QStringList promised;
    for (const QVariant &row : groups) {
        const QVariantMap group = row.toMap();
        const QVariantList keys = group.value(QStringLiteral("keys")).toList();
        QVERIFY(!keys.isEmpty());
        for (const QVariant &line : keys) {
            const QVariantMap binding = line.toMap();
            QVERIFY(!binding.value(QStringLiteral("key")).toString().isEmpty());
            QVERIFY(!binding.value(QStringLiteral("label")).toString().isEmpty());
            if (group.value(QStringLiteral("title")).toString() == QLatin1String("here, right now"))
                promised << binding.value(QStringLiteral("key")).toString();
        }
    }
    // Guarded, because the list this walks is built by matching a heading, and
    // renaming a heading is an ordinary edit to a help sheet. Empty, the loop
    // below does nothing and the whole claim -- every key the sheet promises is
    // answered -- evaporates with the case still green.
    QVERIFY2(!promised.isEmpty(),
             "the sheet has no `here, right now` group, so nothing below is checked");

    for (const QString &shortcut : std::as_const(promised)) {
        QVariant found;
        QMetaObject::invokeMethod(root(), "commandFor", Q_RETURN_ARG(QVariant, found),
                                  Q_ARG(QVariant, QVariant(shortcut)));
        QVERIFY2(!found.toMap().isEmpty(),
                 qPrintable(QStringLiteral("the sheet promises %1 and nothing answers")
                                .arg(shortcut)));
    }

    // The written half, pressed.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(1)));
    settle();

    key('?');
    QVERIFY(root()->property("blocked").toBool());
    key(Qt::Key_Escape);
    QVERIFY(!root()->property("blocked").toBool());

    key(':');
    QVERIFY(root()->property("blocked").toBool());
    key(Qt::Key_Escape);
    QVERIFY(!root()->property("blocked").toBool());

    key('/');
    QVERIFY(root()->property("filtering").toBool());
    key(Qt::Key_Escape);
    QVERIFY(!root()->property("filtering").toBool());

    key('2');
    QCOMPARE(root()->property("view").toInt(), 2);
    key('3');
    QCOMPARE(root()->property("view").toInt(), 3);
    key('4');
    QCOMPARE(root()->property("view").toInt(), 4);
    key('1');
    QCOMPARE(root()->property("view").toInt(), 1);

    key('l');
    QCOMPARE(root()->property("view").toInt(), 2);
    key('h');
    QCOMPARE(root()->property("view").toInt(), 1);
    key(Qt::Key_Return);
    QCOMPARE(root()->property("view").toInt(), 2);
    key(Qt::Key_Escape);
    QCOMPARE(root()->property("view").toInt(), 1);

    // The cursor keys, on a list long enough to have somewhere to go: the day
    // of a profile with three budgets on it.
    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("limit"), QStringLiteral("tstjulia"), QStringLiteral("--session"),
                  QStringLiteral("2h")});
    QVERIFY(waitForWrite());
    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("allow"), QStringLiteral("tstjulia"), QStringLiteral("code"),
                  QStringLiteral("--limit"), QStringLiteral("45m")});
    QVERIFY(waitForWrite());
    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("allow"), QStringLiteral("tstjulia"), QStringLiteral("firefox"),
                  QStringLiteral("--limit"), QStringLiteral("1h")});
    QVERIFY(waitForWrite());

    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(3)));
    settle();
    QVERIFY(root()->property("rows").toList().size() >= 3);

    QCOMPARE(root()->property("cursor").toInt(), 0);
    key('j');
    QCOMPARE(root()->property("cursor").toInt(), 1);
    key(Qt::Key_Down);
    QCOMPARE(root()->property("cursor").toInt(), 2);
    key('k');
    QCOMPARE(root()->property("cursor").toInt(), 1);
    key(Qt::Key_Up);
    QCOMPARE(root()->property("cursor").toInt(), 0);
    key('G');
    QCOMPARE(root()->property("cursor").toInt(),
             root()->property("rows").toList().size() - 1);
    key('g');
    QCOMPARE(root()->property("cursor").toInt(), 0);
    key(Qt::Key_End);
    QCOMPARE(root()->property("cursor").toInt(),
             root()->property("rows").toList().size() - 1);
    key(Qt::Key_Home);
    QCOMPARE(root()->property("cursor").toInt(), 0);

    // And a click on a row does the same as `j`, which is the other half of the
    // promise for a key whose target is the list itself.
    QQuickItem *second = awaitRow(QStringLiteral("todayList"), 1);
    QVERIFY(second);
    clickItem(second);
    QCOMPARE(root()->property("cursor").toInt(), 1);
}

void TestStudio::aRefusalIsASentenceAndNotASilence()
{
    if (!root()->property("operating").toBool())
        QSKIP("not in wheel");

    // A profile for somebody in wheel is the one thing the CLI refuses outright,
    // and it is the readiest failure to provoke without a password prompt. What
    // is under test is not the refusal -- that is the CLI's, and stage 5 pins it
    // -- but that the window says it out loud instead of doing nothing.
    key('n');
    typeInto(QStringLiteral("promptField"), currentUser());
    key(Qt::Key_Return);
    QVERIFY(!waitForWrite());
    QVERIFY(m_admin->failed());
    QVERIFY2(!m_admin->message().isEmpty(), "a refusal that says nothing is a button that does not work");
    QCOMPARE(people().size(), 0);

    // And it is on the status bar, in the urgent colour, rather than only in a
    // member of a C++ object.
    QQuickItem *line = itemNamed(window()->contentItem(), QStringLiteral("statusMessage"));
    QVERIFY(line);
    QVERIFY(line->isVisible());
    QCOMPARE(line->property("text").toString(), m_admin->message());
}

void TestStudio::saysWhatIsReallyInsideAShim()
{
    if (!root()->property("operating").toBool())
        QSKIP("not in wheel");

    // docs/design.md §5, and the reason this window exists rather than a form: an
    // operator must not be able to release `gtk-launch` without being shown the
    // VS Code inside it.
    //
    // It needs an account with a uid, because the cgroup tree is laid out by
    // one, and an account that is not in wheel, because `profile add` refuses
    // those. `nobody` is both on every machine that has it.
    uid_t uid = 0;
    if (!uidForUser(QStringLiteral("nobody"), &uid))
        QSKIP("this machine has no `nobody` to hang a fake cgroup tree on");

    const QString slice = QStringLiteral("%1/cgroup/user.slice/user-%2.slice/user@%2.service/"
                                         "app.slice/app-graphical.slice")
                              .arg(m_tree.path())
                              .arg(uid);
    makeCgroup(QStringLiteral("%1/app-Hyprland-gtk\\x2dlaunch-91ab.scope").arg(slice),
               {7001, 7002, 7003});
    for (int pid : {7001, 7002, 7003})
        makeProcess(m_tree.path() + QStringLiteral("/proc"), pid,
                    QStringLiteral("/usr/share/code/code"));

    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("profile"), QStringLiteral("add"), QStringLiteral("nobody")});
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));
    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("allow"), QStringLiteral("nobody"),
                  QStringLiteral("gtk-launch")});
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));

    const QVariantList programs = programsOf(QStringLiteral("nobody"));
    QCOMPARE(programs.size(), 1);
    const QVariantMap shim = programs.first().toMap();
    QCOMPARE(shim.value(QStringLiteral("id")).toString(), QStringLiteral("gtk-launch"));
    QVERIFY2(shim.value(QStringLiteral("disagrees")).toBool(),
             "the id and the executable disagree and the window did not notice");
    QCOMPARE(shim.value(QStringLiteral("exe")).toString(), QStringLiteral("/usr/share/code/code"));
    QCOMPARE(shim.value(QStringLiteral("exeCount")).toInt(), 3);

    // And it is on the screen, in the urgent colour, on the row.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(2)));
    settle();
    QQuickItem *said = itemNamed(window()->contentItem(), QStringLiteral("programExe"));
    QVERIFY2(said, "the row does not say what is inside the scope");
    QVERIFY(said->isVisible());
    QVERIFY(said->property("text").toString().contains(QStringLiteral("/usr/share/code/code")));
}

// The window renders, and it renders something.
//
// Everything else in this file asks the state what happened, which a window that
// draws nothing at all would answer correctly -- the keys would work, the
// profile would be written, and there would be a black rectangle where the
// balance is. So this one looks at the pixels, and asks `moreThanOneColour`.
//
// The pictures themselves are not written here. `writesTheOperatorShots` and
// `writesTheSubjectShots` at the foot of this file do that, over every screen
// rather than four of them, and they run the same check on every frame before
// saving it -- which is the whole reason the check is a function and not four
// lines in this case.
QByteArray TestStudio::treeUnder(const QString &directory)
{
    QByteArray fingerprint;
    QDirIterator walk(directory, QDir::Files, QDirIterator::Subdirectories);
    QStringList paths;
    while (walk.hasNext())
        paths << walk.next();
    // Sorted, because the walk order is the filesystem's and two identical
    // trees would otherwise fingerprint differently on the same machine.
    paths.sort();
    for (const QString &path : std::as_const(paths)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        fingerprint += path.toUtf8() + '\n' + file.readAll() + '\n';
        file.close();
    }
    return fingerprint;
}

void TestStudio::theWindowNeverWritesToTheMachine()
{
    if (!root()->property("operating").toBool())
        QSKIP("not in wheel");

    // Every command of every view, performed, with the CLI replaced by something
    // that records and writes nothing. What is asserted afterwards is that the
    // tree is byte for byte what it was: if any command had reached a file
    // itself instead of going through `Admin`, that is where it would show.
    //
    // The window already writes only through `pkexec omahouse <verb>` -- the
    // same CLI an operator would have typed, with the same checks and the same
    // refusals -- and until now nothing held it to that. A second path to the
    // machine would be a second set of refusals to keep in step, and the day it
    // appeared no test would have noticed.
    //
    // The idea is not mine: the omastore window was built on this one's model
    // and turned this argument into an assertion, which is what sent me back to
    // write it here.
    const QString recorder = m_tree.path() + QStringLiteral("/recording-cli");
    const QString recorded = m_tree.path() + QStringLiteral("/recorded");
    {
        QFile file(recorder);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$*\" >> %1\nexit 0\n")
                           .arg(recorded).toUtf8());
        file.close();
        QVERIFY(QFile::setPermissions(recorder,
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                              | QFileDevice::ExeOwner));
    }
    qputenv("OMAHOUSE_CLI", recorder.toLocal8Bit());
    m_admin->setProperty("program", recorder);

    const QByteArray before = treeUnder(m_tree.path() + QStringLiteral("/etc"))
            + treeUnder(m_tree.path() + QStringLiteral("/var"));

    QVariantList performed;
    for (const QVariant &view : QVariantList {0, 1, 2, 3}) {
        QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, view));
        settle();
        const QVariantList commands = root()->property("commands").toList();
        for (const QVariant &row : commands) {
            const QVariantMap command = row.toMap();
            if (!command.value(QStringLiteral("usable")).toBool())
                continue;
            QMetaObject::invokeMethod(root(), "perform", Q_ARG(QVariant, row));
            settle();
            performed.append(command.value(QStringLiteral("id")));
            // Anything that opened a sheet or a field is closed again, so the
            // next command is performed from the same standing start.
            key(Qt::Key_Escape);
            settle();
        }
    }
    QVERIFY2(performed.size() >= 4,
             qPrintable(QStringLiteral("only %1 commands were reachable")
                                .arg(performed.size())));

    const QByteArray after = treeUnder(m_tree.path() + QStringLiteral("/etc"))
            + treeUnder(m_tree.path() + QStringLiteral("/var"));
    QVERIFY2(before == after,
             "a command reached the machine without going through the CLI");

    // And nothing wrote on the way in, which is not the sentence above.
    //
    // Performing a command opens its prompt or its confirmation; the write
    // happens when somebody answers, and this case answers nothing -- it
    // escapes each dialog and moves on. So no call is the correct outcome, and
    // asserting it catches what would otherwise hide here: a command that
    // writes before anybody confirmed.
    //
    // Written this way round because the first version asked whether every
    // recorded call named a verb, and there were no recorded calls -- a loop
    // over an empty list, green, proving nothing. Found by guarding it, which
    // is what the omastore session had just named: what iterates and what
    // asserts a negative do not fail on their own. That the writes which really
    // happen go through the CLI is proved by `theWholeJobOnTheKeyboard`, which
    // answers the dialogs.
    QVERIFY2(!QFile::exists(recorded), "a command wrote before anybody confirmed");

    qputenv("OMAHOUSE_CLI", m_cli.toLocal8Bit());
    m_admin->setProperty("program", m_cli);
}

void TestStudio::drawsItself()
{
    if (!root()->property("operating").toBool())
        QSKIP("not in wheel");

    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("profile"), QStringLiteral("add"), QStringLiteral("tstjulia"),
                  QStringLiteral("--name"), QStringLiteral("Julia")});
    QVERIFY(waitForWrite());
    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("profile"), QStringLiteral("default"),
                  QStringLiteral("tstjulia"), QStringLiteral("--deny")});
    QVERIFY(waitForWrite());
    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("limit"), QStringLiteral("tstjulia"), QStringLiteral("--session"),
                  QStringLiteral("2h")});
    QVERIFY(waitForWrite());
    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("allow"), QStringLiteral("tstjulia"), QStringLiteral("code"),
                  QStringLiteral("--limit"), QStringLiteral("45m")});
    QVERIFY(waitForWrite());
    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("allow"), QStringLiteral("tstjulia"), QStringLiteral("firefox"),
                  QStringLiteral("--limit"), QStringLiteral("1h")});
    QVERIFY(waitForWrite());
    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("grant"), QStringLiteral("tstjulia"), QStringLiteral("--session"),
                  QStringLiteral("10m")});
    QVERIFY(waitForWrite());

    const QStringList frames{QStringLiteral("people"), QStringLiteral("programs"),
                             QStringLiteral("today"), QStringLiteral("keys")};
    for (int i = 0; i < frames.size(); ++i) {
        if (frames.at(i) == QLatin1String("keys")) {
            QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(2)));
            key('?');
        } else {
            QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(i + 1)));
        }
        settle();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

        const QImage frame = window()->grabWindow();
        QVERIFY2(!frame.isNull(), qPrintable(frames.at(i)));
        QVERIFY2(moreThanOneColour(frame),
                 qPrintable(QStringLiteral("%1 is a flat rectangle").arg(frames.at(i))));
    }
    key(Qt::Key_Escape);
}

/// `/` narrows the list it was typed on, and no other.
///
/// The key sheet has said `filter this list` all along and one string was
/// applied to all five at once. What that cost was not a narrower list: the
/// profile the window is about follows the cursor of the people list, so a
/// needle that missed the person emptied the people list -- and then the
/// programs view had nobody to be about and drew *nobody is under rules yet*
/// over a household that was right there.
void TestStudio::theFilterNarrowsOnlyTheListItWasTypedOn()
{
    if (!root()->property("operating").toBool()) QSKIP("operator only");
    seedTheExampleHousehold();
    if (QTest::currentTestFailed())
        return;

    // Nothing is asserted about a narrowing until it is known there was
    // something to narrow. Four programs and one person, or every comparison
    // below is between two empty lists.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(2)));
    const int programs = root()->property("programRows").toList().size();
    QVERIFY2(programs > 1, "the fixture has fewer than two programs to tell apart");
    QCOMPARE(root()->property("peopleRows").toList().size(), 1);
    QCOMPARE(root()->property("subject").toString(), QStringLiteral("nobody"));

    // A needle that names a program and misses the person. `firefox` is not in
    // `nobody` and it is not in `Kid`, which is the whole point of choosing it:
    // this is the needle the old filter could not survive.
    key('/');
    typeInto(QStringLiteral("filterField"), QStringLiteral("firefox"));
    settle();

    const QVariantList narrowed = root()->property("programRows").toList();
    QCOMPARE(narrowed.size(), 1);
    QCOMPARE(narrowed.first().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("firefox"));

    // And the people list is untouched, so the window still has somebody to be
    // about and the view still has rows.
    QCOMPARE(root()->property("peopleRows").toList().size(), 1);
    QCOMPARE(root()->property("subject").toString(), QStringLiteral("nobody"));
    QVERIFY2(!root()->property("todayRows").toList().isEmpty(),
             "a needle typed on the programs list emptied the day");
    QVERIFY2(!root()->property("siteRows").toList().isEmpty(),
             "a needle typed on the programs list emptied the sites");

    key(Qt::Key_Return);
    settle();
    QVERIFY(!root()->property("filtering").toBool());

    // Going somewhere else shows that list's own needle, which is none of it.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(1)));
    QCOMPARE(root()->property("filter").toString(), QString());
    QCOMPARE(root()->property("peopleRows").toList().size(), 1);

    // And coming back finds the list as it was left. "This list" is not a
    // thing that lasts until you look away.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(2)));
    QCOMPARE(root()->property("filter").toString(), QStringLiteral("firefox"));
    QCOMPARE(root()->property("programRows").toList().size(), 1);

    // `Esc` clears the one in front of you, which is what the key sheet says
    // and the only way back to the whole list without a view change.
    key(Qt::Key_Escape);
    settle();
    QCOMPARE(root()->property("filter").toString(), QString());
    QCOMPARE(root()->property("programRows").toList().size(), programs);

    // The other direction, and the one the defect was written about: a needle
    // on the people list narrows the people list, and that is allowed to leave
    // the window with nobody to be about -- because that is what was asked for.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(1)));
    key('/');
    typeInto(QStringLiteral("filterField"), QStringLiteral("zzz"));
    settle();
    QCOMPARE(root()->property("peopleRows").toList().size(), 0);
    key(Qt::Key_Escape);
    settle();
    QCOMPARE(root()->property("peopleRows").toList().size(), 1);
}

void TestStudio::theSubjectFaceHasNothingToPress()
{
    // The read-only face, from the shape of the table rather than by faking an
    // account: with `operating` false the command table is `open` and `back` and
    // nothing else, so there is no action for one door to have and the other to
    // lack. The keys that walk the rows are the window's and are the same either
    // way.
    const QVariantList commands = root()->property("commands").toList();
    QVERIFY(!commands.isEmpty());
    QCOMPARE(commands.first().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("open"));
    QCOMPARE(commands.at(1).toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("back"));
    if (!root()->property("operating").toBool())
        QCOMPARE(commands.size(), 2);
    else
        QVERIFY(commands.size() > 2);
}

// ---------------------------------------------------------------------------
// The inventory of screens, written into docs/img.
//
// `.scripts/shots.sh` runs the two cases below, and `docs/screens.md` is the
// catalogue of what came out. A screenshot pasted in by hand goes stale the
// first time a column moves and nobody notices until somebody reads the page,
// so these are regenerated the way docs/cli.md is and checked the same way:
// `.scripts/shots-check.sh` writes them into a scratch directory and compares.
//
// Two cases and not one because the two faces cannot be reached from one
// account. `$OMAHOUSE_AS` moves what the window reads -- see `readingAs` in
// House.cpp -- and the script runs this binary twice, once as `root` and once
// as `nobody`, which is also what makes the bytes the same on any machine: the
// operator's own name is printed across the header of every frame.
//
// Everything drawn here is a fixture. The profile is written by the shipped
// verbs against a temporary tree, the session is a cgroup tree of this suite's
// own, and the day's ledger is written with the clock spelled out rather than
// taken from `QTime::currentTime` -- a picture with `now` in it is a picture
// whose bytes change every run, and `shots-check` would then refuse a commit
// for a reason that has nothing to do with the commit.
//
// What is *not* here: the notification, the greeter refusing a login, and the
// lock screen. Those are not things this window draws at all, and there is no
// honest way to make it draw them; they come from `vm/shots/`, and
// docs/screens.md says of every picture which of the two it is.

QString TestStudio::shotsDir() const
{
    return qEnvironmentVariable("OMAHOUSE_SHOTS");
}

/// Save the window under that name, once it has stopped moving, and refuse a
/// flat rectangle.
///
/// Grabbed until two grabs in a row agree, with a real wait and a real frame
/// between them. Both halves matter. Every chip in this window fades its fill,
/// its border and its label over 90 milliseconds when the view changes, and
/// those are driven by the animation clock, which advances when time passes and
/// a frame is rendered -- so a `processEvents` that returns the moment the queue
/// is empty advances nothing, and two grabs taken through one agree with each
/// other about a window frozen half way through a transition. The first run of
/// this wrote the people chip still lit on the programs view, and it looked
/// exactly like a broken binding.
void TestStudio::shoot(const QString &name)
{
    const QString directory = shotsDir();
    QVERIFY(!directory.isEmpty());
    QVERIFY2(QDir().mkpath(directory), qPrintable(directory));

    QImage frame = window()->grabWindow();
    QDeadlineTimer deadline(8000);
    forever {
        QTest::qWait(50);
        const QImage again = window()->grabWindow();
        if (again == frame)
            break;
        frame = again;
        if (deadline.hasExpired()) {
            QTest::qFail(qPrintable(QStringLiteral("%1 will not stop moving").arg(name)),
                         __FILE__, __LINE__);
            return;
        }
    }

    QVERIFY2(!frame.isNull(), qPrintable(name));
    // The same question `drawsItself` asks, asked of every picture instead of
    // four of them. An inventory that fills up with empty frames is worse than
    // no inventory: the blank ones look like screens that exist.
    QVERIFY2(moreThanOneColour(frame),
             qPrintable(QStringLiteral("%1 is a flat rectangle").arg(name)));

    // And the two questions a flat rectangle does not ask, because the picture
    // that got past this was neither blank nor flat.
    //
    // `shots-check.sh` compares bytes, so it knows how to notice a picture that
    // is *stale* and has no way at all to notice one that is *wrong*: regenerate
    // and it agrees with whatever came out. A fixture writing a document in a
    // shape the reader had stopped accepting made profiles.json fail to parse,
    // the whole household vanished, and `31-operator-machines.png` became a
    // colourful picture of the words `nobody is under rules yet` -- with the
    // gate green, because the file on disk matched the file just written.
    //
    // So the refusal is here, where the window can still be asked what it
    // thinks. `House.error` is the red line for a file that would not read; a
    // refusal from a *verb* is `Admin.message` and is a thing some pictures are
    // deliberately of, so this never touches those.
    QVERIFY2(m_house->error().isEmpty(),
             qPrintable(QStringLiteral("%1 was taken of a window that could not read the "
                                       "machine: %2").arg(name, m_house->error())));

    // And the quieter half: a household that is on disk and not on screen. The
    // failure above showed up as an empty list, which is a legitimate picture
    // for the *empty* screens and a broken one for every other, and only the
    // file can tell the two apart.
    QVector<Profile> onDisk;
    QString why;
    bool absent = false;
    if (readProfiles(paths::profilesFile(), &onDisk, &why, &absent) && !absent
        && !onDisk.isEmpty()) {
        QVERIFY2(!people().isEmpty(),
                 qPrintable(QStringLiteral("%1 was taken with %2 profiles on disk and none "
                                           "on screen").arg(name).arg(onDisk.size())));
    }
    QVERIFY2(frame.save(directory + QLatin1Char('/') + name + QStringLiteral(".png")),
             qPrintable(name));
}

/// The one example household every picture is about.
///
/// `nobody` because it is a real account on every machine and is never in wheel,
/// so `profile add` accepts it and the row does not carry "no such account on
/// this machine" across every frame. The display name carries the person.
void TestStudio::seedTheExampleHousehold()
{
    uid_t uid = 0;
    QVERIFY2(uidForUser(QStringLiteral("nobody"), &uid),
             "docs/img is drawn about `nobody`, which every machine has and none has "
             "in wheel — and this one has no such account");

    emptyTheHouse();

    // Written by the shipped verbs and not by hand: the pictures are of a
    // profile the CLI would have produced, which is the only kind anybody will
    // ever be looking at.
    const QVector<QStringList> verbs{
        {QStringLiteral("profile"), QStringLiteral("add"), QStringLiteral("nobody"),
         QStringLiteral("--name"), QStringLiteral("Kid")},
        {QStringLiteral("profile"), QStringLiteral("default"), QStringLiteral("nobody"),
         QStringLiteral("--deny")},
        {QStringLiteral("profile"), QStringLiteral("enforce"), QStringLiteral("nobody"),
         QStringLiteral("--on")},
        {QStringLiteral("limit"), QStringLiteral("nobody"), QStringLiteral("--session"),
         QStringLiteral("2h")},
        {QStringLiteral("allow"), QStringLiteral("nobody"), QStringLiteral("code"),
         QStringLiteral("--limit"), QStringLiteral("45m")},
        {QStringLiteral("allow"), QStringLiteral("nobody"), QStringLiteral("firefox"),
         QStringLiteral("--limit"), QStringLiteral("1h")},
        // Released with no clock of its own, and a shim: the picker and the
        // programs list both have to say what is really inside it.
        {QStringLiteral("allow"), QStringLiteral("nobody"), QStringLiteral("gtk-launch")},
        // Named and refused, so the list has a row that is not released.
        {QStringLiteral("deny"), QStringLiteral("nobody"), QStringLiteral("steam")},
        // The web half — docs/design.md §11 and §5.3. Three rows and three
        // different things: a site that does not open at all, a site with a
        // clock on it that is nearly spent, and a site with neither that the
        // day counted minutes against anyway.
        {QStringLiteral("web"), QStringLiteral("block"), QStringLiteral("nobody"),
         QStringLiteral("tiktok.com")},
        {QStringLiteral("limit"), QStringLiteral("nobody"), QStringLiteral("--site"),
         QStringLiteral("youtube.com=30m")},
        {QStringLiteral("web"), QStringLiteral("incognito"), QStringLiteral("nobody"),
         QStringLiteral("--deny")},
    };
    for (const QStringList &verb : verbs) {
        m_admin->run(QStringLiteral("fixture"), verb);
        QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));
    }

    seedTheExampleSession(uid);
    if (QTest::currentTestFailed())
        return;

    // The day, with the clock written down.
    QVector<Profile> profiles;
    QString error;
    QVERIFY2(readProfiles(paths::profilesFile(), &profiles, &error), qPrintable(error));
    QCOMPARE(profiles.size(), 1);
    QString session, code, firefox, youtube;
    for (const Budget &budget : profiles.first().budgets) {
        if (budget.isSession())
            session = budget.id;
        else if (budget.match.contains(QLatin1String("code")))
            code = budget.id;
        else if (budget.match.contains(QLatin1String("firefox")))
            firefox = budget.id;
        else if (budget.isSite() && budget.match.contains(QLatin1String("youtube.com")))
            youtube = budget.id;
    }
    QVERIFY2(!session.isEmpty() && !code.isEmpty() && !firefox.isEmpty() && !youtube.isEmpty(),
             "the verbs above did not write the four budgets the day is drawn from");

    const QDate today = QDate::currentDate();
    const auto at = [today](int hour, int minute) {
        return QDateTime(today, QTime(hour, minute));
    };

    Ledger ledger;
    ledger.user = QStringLiteral("nobody");
    ledger.date = today;
    ledger.seconds.insert(session, 70 * 60);
    ledger.seconds.insert(code, 25 * 60);
    // Exactly its limit, so one row is drawn in the colour of a budget that has
    // run out. Nothing else in this window looks like that.
    ledger.seconds.insert(firefox, 60 * 60);
    // The site's clock and the site's minutes, and they agree because on a real
    // machine one statement writes both -- docs/design.md §5.2. `wikipedia.org`
    // has no budget and no rule and is here anyway: the sites view is the day's
    // browsing as much as it is the rules, and a domain nobody has an opinion
    // about is the ordinary case.
    ledger.seconds.insert(youtube, 25 * 60);
    ledger.sites.insert(QStringLiteral("youtube.com"), 25 * 60);
    ledger.sites.insert(QStringLiteral("wikipedia.org"), 8 * 60);
    // And the presence beside them, never inside them. Two thirds of the day in
    // front of the screen and one third with it dark, which is what makes the
    // line over the sites view worth reading: 25m on youtube.com out of an
    // afternoon somebody remembers as longer is answered by the 40m below.
    ledger.presence.insert(QStringLiteral("using"), 70 * 60);
    ledger.presence.insert(QStringLiteral("screen-off"), 40 * 60);
    ledger.grants.append({at(9, 12), QStringLiteral("root"), session, 10});
    ledger.events.append({at(9, 40), EventKind::Warn, firefox, QString(), 5});
    ledger.events.append({at(9, 45), EventKind::Exhausted, firefox, QString(), -1});
    ledger.events.append({at(9, 50), EventKind::Denied, QString(),
                          QStringLiteral("app-Hyprland-steam-2f4c.scope"), -1});
    QVERIFY(QDir().mkpath(paths::userStateDir(QStringLiteral("nobody"))));
    QVERIFY2(writeLedger(paths::ledgerFile(QStringLiteral("nobody"), today), ledger, &error),
             qPrintable(error));

    // The seeding said things on the status bar. None of them belong in a
    // picture of the window at rest.
    m_admin->clear();
    m_house->reload();
    settle();
}

/// A session for that account: three app scopes and the compositor's own unit.
///
/// One scope agrees with its id, one is a launcher shim holding something else
/// entirely -- docs/design.md §5, the reason this window exists rather than a form --
/// and one is a `tmux-spawn` scope the parser cannot name at all.
void TestStudio::seedTheExampleSession(uid_t uid)
{
    const QString user = QStringLiteral("%1/cgroup/user.slice/user-%2.slice/user@%2.service")
                             .arg(m_tree.path())
                             .arg(uid);
    // Whatever an earlier case hung here, gone: two fixtures in one tree draw
    // one picture between them.
    QDir(user).removeRecursively();

    const QString apps = user + QStringLiteral("/app.slice/app-graphical.slice");
    makeCgroup(apps + QStringLiteral("/app-Hyprland-code-1a2b.scope"), {8101, 8102});
    makeCgroup(apps + QStringLiteral("/app-Hyprland-gtk\\x2dlaunch-91ab.scope"),
               {8201, 8202, 8203});
    makeCgroup(apps + QStringLiteral("/tmux-spawn-8d371e9b-645e-4030-a0d1-2243708321e2.scope"),
               {8301, 8302, 8303, 8304});
    makeCgroup(user + QStringLiteral("/session.slice/wayland-wm@hyprland.desktop.service"),
               {8401, 8402, 8403, 8404, 8405, 8406, 8407});

    const QString procRoot = m_tree.path() + QStringLiteral("/proc");
    for (int pid : {8101, 8102, 8201, 8202, 8203})
        makeProcess(procRoot, pid, QStringLiteral("/usr/share/code/code"));
}

void TestStudio::fleetPanelShowsMissingMachinesAndUsesTheKeyboard()
{
    if (!root()->property("operating").toBool()) QSKIP("operator only");
    seedTheExampleHousehold();
    m_admin->run("fixture", {"machine", "add", "station-02"});
    QVERIFY(waitForWrite());
    key('f');
    QCOMPARE(root()->property("view").toInt(), 5);
    const auto rows = root()->property("rows").toList();
    QVERIFY(rows.size() >= 2);
    bool missing = false;
    for (const auto &row : rows) {
        const auto item = row.toMap();
        if (item.value("name").toString() == "station-02") {
            missing = true;
            QCOMPARE(item.value("state").toString(), QStringLiteral("no report today"));
            QCOMPARE(item.value("used").toString(), QStringLiteral("?"));
        }
    }
    QVERIFY(missing);
    QVERIFY(awaitItem("fleetColumns"));
    QMetaObject::invokeMethod(root(), "setFilter",
                              Q_ARG(QVariant, QVariant(QStringLiteral("station-02"))));
    settle();
    QVERIFY(!root()->property("rows").toList().isEmpty());
    for (const auto &row : root()->property("rows").toList())
        QCOMPARE(row.toMap().value("name").toString(), QStringLiteral("station-02"));
    QMetaObject::invokeMethod(root(), "setFilter", Q_ARG(QVariant, QVariant(QString())));
    settle();
    key('j');
    QCOMPARE(root()->property("cursor").toInt(), 1);
    key('+');
    QVERIFY(root()->property("blocked").toBool());
    key(Qt::Key_Escape);
    key('h');
    QCOMPARE(root()->property("view").toInt(), 1);
    // The two views say the same thing about this computer, and they did not.
    // The panel worked its own row out of the household's two numbers, and a
    // grant typed here is in neither of them until the manager next plans -- so
    // the today view said one figure for what was left and the panel beside it
    // said another, about the same machine, on the same screen.
    QString error;
    QVector<Profile> profiles;
    QVERIFY2(readProfiles(paths::profilesFile(), &profiles, &error), qPrintable(error));
    QCOMPARE(profiles.size(), 1);
    QString session;
    for (const Budget &budget : profiles.first().budgets)
        if (budget.isSession()) session = budget.id;
    QVERIFY(!session.isEmpty());

    const QString who = profiles.first().user;
    const QDate today = QDate::currentDate();
    QJsonObject statement{{"credit", 7200}, {"elsewhere", 1200}, {"counted", 0}};
    QJsonObject house;
    for (const Budget &budget : profiles.first().budgets)
        if (budget.hasLimit()) house.insert(budget.id, statement);
    profiles.first().allocation = QJsonObject{{"authority", "manager"}, {"machine", "here"},
        {"user", who}, {"date", today.toString(Qt::ISODate)}, {"revision", 1},
        {"house", house}};
    QVERIFY2(writeProfiles(paths::profilesFile(), profiles, &error), qPrintable(error));
    m_house->reload();
    settle();

    const auto leftOnThisMachine = [&](const QString &budgetId) {
        key('f');
        for (const auto &row : root()->property("rows").toList()) {
            const auto item = row.toMap();
            if (item.value("name").toString() == QLatin1String("here")
                    && item.value("id").toString() == budgetId)
                return item.value("left").toString();
        }
        return QString();
    };
    const QString panel = leftOnThisMachine(session);
    QVERIFY2(!panel.isEmpty(), "this computer has no row of its own in the fleet panel");

    // Ten minutes handed over here, with the manager none the wiser. Both
    // numbers move, and they move together.
    m_admin->run("fixture", {"grant", who, "--budget", session + "=10m"});
    QVERIFY(waitForWrite());
    m_house->reload();
    settle();
    const QString afterPanel = leftOnThisMachine(session);
    key('t');
    QString afterToday;
    for (const auto &entry : todayOf(who)) {
        const auto item = entry.toMap();
        if (item.value("id").toString() == session)
            afterToday = item.value("left").toString();
    }
    QVERIFY2(!afterToday.isEmpty(), "the today view has no row for the session");
    QCOMPARE(afterPanel, afterToday);
    QVERIFY2(afterPanel != panel,
             "ten minutes were handed over on this machine and the panel did not move");

    // And a budget that never resets is drawn out of the counter it really
    // spends. Every pot on every computer showed nothing spent and its whole
    // limit left, because the panel read the daily counter for all of them --
    // and a collected day is a whole ledger, so the number was there to read
    // the entire time.
    //
    // Its row is also the one that proves a pot does not go through the
    // household at all: this statement has no entry for it, and the row is
    // right anyway.
    QVector<Profile> withPot;
    QVERIFY2(readProfiles(paths::profilesFile(), &withPot, &error), qPrintable(error));
    Budget jar;
    jar.id = QStringLiteral("pot");
    jar.match = {QStringLiteral("chromium")};
    jar.dailyMinutes = 120;
    jar.resets = Resets::Never;
    jar.onExhausted = OnExhausted::Close;
    withPot.first().budgets.append(jar);
    QVERIFY2(writeProfiles(paths::profilesFile(), withPot, &error), qPrintable(error));

    Ledger spent;
    bool noDay = false;
    QVERIFY2(readLedger(paths::ledgerFile(who, today), &spent, &error, &noDay),
             qPrintable(error));
    spent.user = who;
    spent.date = today;
    spent.addKeptSeconds(QStringLiteral("pot"), 1800);
    QVERIFY2(writeLedger(paths::ledgerFile(who, today), spent, &error), qPrintable(error));
    m_house->reload();
    settle();

    key('f');
    QString potUsed, potLeft;
    for (const auto &row : root()->property("rows").toList()) {
        const auto item = row.toMap();
        if (item.value("name").toString() == QLatin1String("here")
                && item.value("id").toString() == QLatin1String("pot")) {
            potUsed = item.value("used").toString();
            potLeft = item.value("left").toString();
        }
    }
    QCOMPARE(potUsed, QStringLiteral("30m"));
    QCOMPARE(potLeft, QStringLiteral("1h30m"));

    key('f');
    // A subject cannot enter the operator's fleet view through its public go.
    m_admin->run("fixture", {"machine", "remove", "station-02"});
    QVERIFY(waitForWrite());
}

/// The day of the *house*, on the view that says today.
///
/// `session: 2h` is two hours in the household and not two hours per computer
/// — Fleet.h says so and `omahouse house` in a terminal is where it was said.
/// A manager that drew only the machine it is sitting at was a window showing a
/// third of somebody's evening as the whole of it.
///
/// Three things at once, because separately each of them passes on a bug: the
/// total adds the collected day in, the machine that has sent nothing today is
/// named rather than quietly left out of the sum, and a machine that is not a
/// manager says nothing at all.
void TestStudio::theTodayViewAddsUpTheHouse()
{
    if (!root()->property("operating").toBool()) QSKIP("operator only");
    seedTheExampleHousehold();
    if (QTest::currentTestFailed())
        return;

    const QString who = QStringLiteral("nobody");
    const QDate today = QDate::currentDate();

    // This case's own household. An earlier case that failed before its own
    // clean-up would otherwise leave a computer in the list, and the sum below
    // would be about a machine this one never wrote down.
    QString fleetError;
    QVERIFY2(writeMachines(paths::machinesFile(), {}, &fleetError), qPrintable(fleetError));
    QDir(paths::elsewhereDir()).removeRecursively();
    m_house->reload();
    settle();

    // The session budget's id, out of what the verbs wrote. Not a literal: the
    // id is the CLI's to choose and a test that hard-codes it is a test that
    // goes green against a profile nobody has.
    QVector<Profile> profiles;
    QString error;
    QVERIFY2(readProfiles(paths::profilesFile(), &profiles, &error), qPrintable(error));
    QCOMPARE(profiles.size(), 1);
    QString session;
    for (const Budget &budget : profiles.first().budgets) {
        if (budget.isSession())
            session = budget.id;
    }
    QVERIFY2(!session.isEmpty(), "the fixture has no session budget to add up");

    // What the fixture spent here: 70 minutes against a 2h limit with 10
    // minutes handed over, so 2h10m of allowance and 60 minutes left *on this
    // machine*. Read rather than assumed, so that the arithmetic below is
    // about the numbers the window is actually holding.
    const auto sessionRow = [&]() {
        for (const QVariant &row : todayOf(who)) {
            const QVariantMap map = row.toMap();
            if (map.value(QStringLiteral("session")).toBool())
                return map;
        }
        return QVariantMap();
    };
    QVariantMap row = sessionRow();
    QVERIFY2(!row.isEmpty(), "the today view has no session row to add up");
    const int hereSeconds = row.value(QStringLiteral("spentSeconds")).toInt();
    const int allowance = row.value(QStringLiteral("allowanceSeconds")).toInt();
    QCOMPARE(hereSeconds, 70 * 60);
    QCOMPARE(allowance, 130 * 60);

    // Before anything else: a machine that is not a manager says nothing about
    // a house. Asserted first and against the same row the rest of this case
    // reads, so `house` being true later is a change and not a constant.
    QCOMPARE(row.value(QStringLiteral("house")).toBool(), false);

    // Two other computers, one of which has sent a day and one of which has
    // not. The second is the case Fleet.h and `omahouse house` both insist on
    // saying out loud: a total quietly missing a computer is worse than none.
    m_admin->run("fixture", {"machine", "add", "the kitchen laptop"});
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));
    m_admin->run("fixture", {"machine", "add", "the study"});
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));

    Ledger theirs;
    theirs.user = who;
    theirs.date = today;
    theirs.seconds.insert(session, 30 * 60);
    const QString collected = paths::elsewhereLedgerFile(QStringLiteral("the kitchen laptop"),
                                                         who, today);
    QVERIFY(QDir().mkpath(QFileInfo(collected).absolutePath()));
    QVERIFY2(writeLedger(collected, theirs, &error), qPrintable(error));

    // The machines exist and one of them has a day, and the window still says
    // nothing: what turns this on is this machine being the household's
    // console, which is `machine.json` and not the length of `machines.json`.
    m_house->reload();
    settle();
    row = sessionRow();
    QVERIFY(!row.isEmpty());
    QCOMPARE(row.value(QStringLiteral("house")).toBool(), false);

    ThisMachine mine;
    mine.kind = Kind::Manager;
    mine.name = QStringLiteral("the desk");
    mine.since = QDateTime::currentDateTime();
    QVERIFY2(writeThisMachine(paths::thisMachineFile(), mine, &error), qPrintable(error));
    m_house->reload();
    settle();

    row = sessionRow();
    QVERIFY(!row.isEmpty());
    QVERIFY2(row.value(QStringLiteral("house")).toBool(),
             "this machine manages the household and the day is still only its own");
    QCOMPARE(row.value(QStringLiteral("houseSpentSeconds")).toInt(), (70 + 30) * 60);
    QCOMPARE(row.value(QStringLiteral("houseLeftSeconds")).toInt(), 30 * 60);
    QCOMPARE(row.value(QStringLiteral("houseSpent")).toString(), QStringLiteral("1h40m"));
    QCOMPARE(row.value(QStringLiteral("houseLeft")).toString(), QStringLiteral("30m"));
    // What is left here is untouched. The machine goes on enforcing its own
    // number; adding the house up is a thing the window says and not a thing
    // this machine does differently because it was said.
    QCOMPARE(row.value(QStringLiteral("spentSeconds")).toInt(), hereSeconds);
    QCOMPARE(row.value(QStringLiteral("leftSeconds")).toInt(), 60 * 60);

    // Every machine that contributed, named, and the one that did not, named
    // separately. A sum whose parts cannot be read back is a number nobody can
    // check against `omahouse house`.
    const QString where = row.value(QStringLiteral("houseWhere")).toString();
    QVERIFY2(where.contains(QStringLiteral("the kitchen laptop 30m")), qPrintable(where));
    QVERIFY2(where.contains(QStringLiteral("here 1h10m")), qPrintable(where));
    QVERIFY2(!where.contains(QStringLiteral("the study")), qPrintable(where));
    QCOMPARE(row.value(QStringLiteral("notHeardFrom")).toString(),
             QStringLiteral("the study"));

    // And it is on screen, not only in the snapshot.
    key('3');
    QCOMPARE(root()->property("view").toInt(), 3);
    QQuickItem *drawn = awaitItem("todayHouse");
    QVERIFY2(drawn, "the today view draws no household line");
    const QString said = drawn->property("text").toString();
    QVERIFY2(said.contains(QStringLiteral("1h40m")), qPrintable(said));
    QVERIFY2(said.contains(QStringLiteral("the study")), qPrintable(said));

    // Put back, or every case after this one runs on a manager.
    QVERIFY(QFile::remove(paths::thisMachineFile()));
    m_admin->run("fixture", {"machine", "remove", "the kitchen laptop"});
    QVERIFY(waitForWrite());
    m_admin->run("fixture", {"machine", "remove", "the study"});
    QVERIFY(waitForWrite());
    QDir(paths::elsewhereDir()).removeRecursively();
    m_house->reload();
    settle();
}

void TestStudio::writesTheOperatorShots()
{
    if (shotsDir().isEmpty())
        QSKIP("nothing asked for pictures: this suite is about behaviour");
    QVERIFY2(root()->property("operating").toBool(),
             "the operator half of docs/img needs the operator face — run this under "
             "OMAHOUSE_AS=root, which is what .scripts/shots.sh does");

    seedTheExampleHousehold();
    if (QTest::currentTestFailed())
        return;

    // The three views.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(1)));
    shoot(QStringLiteral("01-operator-people"));
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(2)));
    shoot(QStringLiteral("02-operator-programs"));
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(3)));
    shoot(QStringLiteral("03-operator-today"));
    // The machines picture uses the same local day plus an explicitly stale
    // remote observation. A fixed timestamp keeps the generated image stable.
    QVector<Profile> originalProfiles;
    QString fleetError;
    QVERIFY(readProfiles(paths::profilesFile(), &originalProfiles, &fleetError));
    auto enrolledProfiles = originalProfiles;
    const QString date = QDate::currentDate().toString(Qt::ISODate);
    // The household's credit, what the other computer has spent of it, and how
    // much of this machine's own credit is already inside the first number.
    // `station-02` has spent 40 minutes of the session, so this machine is told
    // `elsewhere: 2400` and works out the same balance station-02 does.
    //
    // The session's credit is 7800 and not 7200 because the fixture's own day
    // has ten minutes handed over in it, and the household has folded them in.
    // So `counted` is 600 there and zero everywhere else -- a statement saying
    // it had counted none of them while carrying them inside `credit` is the
    // one shape that pays a grant twice, and a fixture that held it would put
    // that number in a picture the documentation ships.
    const auto statement = [](int credit, int elsewhere, int counted = 0) {
        return QJsonObject{{"credit", credit}, {"elsewhere", elsewhere},
                           {"counted", counted}};
    };
    QJsonObject document{{"authority", "example"}, {"machine", "here"},
        {"user", "nobody"}, {"date", date}, {"revision", 1},
        {"house", QJsonObject{{"session", statement(7800, 2400, 600)},
                              {"code", statement(2700, 0)},
                              {"firefox", statement(3600, 0)},
                              {"youtube.com", statement(1800, 0)}}}};
    for (auto &profile : enrolledProfiles)
        if (profile.user == QLatin1String("nobody")) profile.allocation = document;
    QVERIFY(writeProfiles(paths::profilesFile(), enrolledProfiles, &fleetError));
    Machine station; station.name = QStringLiteral("station-02");
    QVERIFY(writeMachines(paths::machinesFile(), {station}, &fleetError));
    Ledger remote;
    remote.user = QStringLiteral("nobody"); remote.date = QDate::currentDate();
    remote.addSeconds(QStringLiteral("session"), 2400);
    document.insert("machine", "station-02");
    // station-02's own statement: the same credit, its own `elsewhere`, and
    // nothing counted from it -- the ten minutes were handed over here.
    document.insert("house", QJsonObject{{"session", statement(7800, 4200)},
                                         {"code", statement(2700, 1500)},
                                         {"firefox", statement(3600, 3600)},
                                         {"youtube.com", statement(1800, 1500)}});
    remote.allocation = document;
    remote.observedAt = QDateTime::fromString(QStringLiteral("2026-09-01T09:30:00"), Qt::ISODate);
    QVERIFY(writeLedger(paths::elsewhereLedgerFile(station.name, remote.user, remote.date), remote, &fleetError));
    m_house->reload();
    key('f');
    QMetaObject::invokeMethod(root(), "setFilter",
                              Q_ARG(QVariant, QVariant(QStringLiteral("session"))));
    shoot(QStringLiteral("31-operator-machines"));
    QMetaObject::invokeMethod(root(), "setFilter", Q_ARG(QVariant, QVariant(QString())));
    QVERIFY(writeProfiles(paths::profilesFile(), originalProfiles, &fleetError));
    QVERIFY(writeMachines(paths::machinesFile(), {}, &fleetError));
    m_house->reload();

    // The window's own three sheets.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(2)));
    key('?');
    shoot(QStringLiteral("04-operator-keys"));
    key(Qt::Key_Escape);

    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(1)));
    key(':');
    shoot(QStringLiteral("05-operator-commands"));
    key(Qt::Key_Escape);

    // Filtered on `o`, which narrows the programs list and nothing else: each
    // list keeps a needle of its own now, so this one leaves the people list
    // whole and the window still has somebody to be about. The needle is kept
    // as it was so the picture is comparable with the ones before it.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(2)));
    key('/');
    typeInto(QStringLiteral("filterField"), QStringLiteral("o"));
    shoot(QStringLiteral("06-operator-filter"));
    key(Qt::Key_Escape);

    // Every question the window asks.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(1)));
    key('n');
    shoot(QStringLiteral("07-operator-new-profile"));
    key(Qt::Key_Escape);

    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(2)));
    key('a');
    shoot(QStringLiteral("08-operator-choose-program"));
    typeInto(QStringLiteral("pickerQuery"), QStringLiteral("fire"));
    key(Qt::Key_Return);
    shoot(QStringLiteral("09-operator-program-limit"));
    key(Qt::Key_Escape);

    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(2)));
    key('m');
    shoot(QStringLiteral("10-operator-minutes"));
    key(Qt::Key_Escape);

    key('+');
    shoot(QStringLiteral("11-operator-more-today"));
    key(Qt::Key_Escape);

    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(3)));
    key('s');
    shoot(QStringLiteral("12-operator-day-total"));
    key(Qt::Key_Escape);

    // The two that cannot be undone by pressing the same key again.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(1)));
    key('x');
    shoot(QStringLiteral("13-operator-forget-profile"));
    key(Qt::Key_Escape);

    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(2)));
    key('x');
    shoot(QStringLiteral("14-operator-drop-program"));
    key(Qt::Key_Escape);

    // The sites, and the two questions they ask. The view carries the reach of a
    // browser policy on its own face, once -- which is the whole of where this
    // window says it, so the two sheets below deliberately do not repeat it.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(4)));
    shoot(QStringLiteral("15-operator-sites"));

    // The palette over it, because that is where the six site commands are read
    // by name with their keys beside them: the visible half of the promise that
    // nothing new arrived on only one of the two doors.
    key(':');
    shoot(QStringLiteral("16-operator-site-commands"));
    key(Qt::Key_Escape);

    // On `tiktok.com`, which this profile already blocks, so the field opens
    // empty. On a row that is not blocked it opens with that row's domain in it.
    key('b');
    shoot(QStringLiteral("17-operator-block-site"));
    key(Qt::Key_Escape);

    // And on `youtube.com`, which has a clock, so the field opens with it.
    key('j');
    key('m');
    shoot(QStringLiteral("18-operator-site-minutes"));
    key(Qt::Key_Escape);

    // A refusal, out of the CLI and not made up here: `root` is in wheel on
    // every machine, and a profile for one of them is the program's one hard
    // refusal. It has to be a sentence on the status bar and not a silence.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(1)));
    key('n');
    typeInto(QStringLiteral("promptField"), QStringLiteral("root"));
    key(Qt::Key_Return);
    QVERIFY2(!waitForWrite(), "the CLI wrote a profile for root");
    shoot(QStringLiteral("19-operator-refusal"));

    // And the four ways this window can be empty, which are four different
    // sentences and not one.
    m_admin->clear();
    emptyTheHouse();
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(1)));
    shoot(QStringLiteral("20-operator-people-empty"));

    m_admin->run(QStringLiteral("fixture"),
                 {QStringLiteral("profile"), QStringLiteral("add"), QStringLiteral("nobody"),
                  QStringLiteral("--name"), QStringLiteral("Kid")});
    QVERIFY2(waitForWrite(), qPrintable(m_admin->message()));
    m_admin->clear();
    settle();
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(2)));
    shoot(QStringLiteral("21-operator-programs-empty"));
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(3)));
    shoot(QStringLiteral("22-operator-today-empty"));
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(4)));
    shoot(QStringLiteral("23-operator-sites-empty"));
}

void TestStudio::writesTheSubjectShots()
{
    if (shotsDir().isEmpty())
        QSKIP("nothing asked for pictures: this suite is about behaviour");
    QVERIFY2(!root()->property("operating").toBool(),
             "the subject half of docs/img needs the read-only face — run this under "
             "OMAHOUSE_AS=nobody, which is what .scripts/shots.sh does");

    seedTheExampleHousehold();
    if (QTest::currentTestFailed())
        return;

    // The same four views, the same household, and nothing to press.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(1)));
    shoot(QStringLiteral("24-subject-people"));
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(2)));
    shoot(QStringLiteral("25-subject-programs"));
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(3)));
    shoot(QStringLiteral("26-subject-today"));
    // The sites view on this face is the one screen in the window that is
    // addressed to the person being measured rather than about them: what does
    // not open, what is left of the half hour, and why the number is smaller
    // than the afternoon felt.
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(4)));
    shoot(QStringLiteral("27-subject-sites"));

    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(2)));
    key('?');
    shoot(QStringLiteral("28-subject-keys"));
    key(Qt::Key_Escape);

    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(1)));
    key(':');
    shoot(QStringLiteral("29-subject-commands"));
    key(Qt::Key_Escape);

    emptyTheHouse();
    QMetaObject::invokeMethod(root(), "go", Q_ARG(QVariant, QVariant(1)));
    shoot(QStringLiteral("30-subject-nothing"));
}

// The quietest text in the window is still text somebody has to read.
//
// `dim` comes from the theme's `muted`, and `muted` is not a colour for text.
// Measured against the twenty-two themes Omarchy ships on this machine, twenty
// put it under 4.5:1 on that theme's own background; `matte-black` is 1.48:1,
// and `last-horizon` sets it to the same value as `selection` -- a colour whose
// job is to sit behind a word rather than to be one. omahouse's own fallback is
// 2.91:1, so this is our defect and not somebody else's.
//
// Real themes, by their real numbers, and the assertion is the ratio and never
// a hex: a colour a household can read is a property, and an expected colour
// would be a copy of the implementation, rewritten every time a theme moves.
void TestStudio::everyQuietWordIsReadableOnEveryTheme()
{
    struct Case { const char *name, *background, *foreground, *muted; };
    static const Case themes[] = {
        {"matte-black",      "#121212", "#bebebe", "#333333"},  // 1.48:1
        {"everforest",       "#2d353b", "#d3c6aa", "#475258"},  // 1.55:1
        {"nord",             "#2e3440", "#d8dee9", "#4c566a"},  // 1.69:1
        {"catppuccin-latte", "#eff1f5", "#4c4f69", "#acb0be"},  // 1.91:1, a light theme
        {"tokyo-night",      "#1a1b26", "#a9b1d6", "#414868"},  // 1.91:1
        {"last-horizon",     "#0c0b0c", "#FAFCFB", "#584e51"},  // 2.45:1, muted == selection
        {"omahouse's own",   "#16161e", "#c0caf5", "#565f89"},  // 2.91:1
    };

    int wereUnreadable = 0;
    for (const Case &theme : themes) {
        const QColor background(QLatin1String(theme.background));
        const QColor foreground(QLatin1String(theme.foreground));
        const QColor muted(QLatin1String(theme.muted));
        QVERIFY2(background.isValid() && foreground.isValid() && muted.isValid(),
                 theme.name);

        if (Theme::contrast(muted, background) < Theme::kReadable)
            ++wereUnreadable;

        const QColor quiet = Theme::quietOn(muted, background, foreground);
        const qreal given = Theme::contrast(quiet, background);
        QVERIFY2(given >= Theme::kReadable,
                 qPrintable(QStringLiteral("%1: the quietest word is %2:1 on its "
                                           "own background")
                                    .arg(QLatin1String(theme.name))
                                    .arg(given, 0, 'f', 2)));

        // And it is still the quietest thing on the screen.
        // Strictly quieter, and not merely no louder: a floor that answered
        // `foreground` for everything would satisfy the line above and would
        // have taken the whole distinction away.
        QVERIFY2(Theme::contrast(quiet, background)
                         < Theme::contrast(foreground, background),
                 qPrintable(QStringLiteral("%1: dim is the foreground, so nothing "
                                           "on this screen is quiet any more")
                                    .arg(QLatin1String(theme.name))));
    }

    // Every theme passing because every theme was already fine would be a case
    // that never reaches the floor it exists to measure.
    QCOMPARE(wereUnreadable, int(std::size(themes)));

    // And a theme that asks for something readable is left exactly alone.
    const QColor plain(QStringLiteral("#c0caf5"));
    const QColor dark(QStringLiteral("#16161e"));
    QCOMPARE(Theme::quietOn(plain, dark, plain), plain);
}

QTEST_MAIN(TestStudio)
#include "tst_studio.moc"
