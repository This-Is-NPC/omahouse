#pragma once

#include <QString>

namespace omahouse {

// What one machine has to tell another before either will speak to it.
//
// Two computers with no prior channel cannot come to trust each other in one
// move. Omakure checks a peer against an identity it was given beforehand, and
// it is given that identity by a person -- `node trust --node-id ... --public-key
// ... --transport-certificate ...`, four values, one of them a kilobyte of hex.
// Nobody types that twice correctly, and the household that has to is a
// household that gives up and runs one computer.
//
// So the four values travel as one line. It is not encryption and it is not a
// credential: everything in here is public by construction -- a node id, a
// public key, a certificate and an address. What the line buys is that a person
// can carry it between two machines without transcribing it, and that a machine
// receiving it can say "this is not one of ours" instead of failing later on a
// truncated field.
//
// The prefix is a version and it is checked. A pairing line is the one thing in
// omahouse that crosses between two installs that may not be the same build, and
// the failure of a format change here is two machines that trust nothing and
// cannot say why.

struct Pairing {
    /// What the machine calls itself. A hint for the operator, never a key:
    /// the receiving side names the machine in its own words.
    QString name;
    /// `omk1_<hex>`, the identity every later check is against.
    QString nodeId;
    /// The node's public key, as Omakure prints it.
    QString publicKey;
    /// The transport certificate, hex. This is the long one.
    QString transportCert;
    /// `host:port` -- where the wire listens, not where the API does.
    QString endpoint;

    bool isValid() const;
};

/// The one line a person carries. `omahouse-pair-1.<base64url>`.
QString encodePairing(const Pairing &pairing);

/// The other end, with a reason when it will not. Every refusal here names the
/// field, because a pairing line arrives by copy and paste and the whole class
/// of failure is "half of it".
bool decodePairing(const QString &line, Pairing *out, QString *error);

/// Whether a string is the shape Omakure prints a certificate in: hex, even
/// length, and not empty. Separate because `prepare` reads a certificate off
/// disk and wants the same answer before it ever builds a line.
bool looksLikeCertificate(const QString &hex);

/// Whether a string is `host:port` with a port a machine could listen on.
bool looksLikeEndpoint(const QString &endpoint);

} // namespace omahouse
