#pragma once

#include <QString>
#include <QStringList>

namespace omahouse {

// The Omakure `node.toml` a paired machine needs, as text.
//
// omahouse writes this file because there is nothing else that can: Omakure has
// no verb that edits its own config, and the three settings pairing turns on --
// who the peers are, whether Cues are accepted, and from which Battery -- are
// all in it. The alternative is a household editing TOML by hand between two
// machines, which is the step that was measured going wrong most often.
//
// It is rendered rather than patched. A config half of which is somebody's and
// half of which is ours has no owner, and the failure mode is a merge that
// silently drops `static_peers` and leaves two machines that trust each other
// and cannot find each other. So `machine prepare` writes the whole file and
// says that it did.
//
// Pure, and here rather than in `src/sys`, because getting a field wrong is the
// expensive failure and this is the half that can be proved without a machine.

struct NodeConfig {
    /// What this node calls itself in Omakure's own status. The household's
    /// name for the machine, which is not the node id.
    QString displayName;
    /// Where the local HTTP API answers. Loopback, always: this is the door to
    /// everything the node can do, and it is nobody else's business.
    QString apiBind = QStringLiteral("127.0.0.1:8787");
    /// Where the wire listens. Not loopback -- that is the point of it.
    QString directBind = QStringLiteral("0.0.0.0:8788");
    /// `<node id>@<host:port>`, one per peer this machine should reach.
    QStringList staticPeers;
    /// The Batteries whose scripts this machine will run when cued. Empty is the
    /// safe state and is what a machine that gives orders gets: a Conductor that
    /// can be cued back is a Conductor somebody took.
    QStringList cueBatteries;
    /// The household this machine belongs to.
    QString organisation = QStringLiteral("one-household");
};

/// The file, whole. Deterministic: same struct, same bytes, so that a machine
/// prepared twice is a machine that did not change.
QString renderNodeConfig(const NodeConfig &config);

/// The two values omahouse has to know about a config it already wrote:
/// what this machine calls itself, and where its wire listens.
///
/// It reads back rather than parses. This is not a TOML reader and must never
/// become one: it looks for the keys `renderNodeConfig` writes, on their own
/// lines, in the file that function produced. Anything else -- a config
/// somebody wrote by hand, a config from a future version -- comes back false,
/// which is the honest answer, because the caller's next act is to rewrite the
/// file and it needs to know it would be rewriting something of its own.
bool readRenderedNodeConfig(const QString &text, NodeConfig *out);

/// `<node id>@<host:port>`, the one shape `static_peers` takes.
QString staticPeer(const QString &nodeId, const QString &endpoint);

} // namespace omahouse
