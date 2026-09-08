#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QRegularExpression>
#include <QJSValue>

// Whose password the prompt in front of `pkexec omahouse` is asking for.
//
// There is one polkit action, `org.omarchy.omahouse.manage`, and it is the only
// gate in front of the CLI running as root -- the CLI itself does not refuse a
// write from somebody outside wheel. So the two files that decide it are worth a
// suite: `org.omarchy.omahouse.policy`, which says an administrator, and
// `org.omarchy.omahouse.rules`, which says *which* administrator when the person
// asking is one.
//
// The rule is run rather than read. polkit evaluates these files in a JavaScript
// interpreter, and a test that greps for `AUTH_SELF` passes just as happily
// on a rule that returns it for the wrong action, for everybody, or from inside
// a branch that is never taken. QJSEngine is not polkit's own interpreter, but
// the rule is ES5 with two calls in it, and running it is the difference between
// checking what the file says and checking what it does.
class PolkitTest : public QObject {
    Q_OBJECT

private:
    QString m_policy;
    QString m_rules;

    /// The packaging directory of the tree this binary was built from.
    /// `build-tests/core/tst_omahouse` is two levels down from the root, the
    /// same walk `tst_studio` does to find the CLI.
    static QString packagingFile(const QString &name)
    {
        return QFileInfo(QCoreApplication::applicationDirPath()
                         + QStringLiteral("/../../packaging/") + name)
            .absoluteFilePath();
    }

    static QString slurp(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};
        return QString::fromUtf8(file.readAll());
    }

    /// polkit's own `Result` values, which are the strings the `.policy` file
    /// spells its verbs with. Returning `undefined` is `not_handled` too, and
    /// the harness below reports it as exactly that, because a rule that falls
    /// off its own end and a rule that says so out loud mean the same thing to
    /// polkit and must mean the same thing here.
    static QString results()
    {
        return QStringLiteral(
            "var polkit = {"
            "  Result: {"
            "    NO: 'no', YES: 'yes',"
            "    AUTH_SELF: 'auth_self', AUTH_SELF_KEEP: 'auth_self_keep',"
            "    AUTH_ADMIN: 'auth_admin', AUTH_ADMIN_KEEP: 'auth_admin_keep',"
            "    NOT_HANDLED: 'not_handled'"
            "  },"
            "  _rules: [],"
            "  addRule: function (fn) { this._rules.push(fn); },"
            "  addAdminRule: function (fn) { }"
            "};"
            "function __ask(actionId, groups) {"
            "  var action = { id: actionId };"
            "  var subject = {"
            "    isInGroup: function (name) { return groups.indexOf(name) >= 0; }"
            "  };"
            "  for (var i = 0; i < polkit._rules.length; i++) {"
            "    var answer = polkit._rules[i](action, subject);"
            "    if (answer === undefined || answer === polkit.Result.NOT_HANDLED)"
            "      continue;"
            "    return answer;"
            "  }"
            "  return polkit.Result.NOT_HANDLED;"
            "}");
    }

    /// What the shipped rule answers for one action and one subject's groups.
    /// Every failure on the way -- a file that would not parse, a rule that
    /// threw -- is a test failure with the interpreter's own message on it,
    /// never a quiet `not_handled` that would read like a deliberate answer.
    QString ask(const QString &actionId, const QStringList &groups)
    {
        QJSEngine engine;
        const QJSValue harness = engine.evaluate(results());
        if (harness.isError())
            return QStringLiteral("harness: ") + harness.toString();

        const QJSValue loaded = engine.evaluate(m_rules, QStringLiteral("omahouse.rules"));
        if (loaded.isError())
            return QStringLiteral("rules: ") + loaded.toString();

        QJSValue quoted;
        QStringList literals;
        for (const QString &group : groups)
            literals.append(QStringLiteral("'%1'").arg(group));
        quoted = engine.evaluate(QStringLiteral("__ask('%1', [%2])")
                                     .arg(actionId, literals.join(QStringLiteral(", "))));
        if (quoted.isError())
            return QStringLiteral("call: ") + quoted.toString();
        return quoted.toString();
    }

