#include "NodeConfig.h"
#include "Pairing.h"

#include <QtTest>

using namespace omahouse;

/// The one line a person carries between two computers, and the config that
/// line ends up writing.
///
/// Both halves are here because both are the same failure: a household that
/// pairs two machines and finds, an hour later, that they trust each other and
/// cannot find each other. Everything below is about that hour never happening
/// -- a line that arrived in pieces is refused as pieces, and a config missing
/// a field Omakure needs is a config this test names.
class PairingTest : public QObject
{
    Q_OBJECT

private:
    static Pairing anOrdinaryOne()
    {
        Pairing pairing;
        pairing.name = QStringLiteral("the kitchen laptop");
        pairing.nodeId = QStringLiteral("omk1_1c6eeda1420f17b356d");
        pairing.publicKey = QStringLiteral("mHf6yPqk1kZ2fJ0wCq8YnQ");
        pairing.transportCert = QStringLiteral("30820122300d06092a864886f70d01010105");
        pairing.endpoint = QStringLiteral("192.168.1.20:7879");
        return pairing;
    }

private slots:
    void aLineSurvivesTheJourney()
    {
        const Pairing sent = anOrdinaryOne();
        Pairing got;
        QString error;
        QVERIFY2(decodePairing(encodePairing(sent), &got, &error), qPrintable(error));
        QCOMPARE(got.name, sent.name);
        QCOMPARE(got.nodeId, sent.nodeId);
        QCOMPARE(got.publicKey, sent.publicKey);
        QCOMPARE(got.transportCert, sent.transportCert);
        QCOMPARE(got.endpoint, sent.endpoint);
    }

    void aLineIsOneWordWithNothingAShellMinds()
    {
        // It gets pasted into a command line. A `+`, a `/` or an `=` in it is a
        // household finding out that copy and paste is not enough.
        const QString line = encodePairing(anOrdinaryOne());
        QVERIFY(!line.contains(QLatin1Char(' ')));
        QVERIFY(!line.contains(QLatin1Char('+')));
        QVERIFY(!line.contains(QLatin1Char('/')));
        QVERIFY(!line.contains(QLatin1Char('=')));
    }

    void whitespaceAroundItIsNotDamage()
    {
        // A line that came through a terminal, an email or a chat window.
        Pairing got;
        QString error;
        const QString line = QStringLiteral("  ") + encodePairing(anOrdinaryOne())
                + QStringLiteral("\n");
        QVERIFY2(decodePairing(line, &got, &error), qPrintable(error));
        QCOMPARE(got.nodeId, anOrdinaryOne().nodeId);
    }

    void halfALineIsRefusedAsHalfALine()
    {
        // The failure this format exists to catch. Truncation is what a wrapped
        // terminal does, and the alternative to refusing here is trusting half
        // a certificate.
        const QString whole = encodePairing(anOrdinaryOne());
        Pairing got;
        QString error;
        QVERIFY(!decodePairing(whole.left(whole.size() / 2), &got, &error));
        QVERIFY(error.contains(QStringLiteral("damaged")));
    }

    void somethingElseEntirelyIsSaidToBeSomethingElse()
    {
        Pairing got;
        QString error;
        QVERIFY(!decodePairing(QStringLiteral("omk1_1c6eeda1420f17b356d"), &got, &error));
        QVERIFY(error.contains(QStringLiteral("omahouse-pair-1.")));
    }

    void aLineWithNoCertificateIsRefusedByName()
    {
        // Every refusal names its field, because "invalid" is not something a
        // person can act on and "no certificate in it" is.
        Pairing missing = anOrdinaryOne();
        missing.transportCert.clear();
        Pairing got;
        QString error;
        QVERIFY(!decodePairing(encodePairing(missing), &got, &error));
        QVERIFY(error.contains(QStringLiteral("certificate")));
    }

    void aCertificateThatIsNotHexIsNotACertificate()
    {
        QVERIFY(looksLikeCertificate(QStringLiteral("30820122")));
        QVERIFY(!looksLikeCertificate(QStringLiteral("308201")   // odd length
                                      + QStringLiteral("2")));
        QVERIFY(!looksLikeCertificate(QStringLiteral("zzzz")));
        QVERIFY(!looksLikeCertificate(QString()));
    }

