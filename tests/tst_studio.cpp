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

#include "Admin.h"
#include "Catalog.h"
#include "House.h"
#include "Theme.h"
#include "Paths.h"
#include "Users.h"

#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
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
    QVERIFY2(QFile::link(executable, directory + QStringLiteral("/exe")), qPrintable(directory));
}

} // namespace

class TestStudio : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void opensOnTheFaceOfWhoeverRanIt();
    void theWholeJobOnTheKeyboard();
    void theWholeJobOnTheMouse();
    void everyCommandIsBothAKeyAndAChip();
    void everyKeyOnTheSheetIsAnswered();
    void aRefusalIsASentenceAndNotASilence();
    void saysWhatIsReallyInsideAShim();
    void drawsItself();
    void theSubjectFaceHasNothingToPress();

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
    void emptyTheHouse();
    QVariantList people() const;
    QVariantMap personNamed(const QString &user) const;
    QVariantList programsOf(const QString &user) const;
    QVariantList todayOf(const QString &user) const;

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

    // Both roots somewhere of this suite's own. Not a convenience: it is the
    // seam Paths.h documents -- a run pointed at a tree of its own is writing
    // where it was told to write, and demanding root for that would be demanding
    // root to write in somebody's home directory.
    qputenv("OMAHOUSE_CONFIG_DIR", (root + "/etc").toLocal8Bit());
    qputenv("OMAHOUSE_STATE_DIR", (root + "/var").toLocal8Bit());
    qputenv("OMAHOUSE_CGROUP_ROOT", (root + "/cgroup").toLocal8Bit());
    qputenv("OMAHOUSE_PROC_ROOT", (root + "/proc").toLocal8Bit());
    qputenv("OMAHOUSE_DESKTOP_DIRS", (root + "/share").toLocal8Bit());
    QVERIFY(QDir().mkpath(root + "/etc"));
    QVERIFY(QDir().mkpath(root + "/var"));
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
    root()->setProperty("cursorToday", 0);
    root()->setProperty("filter", QString());
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

QVariantList TestStudio::todayOf(const QString &user) const
{
    return m_house->snapshot().value(QStringLiteral("today")).toMap().value(user).toList();
}

// ---------------------------------------------------------------------------

void TestStudio::opensOnTheFaceOfWhoeverRanIt()
{
    // Nobody chooses it, and there is no switch on screen to look for. It is
    // wheel or it is not -- spec.md §1 -- and this machine's answer is whatever
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

    // The general form of the promise. Every row of the one table, on all three
    // views: a chip on screen with that id, carrying that key on its face.
    QSet<QString> keysSeen;
    for (int view = 1; view <= 3; ++view) {
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

            // And the window agrees that the key belongs to that action.
            QVariant found;
            QMetaObject::invokeMethod(root(), "commandFor", Q_RETURN_ARG(QVariant, found),
                                      Q_ARG(QVariant, QVariant(shortcut)));
            QCOMPARE(found.toMap().value(QStringLiteral("id")).toString(), id);
        }
    }

    // The window's own keys are not in the table, and they have chips of their
    // own in the header. Same promise, written by hand because they are about
    // the window and not about a profile.
    for (const QString &name : {QStringLiteral("viewChip1"), QStringLiteral("viewChip2"),
                                QStringLiteral("viewChip3"), QStringLiteral("filterChip"),
                                QStringLiteral("paletteChip"), QStringLiteral("keysChip")}) {
        QQuickItem *chip = itemNamed(window()->contentItem(), name);
        QVERIFY2(chip, qPrintable(name));
        QVERIFY2(!chip->property("key").toString().isEmpty(), qPrintable(name));
    }
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

    // spec.md §5, and the reason this window exists rather than a form: an
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
// balance is. So this one looks at the pixels, and asks the one question a
// picture can be asked without pinning a layout to a number: is there more than
// one colour in it.
//
// With OMAHOUSE_SHOTS naming a directory it writes the frames there as well, so
// a keyboard-driven window can be looked at without a screen, a session or a
// compositor. Off by default: the suite is about behaviour, and nobody wants a
// test that writes pictures every time it runs.
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

    const QString shots = qEnvironmentVariable("OMAHOUSE_SHOTS");
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
        QSet<QRgb> colours;
        for (int y = 0; y < frame.height() && colours.size() < 8; y += 3) {
            for (int x = 0; x < frame.width() && colours.size() < 8; x += 3)
                colours.insert(frame.pixel(x, y));
        }
        QVERIFY2(colours.size() > 4,
                 qPrintable(QStringLiteral("%1 is a flat rectangle").arg(frames.at(i))));

        if (!shots.isEmpty()) {
            QDir().mkpath(shots);
            QVERIFY(frame.save(shots + QLatin1Char('/') + frames.at(i)
                               + QStringLiteral(".png")));
        }
    }
    key(Qt::Key_Escape);
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

QTEST_MAIN(TestStudio)
#include "tst_studio.moc"
