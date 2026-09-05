#include <QtTest>

#include "Profile.h"
#include "WebPolicy.h"

using namespace omahouse;

namespace {

// A profile is a whole account under rules, and every case here is about one
// field of it, so the builder takes the web half and leaves everything else at
// what `profile add` writes.
Profile person(const QString &user, const Web &web = Web())
{
    Profile profile;
    profile.user = user;
    profile.enabled = true;
    profile.web = web;
    return profile;
}

Web webThat(Verdict defaultVerdict, const QVector<Rule> &rules = {})
{
    Web web;
    web.defaultVerdict = defaultVerdict;
    web.rules = rules;
    return web;
}

Rule block(const QString &domain)
{
    return Rule {domain, Verdict::Deny};
}

Rule open(const QString &domain)
{
    return Rule {domain, Verdict::Allow};
}

} // namespace

// docs/design.md §11, and the whole of it that can be proved without a browser,
// without root and without a disk. What the file holds is arithmetic over a list
// of profiles; where the file goes is `src/sys`, and it is not this suite's
// business.
class WebPolicyTests : public QObject
{
    Q_OBJECT

private slots:
    // The case every machine is in before anybody asks for anything, and the
    // case a machine has to come back to. Not "write an empty policy": no file.
    void noProfileAsksForAnything()
    {
        QVERIFY(!chromiumPolicyFor({}).needed());
        QVERIFY(!chromiumPolicyFor({person(QStringLiteral("julia"))}).needed());
    }

    void oneBlockedSite()
    {
        const ChromiumPolicy policy = chromiumPolicyFor(
            {person(QStringLiteral("julia"),
                    webThat(Verdict::Allow, {block(QStringLiteral("youtube.com"))}))});

        QVERIFY(policy.needed());
        QCOMPARE(policy.blocklist, QStringList {QStringLiteral("youtube.com")});
        QVERIFY(policy.allowlist.isEmpty());
        QVERIFY(!policy.incognitoDenied);

        const QJsonObject written = policy.toJson();
        QCOMPARE(written.keys(), QStringList {QStringLiteral("URLBlocklist")});
        QCOMPARE(written.value(QStringLiteral("URLBlocklist")).toArray().size(), 1);
    }

    // The measured trap, one policy over: `.temp/spike-extension.md` §7 found a
    // `NativeMessagingAllowlist` with no blocklist beside it letting through
    // exactly the host it was meant to keep out. The same is true of
    // `URLAllowlist`, so an allowlist alone is not a policy -- it is a file that
    // makes a machine look managed while it is not, and there is no file.
    void anAllowlistWithNothingBlockedIsNoPolicyAtAll()
    {
        const ChromiumPolicy policy = chromiumPolicyFor(
            {person(QStringLiteral("julia"),
                    webThat(Verdict::Allow, {open(QStringLiteral("wikipedia.org"))}))});

        QVERIFY(!policy.needed());
        QVERIFY(policy.blocklist.isEmpty());
        QVERIFY(policy.allowlist.isEmpty());
        QVERIFY(policy.toJson().isEmpty());
    }

    // `--only-listed`: everything blocked, and the listed sites carved back out
    // of it. The allowlist means something here because there is a blocklist for
    // it to be an exception to.
    void onlyTheListedSitesOpen()
    {
        const ChromiumPolicy policy = chromiumPolicyFor(
            {person(QStringLiteral("julia"),
                    webThat(Verdict::Deny, {open(QStringLiteral("wikipedia.org")),
                                            open(QStringLiteral("scratch.mit.edu"))}))});

        QCOMPARE(policy.blocklist, QStringList {QStringLiteral("*")});
        QCOMPARE(policy.allowlist,
                 (QStringList {QStringLiteral("scratch.mit.edu"),
                               QStringLiteral("wikipedia.org")}));
    }

    // Written the other way round in a file somebody edited by hand. The default
    // verdict is the rule nobody wrote at the end of the list, so a `*` rule
    // *last* is that same rule spelled out -- and it has to land in the same
    // place, without reaching the blocklist as a second literal `*`.
    //
    // Last, and not first, because the first rule that names a site wins and `*`
    // names every site: a `*` at the top of the list is a list with one rule in
    // it. That is the app half's behaviour too, and it is the model rather than
    // a quirk of this file.
    void blockingEverythingByRuleIsTheSameThing()
    {
        const ChromiumPolicy byRule = chromiumPolicyFor(
            {person(QStringLiteral("julia"),
                    webThat(Verdict::Allow, {open(QStringLiteral("wikipedia.org")),
                                             block(QStringLiteral("*"))}))});
        const ChromiumPolicy byDefault = chromiumPolicyFor(
            {person(QStringLiteral("julia"),
                    webThat(Verdict::Deny, {open(QStringLiteral("wikipedia.org"))}))});

        QVERIFY(byRule == byDefault);
        QCOMPARE(byRule.blocklist, QStringList {QStringLiteral("*")});
    }