private slots:
    void initTestCase()
    {
        const QString policyPath = packagingFile(QStringLiteral("org.omarchy.omahouse.policy"));
        const QString rulesPath = packagingFile(QStringLiteral("org.omarchy.omahouse.rules"));

        m_policy = slurp(policyPath);
        m_rules = slurp(rulesPath);

        // Read before anything is asserted about them. Every check below is
        // about what is in these two strings, and an unreadable file would make
        // all of them agree about nothing at all.
        QVERIFY2(!m_policy.isEmpty(), qPrintable(QStringLiteral("no policy at %1").arg(policyPath)));
        QVERIFY2(!m_rules.isEmpty(), qPrintable(QStringLiteral("no rules at %1").arg(rulesPath)));
    }

    /// The whole point of the rules file: somebody in wheel is asked for their
    /// own password, not for "an administrator's". On a machine with two people
    /// in wheel, `auth_admin` alone puts up a prompt that does not say whose
    /// password it wants, and takes the other one's.
    ///
    /// `auth_self` and not `auth_self_keep`, asserted rather than left open,
    /// because the two differ in how often the prompt comes up and
    /// docs/screens.md and the VM walkthrough both say every write asks.
    void anAdministratorIsAskedForTheirOwnPassword()
    {
        QCOMPARE(ask(QStringLiteral("org.omarchy.omahouse.manage"),
                     {QStringLiteral("wheel")}),
                 QStringLiteral("auth_self"));
    }

    /// And the hole this rule must not open. The polkit action is the only gate
    /// in front of `pkexec omahouse`, so a blanket `auth_self` would let the
    /// account under rules authorise its own release with its own password.
    /// Outside wheel the rule says nothing and the `.policy` default stands.
    void everybodyElseStillNeedsAnAdministrator()
    {
        QCOMPARE(ask(QStringLiteral("org.omarchy.omahouse.manage"),
                     {QStringLiteral("kid")}),
                 QStringLiteral("not_handled"));
        QCOMPARE(ask(QStringLiteral("org.omarchy.omahouse.manage"), {}),
                 QStringLiteral("not_handled"));
    }

    /// A rules file is machine-wide and is asked about every action on it. This
    /// one answers for ours and steps out of the way of everybody else's, so
    /// installing omahouse does not quietly re-decide who may change the clock.
    void noOtherActionIsAnsweredHere()
    {
        QCOMPARE(ask(QStringLiteral("org.freedesktop.systemd1.manage-units"),
                     {QStringLiteral("wheel")}),
                 QStringLiteral("not_handled"));
        QCOMPARE(ask(QStringLiteral("org.omarchy.omahouse.manage.other"),
                     {QStringLiteral("wheel")}),
                 QStringLiteral("not_handled"));
    }

    /// The fallback the rule leans on. `everybodyElseStillNeedsAnAdministrator`
    /// only means something while the action's own defaults are `auth_admin`:
    /// the rule returning `not_handled` is a rule handing the decision back to
    /// this file, and if this file ever said `auth_self` the handing back would
    /// be the hole rather than the floor.
    void thePolicyUnderneathStillAsksAnAdministrator()
    {
        // The comments are cut out first. This file argues at length about
        // `auth_self` and why it is not used, and a check over the whole text
        // would go red at the paragraph that says so -- which is the vacuity
        // rule from the other end: an assertion that passes and fails for
        // reasons that have nothing to do with what it is about.
        static const QRegularExpression comment(QStringLiteral("<!--.*?-->"),
                                                QRegularExpression::DotMatchesEverythingOption);
        QString said = m_policy;
        said.remove(comment);
        QVERIFY2(said.contains(QStringLiteral("<action id=\"org.omarchy.omahouse.manage\">")),
                 "the action the rules file names is not in the policy");

        for (const QString &element : {QStringLiteral("allow_any"),
                                       QStringLiteral("allow_inactive"),
                                       QStringLiteral("allow_active")}) {
            const QString wanted = QStringLiteral("<%1>auth_admin</%1>").arg(element);
            QVERIFY2(said.contains(wanted),
                     qPrintable(wanted + QStringLiteral(" is not in the policy")));
        }
        QVERIFY2(!said.contains(QStringLiteral("auth_self")),
                 "the policy itself authorises the subject: the account under rules can "
                 "release itself with its own password");
    }
};

int runPolkitTests(int argc, char **argv)
{
    PolkitTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_polkit.moc"
