#include "Pairing.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace omahouse {
namespace {

// One version, checked. See the header for why this is not decoration.
const QLatin1String kPrefix("omahouse-pair-1.");

QString stringAt(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toString();
}

} // namespace

bool carriesASecret(const Pairing &pairing)
{
    return !pairing.token.isEmpty();
}

bool looksLikeCertificate(const QString &hex)
{
    if (hex.isEmpty() || hex.size() % 2 != 0)
        return false;
    for (const QChar c : hex) {
        if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9'))
              || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
              || (c >= QLatin1Char('A') && c <= QLatin1Char('F'))))
            return false;
    }
    return true;
}

bool looksLikeEndpoint(const QString &endpoint)
{
    // From the right, because an IPv6 host has colons of its own and the port
    // is always the last one.
    const int colon = endpoint.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || colon == endpoint.size() - 1)
        return false;
    bool ok = false;
    const int port = endpoint.mid(colon + 1).toInt(&ok);
    return ok && port > 0 && port <= 65535;
}

bool Pairing::isValid() const
{
    return !nodeId.isEmpty() && !publicKey.isEmpty()
            && looksLikeCertificate(transportCert) && looksLikeEndpoint(endpoint);
}

QString encodePairing(const Pairing &pairing)
{
    QJsonObject object {
        {QStringLiteral("name"), pairing.name},
        {QStringLiteral("node"), pairing.nodeId},
        {QStringLiteral("key"), pairing.publicKey},
        {QStringLiteral("cert"), pairing.transportCert},
        {QStringLiteral("at"), pairing.endpoint},
    };
    // Left out entirely when there is none, rather than written empty. An
    // invitation and a pairing are then different lengths as well as different
    // contents, which is one more way a line pasted the wrong way round fails
    // early instead of late.
    if (!pairing.apiEndpoint.isEmpty())
        object.insert(QStringLiteral("api"), pairing.apiEndpoint);
    if (!pairing.token.isEmpty())
        object.insert(QStringLiteral("tok"), pairing.token);
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    // Url alphabet and no padding: this line gets pasted into a shell, and `+`
    // and `=` are both things a shell has opinions about.
    return kPrefix
            + QString::fromLatin1(json.toBase64(QByteArray::Base64UrlEncoding
                                                | QByteArray::OmitTrailingEquals));
}

bool decodePairing(const QString &line, Pairing *out, QString *error)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        *error = QStringLiteral("the pairing line is empty");
        return false;
    }
    if (!trimmed.startsWith(kPrefix)) {
        // Named rather than described, because the commonest way to get here is
        // pasting the whole sentence the other machine printed around the line.
        *error = QStringLiteral("this is not a pairing line: it does not begin with "
                                "%1").arg(kPrefix);
        return false;
    }

    const QByteArray json = QByteArray::fromBase64(
            trimmed.mid(kPrefix.size()).toLatin1(), QByteArray::Base64UrlEncoding);
    QJsonParseError parsed {};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parsed);
    if (!document.isObject()) {
        // Truncation lands here, and it is the failure this format exists to
        // catch: a line cut short by a terminal wrap decodes to nothing that
        // parses, and saying so beats trusting half a certificate.
        *error = QStringLiteral("the pairing line is damaged — copy the whole of it, "
                                "in one piece");
        return false;
    }

    const QJsonObject object = document.object();
    Pairing pairing;
    pairing.name = stringAt(object, "name");
    pairing.nodeId = stringAt(object, "node");
    pairing.publicKey = stringAt(object, "key");
    pairing.transportCert = stringAt(object, "cert");
    pairing.endpoint = stringAt(object, "at");
    pairing.apiEndpoint = stringAt(object, "api");
    pairing.token = stringAt(object, "tok");

    // One field at a time, because "invalid" is not something a person can act
    // on and "no certificate in it" is.
    if (pairing.nodeId.isEmpty()) {
        *error = QStringLiteral("the pairing line names no node");
        return false;
    }
    if (pairing.publicKey.isEmpty()) {
        *error = QStringLiteral("the pairing line carries no public key");
        return false;
    }
    if (!looksLikeCertificate(pairing.transportCert)) {
        *error = QStringLiteral("the pairing line carries no usable transport "
                                "certificate");
        return false;
    }
    if (!looksLikeEndpoint(pairing.endpoint)) {
        *error = QStringLiteral("the pairing line has no address to answer at "
                                "(host:port)");
        return false;
    }
    // Both or neither. An address with no bearer is a door nobody can open and
    // a bearer with no address is a key to nowhere; either on its own is a line
    // that lost a field on the way, which is exactly what this refuses.
    if (pairing.apiEndpoint.isEmpty() != pairing.token.isEmpty()) {
        *error = QStringLiteral("the pairing line is missing half of how to read "
                                "that machine");
        return false;
    }
    if (!pairing.apiEndpoint.isEmpty() && !looksLikeEndpoint(pairing.apiEndpoint)) {
        *error = QStringLiteral("the pairing line has no usable address for that "
                                "machine's console");
        return false;
    }

    *out = pairing;
    return true;
}

} // namespace omahouse