    // The composition rule of WebPolicy.h, and the reason it is written down:
    // the most restrictive wins, and there is no precedence. julia's block
    // reaches pedro's machine, because there is only one machine.
    void twoProfilesThatDisagreeAboutOneSite()
    {
        const ChromiumPolicy policy = chromiumPolicyFor({
            person(QStringLiteral("julia"),
                   webThat(Verdict::Allow, {block(QStringLiteral("youtube.com"))})),
            person(QStringLiteral("pedro"),
                   webThat(Verdict::Allow, {open(QStringLiteral("youtube.com"))})),
        });

        QCOMPARE(policy.blocklist, QStringList {QStringLiteral("youtube.com")});
        // And never in both lists. Chromium gives the allowlist the tie, so a
        // domain in both is a domain that opens -- which would turn "the most
        // restrictive wins" into its exact opposite.
        QVERIFY(!policy.allowlist.contains(QStringLiteral("youtube.com")));
    }

    // The harder half of the same rule: one profile asking for `--only-listed`
    // puts `*` in front of everybody, and the other profile's sites open only
    // because they are named. A site the second profile never mentioned is shut,
    // which is the cost of there being one file, and it is why `omahouse web`
    // prints who else has a say.
    void oneProfileClosingTheDoorClosesItForEverybody()
    {
        const ChromiumPolicy policy = chromiumPolicyFor({
            person(QStringLiteral("julia"),
                   webThat(Verdict::Deny, {open(QStringLiteral("wikipedia.org"))})),
            person(QStringLiteral("pedro"),
                   webThat(Verdict::Allow, {open(QStringLiteral("github.com"))})),
        });

        QVERIFY(policy.blocklist.contains(QStringLiteral("*")));
        // github.com is named by pedro and falls to julia's `deny` default, so
        // the two disagree and the restrictive one holds.
        QVERIFY(policy.blocklist.contains(QStringLiteral("github.com")));
        QCOMPARE(policy.allowlist, QStringList {QStringLiteral("wikipedia.org")});
    }

    void incognitoDeniedByOneAndAllowedByAnother()
    {
        Web denies;
        denies.incognitoStated = true;
        denies.incognito = Verdict::Deny;
        Web allows;
        allows.incognitoStated = true;
        allows.incognito = Verdict::Allow;

        const ChromiumPolicy policy = chromiumPolicyFor({
            person(QStringLiteral("julia"), denies),
            person(QStringLiteral("pedro"), allows),
        });

        QVERIFY(policy.incognitoDenied);
        QCOMPARE(policy.toJson().value(QStringLiteral("IncognitoModeAvailability")).toInt(), 1);
    }

    // Said `allow` is not the same as said nothing, and neither of them writes
    // `IncognitoModeAvailability: 0`. omahouse being more permissive than it was
    // asked to be -- overriding some other administrator's file to turn
    // incognito back on -- is the one direction this never takes by itself.
    void allowingIncognitoAsksTheBrowserForNothing()
    {
        Web allows;
        allows.incognitoStated = true;
        allows.incognito = Verdict::Allow;

        const ChromiumPolicy policy =
            chromiumPolicyFor({person(QStringLiteral("julia"), allows)});
        QVERIFY(!policy.needed());
        QVERIFY(!policy.toJson().contains(QStringLiteral("IncognitoModeAvailability")));
    }

    // A profile switched off has to stop doing things to the machine, or
    // "disabled" is a word for something still in force.
    void adisabledProfileIsNotConsulted()
    {
        Profile off = person(QStringLiteral("julia"),
                             webThat(Verdict::Allow, {block(QStringLiteral("youtube.com"))}));
        off.enabled = false;
        QVERIFY(!chromiumPolicyFor({off}).needed());
    }