    void anAddressNeedsAPortSomethingCouldListenOn()
    {
        QVERIFY(looksLikeEndpoint(QStringLiteral("192.168.1.20:7879")));
        QVERIFY(looksLikeEndpoint(QStringLiteral("laptop.local:7879")));
        // The last colon is the port, because an IPv6 host has colons of its own.
        QVERIFY(looksLikeEndpoint(QStringLiteral("[fd00::1]:7879")));
        QVERIFY(!looksLikeEndpoint(QStringLiteral("192.168.1.20")));
        QVERIFY(!looksLikeEndpoint(QStringLiteral("192.168.1.20:")));
        QVERIFY(!looksLikeEndpoint(QStringLiteral("192.168.1.20:0")));
        QVERIFY(!looksLikeEndpoint(QStringLiteral("192.168.1.20:99999")));
        QVERIFY(!looksLikeEndpoint(QStringLiteral(":7879")));
    }

    // -- the config that line writes -----------------------------------------

    void theConfigCarriesWhatOmakureRefusesToStartWithout()
    {
        NodeConfig config;
        config.displayName = QStringLiteral("the kitchen laptop");
        const QString text = renderNodeConfig(config);
        // Both of these were found by their absence: without them Omakure
        // refuses the file outright, and the refusal names neither.
        QVERIFY(text.contains(QStringLiteral("version = 1")));
        QVERIFY(text.contains(QStringLiteral("[node]")));
    }

    void aMachineWithNoBatteryAcceptsNoCues()
    {
        // The Conductor's side. One that could be cued back is one somebody took.
        NodeConfig config;
        config.displayName = QStringLiteral("the study");
        const QString text = renderNodeConfig(config);
        QVERIFY(text.contains(QStringLiteral("allow_remote_cues = false")));
        QVERIFY(text.contains(QStringLiteral("remote_cue_batteries = []")));
    }

    void aMachineWithABatteryAcceptsCuesForThatBatteryOnly()
    {
        NodeConfig config;
        config.displayName = QStringLiteral("the kitchen laptop");
        config.cueBatteries = {QStringLiteral("omahouse")};
        const QString text = renderNodeConfig(config);
        QVERIFY(text.contains(QStringLiteral("allow_remote_cues = true")));
        QVERIFY(text.contains(QStringLiteral("remote_cue_batteries = [\"omahouse\"]")));
        // And still no loose scripts: the Battery is the whole of what may run.
        QVERIFY(text.contains(QStringLiteral("remote_cue_scripts = []")));
    }

    void thePeersComeOutInTheShapeOmakureReads()
    {
        NodeConfig config;
        config.displayName = QStringLiteral("the study");
        config.staticPeers = {staticPeer(QStringLiteral("omk1_aaa"),
                                         QStringLiteral("192.168.1.20:7879")),
                              staticPeer(QStringLiteral("omk1_bbb"),
                                         QStringLiteral("192.168.1.21:7879"))};
        QVERIFY(renderNodeConfig(config).contains(
                QStringLiteral("static_peers = [\"omk1_aaa@192.168.1.20:7879\", "
                               "\"omk1_bbb@192.168.1.21:7879\"]")));
    }

    void aConfigIsReadBackWhole()
    {
        NodeConfig config;
        config.displayName = QStringLiteral("the kitchen laptop");
        config.apiBind = QStringLiteral("127.0.0.1:8787");
        config.directBind = QStringLiteral("0.0.0.0:7879");
        NodeConfig back;
        QVERIFY(readRenderedNodeConfig(renderNodeConfig(config), &back));
        QCOMPARE(back.displayName, config.displayName);
        QCOMPARE(back.apiBind, config.apiBind);
        // `bind` and `direct_bind` both end in the same six letters, and the
        // reader must not confuse them: it would move the wire to loopback.
        QCOMPARE(back.directBind, config.directBind);
    }

    void aConfigNobodyHereWroteIsNotReadAtAll()
    {
        // Half of one is worse than none: the caller's next act is to rewrite
        // the file, and it would rewrite the machine's address with a default.
        NodeConfig back;
        QVERIFY(!readRenderedNodeConfig(QStringLiteral("version = 1\n[node]\n"), &back));
        QVERIFY(!readRenderedNodeConfig(QString(), &back));
    }

    void thesameConfigIsTheSameBytes()
    {
        // A machine prepared twice is a machine that did not change.
        NodeConfig config;
        config.displayName = QStringLiteral("the kitchen laptop");
        config.cueBatteries = {QStringLiteral("omahouse")};
        QCOMPARE(renderNodeConfig(config), renderNodeConfig(config));
    }
};

#include "tst_pairing.moc"

int runPairingTests(int argc, char **argv)
{
    PairingTest tests;
    return QTest::qExec(&tests, argc, argv);
}
