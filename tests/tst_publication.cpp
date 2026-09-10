#include "Publication.h"
#include <QtTest>
using namespace omahouse;
class PublicationTest : public QObject
{
    Q_OBJECT
private slots:
    void states()
    {
        Profile draft;
        draft.user = "kid";
        Published published;
        published.profile = draft;
        Collected collected;
        collected.machine = "laptop";
        QCOMPARE(publicationOf(draft, nullptr, collected, false), Publication::NotPaired);
        QCOMPARE(publicationOf(draft, nullptr, collected, true), Publication::NeverPublished);
        collected.collected = true;
        QCOMPARE(publicationOf(draft, nullptr, collected, true), Publication::NeverPublished);
        collected.has = true;
        collected.profile = draft;
        QCOMPARE(publicationOf(draft, &published, collected, true), Publication::UpToDate);
        collected.profile.writtenBy = "omakure";
        QCOMPARE(publicationOf(draft, &published, collected, true), Publication::UpToDate);
        draft.enabled = false;
        QCOMPARE(publicationOf(draft, &published, collected, true), Publication::Behind);
        collected.profile.enforce = !draft.enforce;
        QCOMPARE(publicationOf(draft, &published, collected, true), Publication::ChangedThere);
        QCOMPARE(publicationOf(draft, nullptr, collected, true), Publication::ChangedThere);
        collected.has = false;
        QCOMPARE(publicationOf(draft, &published, collected, true), Publication::ChangedThere);
    }
    void allocationsAreNotPublicationRules()
    {
        Profile draft;
        draft.user = "kid";
        draft.allocation = QJsonObject { { "authority", "house" }, { "machine", "here" } };
        Published published;
        published.profile = draft;
        Collected collected;
        collected.collected = true;
        collected.has = true;
        collected.profile = draft;
        collected.profile.allocation["machine"] = "laptop";
        QCOMPARE(publicationOf(draft, &published, collected, true), Publication::UpToDate);
        collected.profile.allocation["revision"] = 42;
        QCOMPARE(publicationOf(draft, &published, collected, true), Publication::UpToDate);
        draft.enabled = false;
        QCOMPARE(publicationOf(draft, &published, collected, true), Publication::Behind);
        collected.profile.displayName = "Changed policy";
        QCOMPARE(publicationOf(draft, &published, collected, true), Publication::ChangedThere);
    }
    void standing()
    {
        QCOMPARE(publicationSummary({ { "unpaired", Publication::NotPaired } }), QString("never"));
        QVector<PublicationRow> rows { { "laptop", Publication::NeverPublished },
            { "study", Publication::ChangedThere } };
        const auto result = standingOf(rows);
        QVERIFY(result.unresolved);
        QCOMPARE(result.changedOn, QStringList { "study" });
        QVERIFY(!standingOf({ { "laptop", Publication::NeverPublished } }).unresolved);
    }
    void aDecisionResolvesOnlyWhatWasSeenAndChosen()
    {
        Profile draft;
        draft.user = "kid";
        Published published;
        published.profile = draft;
        Collected collected;
        collected.collected = true;
        collected.has = true;
        collected.profile = draft;
        collected.profile.enabled = false;
        Resolution resolution { collected.profile.toJson(), draft.withoutTheStamp() };
        QCOMPARE(
            publicationOf(draft, &published, collected, true, &resolution), Publication::Behind);
        collected.profile.writtenBy = "another edit";
        QCOMPARE(publicationOf(draft, &published, collected, true, &resolution),
            Publication::ChangedThere);
        collected.profile.writtenBy.clear();
        draft.enforce = !draft.enforce;
        QCOMPARE(publicationOf(draft, &published, collected, true, &resolution),
            Publication::ChangedThere);
    }
    void aPublicationSurvivesItsDocument()
    {
        Published original;
        original.profile.user = "kid";
        original.publishedAt = QDateTime::currentDateTimeUtc();
        Published back;
        QString error;
        QVERIFY2(Published::fromJson(original.toJson(), &back, &error), qPrintable(error));
        QCOMPARE(back.profile.toJson(), original.profile.toJson());
        auto bad = original.toJson();
        bad["schemaVersion"] = 2;
        QVERIFY(!Published::fromJson(bad, &back, &error));
    }
};
int runPublicationTests(int argc, char** argv)
{
    PublicationTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_publication.moc"