    // The file is compared with what is already on disk to decide whether to
    // write at all, so the same decision read in a different order has to come
    // out the same bytes. Otherwise every verb rewrites the policy forever.
    void theSameDecisionInAnyOrderIsTheSameFile()
    {
        const Profile julia =
            person(QStringLiteral("julia"),
                   webThat(Verdict::Allow, {block(QStringLiteral("youtube.com")),
                                            block(QStringLiteral("tiktok.com"))}));
        const Profile pedro =
            person(QStringLiteral("pedro"),
                   webThat(Verdict::Allow, {block(QStringLiteral("tiktok.com"))}));

        QVERIFY(chromiumPolicyFor({julia, pedro}) == chromiumPolicyFor({pedro, julia}));
        QCOMPARE(chromiumPolicyFor({julia, pedro}).blocklist,
                 (QStringList {QStringLiteral("tiktok.com"), QStringLiteral("youtube.com")}));
    }

    // The undoing path of docs/design.md §11, without uninstalling anything:
    // the last block taken back is a machine with no policy file on it.
    void takingTheLastBlockBackLeavesNothingBehind()
    {
        Profile julia = person(QStringLiteral("julia"),
                               webThat(Verdict::Allow, {block(QStringLiteral("youtube.com"))}));
        QVERIFY(chromiumPolicyFor({julia}).needed());

        julia.web.rules[0].verdict = Verdict::Allow;
        QVERIFY(!chromiumPolicyFor({julia}).needed());
    }

    // The first rule that names a site wins, the same order and for the same
    // reason as the app half: the rules are read as the operator wrote them.
    void theFirstRuleAboutASiteWins()
    {
        const Web web = webThat(Verdict::Allow, {open(QStringLiteral("youtube.com")),
                                                 block(QStringLiteral("youtube.com"))});
        QCOMPARE(web.verdictFor(QStringLiteral("youtube.com")), Verdict::Allow);
        QCOMPARE(web.verdictFor(QStringLiteral("anything.else")), Verdict::Allow);
    }

    // A profile with a web half survives the trip through profiles.json, and a
    // profile without one does not grow an empty `web` on the way -- which is
    // what keeps "never had web rules" and "had them taken away" one state.
    void theWebHalfSurvivesTheFile()
    {
        Profile julia = person(QStringLiteral("julia"),
                               webThat(Verdict::Deny, {block(QStringLiteral("youtube.com")),
                                                       open(QStringLiteral("wikipedia.org"))}));
        julia.web.incognitoStated = true;
        julia.web.incognito = Verdict::Deny;

        const QJsonObject written = julia.toJson();
        QVERIFY(written.contains(QStringLiteral("web")));

        Profile read;
        QString error;
        QVERIFY2(Profile::fromJson(written, &read, &error), qPrintable(error));
        QCOMPARE(read.web.defaultVerdict, Verdict::Deny);
        QCOMPARE(read.web.rules.size(), 2);
        QCOMPARE(read.web.rules.at(0).match, QStringLiteral("youtube.com"));
        QCOMPARE(read.web.rules.at(0).verdict, Verdict::Deny);
        QVERIFY(read.web.incognitoStated);
        QCOMPARE(read.web.incognito, Verdict::Deny);
        QVERIFY(chromiumPolicyFor({read}) == chromiumPolicyFor({julia}));

        QVERIFY(!person(QStringLiteral("pedro")).toJson().contains(QStringLiteral("web")));
    }

    // Every profiles.json written before this existed has no `web` key at all.
    // That is the ordinary state of the file and not a failure, and it must not
    // be guessed into a deny.
    void aProfileWithNoWebKeyIsNotAFailure()
    {
        const QJsonObject old {
            {QStringLiteral("user"), QStringLiteral("julia")},
            {QStringLiteral("default"), QStringLiteral("deny")},
        };
        Profile read;
        QString error;
        QVERIFY2(Profile::fromJson(old, &read, &error), qPrintable(error));
        QVERIFY(!read.web.saysAnything());
        QCOMPARE(read.web.defaultVerdict, Verdict::Allow);
        QVERIFY(!read.web.incognitoStated);
    }

    void aWebHalfThatWillNotParseIsSaidAndNotGuessed()
    {
        QString error;
        Profile read;

        QJsonObject notAnObject {
            {QStringLiteral("user"), QStringLiteral("julia")},
            {QStringLiteral("web"), QStringLiteral("no")},
        };
        QVERIFY(!Profile::fromJson(notAnObject, &read, &error));
        QVERIFY(error.contains(QStringLiteral("web")));

        QJsonObject unknownVerdict {
            {QStringLiteral("user"), QStringLiteral("julia")},
            {QStringLiteral("web"),
             QJsonObject {{QStringLiteral("incognito"), QStringLiteral("sometimes")}}},
        };
        QVERIFY(!Profile::fromJson(unknownVerdict, &read, &error));
        QVERIFY(error.contains(QStringLiteral("sometimes")));
    }

    // -- the sites that ran out today -----------------------------------------

    // A site limit on a machine with no web rules at all. There is no policy
    // file until the time runs out, there is one while it is out, and there is
    // none again once it is not -- which is the same file appearing and going
    // away that taking the last block back does, and it has to be, because a
    // restriction left behind at midnight is one nothing on the machine knows
    // how to lift.
    void aSiteOutOfTimeIsAPolicyOnItsOwn()
    {
        const QVector<Profile> nobodyHasWebRules {person(QStringLiteral("julia"))};
        QVERIFY(!chromiumPolicyFor(nobodyHasWebRules).needed());

        const ChromiumPolicy out =
            chromiumPolicyFor(nobodyHasWebRules, {QStringLiteral("youtube.com")});
        QVERIFY(out.needed());
        QCOMPARE(out.blocklist, QStringList {QStringLiteral("youtube.com")});
        QVERIFY(out.allowlist.isEmpty());

        QVERIFY(!chromiumPolicyFor(nobodyHasWebRules, {}).needed());
    }

    // The clock has the last word. A profile that allows a site is allowing it
    // in general and not for the thirty-first minute, so a site that has run out
    // is blocked -- and it is never also allowlisted, because Chromium gives the
    // allowlist the tie and that would turn the block into its opposite.
    void aSiteOutOfTimeIsBlockedEvenWhereAProfileAllowsIt()
    {
        const Profile julia =
            person(QStringLiteral("julia"),
                   webThat(Verdict::Deny, {open(QStringLiteral("youtube.com")),
                                           open(QStringLiteral("wikipedia.org"))}));

        const ChromiumPolicy lit = chromiumPolicyFor({julia});
        QVERIFY(lit.allowlist.contains(QStringLiteral("youtube.com")));

        const ChromiumPolicy spent =
            chromiumPolicyFor({julia}, {QStringLiteral("youtube.com")});
        QVERIFY(spent.blocklist.contains(QStringLiteral("youtube.com")));
        QVERIFY2(!spent.allowlist.contains(QStringLiteral("youtube.com")),
                 "a site that ran out was left in the allowlist, which opens it");
        // And the rest of the profile is untouched: only the site that ran out
        // moved.
        QVERIFY(spent.allowlist.contains(QStringLiteral("wikipedia.org")));
    }

    // A budget on browsing at all, run out. `*` reaches the file the way
    // `--only-listed` does and not as a domain called star.
    void abudgetOnBrowsingItselfBlocksEverything()
    {
        const ChromiumPolicy spent =
            chromiumPolicyFor({person(QStringLiteral("julia"))}, {QStringLiteral("*")});
        QCOMPARE(spent.blocklist, QStringList {QStringLiteral("*")});
    }

    // The same set in any order is the same file, sites included -- which is
    // what stops a two second loop rewriting the policy forever because two
    // orderings of one decision compare unequal.
    void theSameDayInAnyOrderIsStillTheSameFile()
    {
        const Profile julia = person(QStringLiteral("julia"),
                                     webThat(Verdict::Allow, {block(QStringLiteral("tiktok.com"))}));
        QVERIFY(chromiumPolicyFor({julia}, {QStringLiteral("youtube.com"),
                                            QStringLiteral("archlinux.org")})
                == chromiumPolicyFor({julia}, {QStringLiteral("archlinux.org"),
                                               QStringLiteral("youtube.com")}));
    }

    // -- a budget about a site, in the file -----------------------------------

    // `kind` survives the trip, and it is written only when it is `site`: a
    // `"kind": "app"` on every budget would rewrite every profiles.json on every
    // machine to say what it already said.
    void aSiteBudgetSurvivesTheFileAndAnAppBudgetSaysNothingNew()
    {
        Profile julia = person(QStringLiteral("julia"));
        Budget site;
        site.id = QStringLiteral("youtube.com");
        site.match = QStringLiteral("youtube.com");
        site.selects = Selects::Site;
        site.dailyMinutes = 30;
        site.onExhausted = OnExhausted::Block;
        Budget app;
        app.id = QStringLiteral("chromium");
        app.match = QStringLiteral("chromium");
        app.dailyMinutes = 45;
        app.onExhausted = OnExhausted::Close;
        julia.budgets = {site, app};

        const QJsonObject written = julia.toJson();
        const QJsonArray budgets = written.value(QStringLiteral("budgets")).toArray();
        QCOMPARE(budgets.at(0).toObject().value(QStringLiteral("kind")).toString(),
                 QStringLiteral("site"));
        QVERIFY2(!budgets.at(1).toObject().contains(QStringLiteral("kind")),
                 "an app budget wrote a kind, which rewrites every file on every machine");

        Profile read;
        QString error;
        QVERIFY2(Profile::fromJson(written, &read, &error), qPrintable(error));
        QCOMPARE(read.budgets.at(0).selects, Selects::Site);
        QCOMPARE(read.budgets.at(0).onExhausted, OnExhausted::Block);
        QCOMPARE(read.budgets.at(1).selects, Selects::App);
        QCOMPARE(read.budgets.at(1).onExhausted, OnExhausted::Close);
    }

    // A site with no action named blocks, an app with none warns. Different
    // defaults because they are the honest reading of each: the observing stage
    // for apps is `enforce: false`, and for sites it was the whole of §5.2.
    void aSiteBudgetWithNoActionNamedBlocks()
    {
        const QJsonObject written {
            {QStringLiteral("user"), QStringLiteral("julia")},
            {QStringLiteral("budgets"),
             QJsonArray {QJsonObject {{QStringLiteral("id"), QStringLiteral("youtube.com")},
                                      {QStringLiteral("match"), QStringLiteral("youtube.com")},
                                      {QStringLiteral("kind"), QStringLiteral("site")},
                                      {QStringLiteral("dailyMinutes"), 30}}}},
        };
        Profile read;
        QString error;
        QVERIFY2(Profile::fromJson(written, &read, &error), qPrintable(error));
        QCOMPARE(read.budgets.at(0).onExhausted, OnExhausted::Block);
    }

    // An instruction with nothing on the other end of it is refused where the
    // file is read, and not repaired into something that would run. A `close` on
    // a site names no cgroup and a `block` on an app names no domain, and either
    // one would read back out of `profile show` and never fire.
    void anActionThatCannotHappenToThatKindIsRefused()
    {
        const auto budgetSaying = [](const QString &kind, const QString &action) {
            QJsonObject entry {{QStringLiteral("id"), QStringLiteral("thing")},
                               {QStringLiteral("match"), QStringLiteral("thing")},
                               {QStringLiteral("onExhausted"), action}};
            if (!kind.isEmpty())
                entry.insert(QStringLiteral("kind"), kind);
            return QJsonObject {{QStringLiteral("user"), QStringLiteral("julia")},
                                {QStringLiteral("budgets"), QJsonArray {entry}}};
        };

        Profile read;
        QString error;
        QVERIFY(!Profile::fromJson(budgetSaying(QStringLiteral("site"),
                                                QStringLiteral("close")),
                                   &read, &error));
        QVERIFY(error.contains(QStringLiteral("close")));
        QVERIFY(!Profile::fromJson(budgetSaying(QStringLiteral("site"),
                                                QStringLiteral("logout")),
                                   &read, &error));
        QVERIFY(!Profile::fromJson(budgetSaying(QString(), QStringLiteral("block")), &read,
                                   &error));
        QVERIFY(error.contains(QStringLiteral("block")));

        // And an unknown kind is said rather than guessed at.
        QVERIFY(!Profile::fromJson(budgetSaying(QStringLiteral("website"),
                                                QStringLiteral("warn")),
                                   &read, &error));
        QVERIFY(error.contains(QStringLiteral("website")));

        // `warn` fits both: it is the observing stage of either.
        QVERIFY(Profile::fromJson(budgetSaying(QStringLiteral("site"), QStringLiteral("warn")),
                                  &read, &error));
        QVERIFY(Profile::fromJson(budgetSaying(QString(), QStringLiteral("warn")), &read,
                                  &error));
    }
};

#include "tst_webpolicy.moc"

int runWebPolicyTests(int argc, char **argv)
{
    WebPolicyTests tests;
    return QTest::qExec(&tests, argc, argv);
}
