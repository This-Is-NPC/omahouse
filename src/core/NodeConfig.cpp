#include "NodeConfig.h"

namespace omahouse {
namespace {

/// A TOML string list, quoted. No escaping beyond the quote itself: every value
/// that reaches here is a node id, a host or a Battery name, and a household
/// that has put a newline in one has a problem this function cannot fix.
QString list(const QStringList &values)
{
    QStringList quoted;
    for (const QString &value : values)
        quoted.append(QStringLiteral("\"%1\"").arg(QString(value).remove(QLatin1Char('"'))));
    return quoted.join(QStringLiteral(", "));
}

QString quoted(const QString &value)
{
    return QStringLiteral("\"%1\"").arg(QString(value).remove(QLatin1Char('"')));
}

/// The value of `key = "value"` on its own line, or empty.
QString valueOf(const QString &text, const QString &key)
{
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(key + QStringLiteral(" = \"")))
            continue;
        const int open = trimmed.indexOf(QLatin1Char('"'));
        const int close = trimmed.lastIndexOf(QLatin1Char('"'));
        if (close > open)
            return trimmed.mid(open + 1, close - open - 1);
    }
    return QString();
}

} // namespace

bool readRenderedNodeConfig(const QString &text, NodeConfig *out)
{
    NodeConfig config;
    config.displayName = valueOf(text, QStringLiteral("display_name"));
    config.apiBind = valueOf(text, QStringLiteral("bind"));
    config.directBind = valueOf(text, QStringLiteral("direct_bind"));
    const QString organisation = valueOf(text, QStringLiteral("id"));
    if (!organisation.isEmpty())
        config.organisation = organisation;
    // Both, and not either. A file with a name and no wire is a file this did
    // not write, and half of it is worse than none: the caller would rewrite
    // the machine's address with a default and the household would lose the
    // wire it had.
    if (config.displayName.isEmpty() || config.directBind.isEmpty()
            || config.apiBind.isEmpty())
        return false;
    *out = config;
    return true;
}

QString staticPeer(const QString &nodeId, const QString &endpoint)
{
    return QStringLiteral("%1@%2").arg(nodeId, endpoint);
}

QString renderNodeConfig(const NodeConfig &config)
{
    // `version` and `[node]` first and always present: without them Omakure
    // refuses the file outright, and the refusal names neither.
    return QStringLiteral(
                   "# Written by omahouse. Edits here are replaced the next time a\n"
                   "# machine is paired.\n"
                   "version = 1\n"
                   "\n[node]\n"
                   "display_name = %1\n"
                   "\n[api]\n"
                   "bind = %2\n"
                   "\n[network]\n"
                   "mode = \"direct\"\n"
                   "relays = []\n"
                   "direct_bind = %3\n"
                   "static_peers = [%4]\n"
                   "max_message_bytes = 1048576\n"
                   "\n[trust]\n"
                   "enrollment = \"manual\"\n"
                   "allow_remote_cues = %5\n"
                   "remote_cue_scripts = []\n"
                   "remote_cue_batteries = [%6]\n"
                   "allow_baseline_push = false\n"
                   "baseline_publishers = []\n"
                   "authorities = []\n"
                   "bootstrap_token_hash = \"\"\n"
                   "bootstrap_nonce_hash = \"\"\n"
                   "\n[discovery]\n"
                   "enabled = false\n"
                   "\n[organization]\n"
                   "id = %7\n"
                   "discovery_secret_ref = \"\"\n")
            .arg(quoted(config.displayName), quoted(config.apiBind),
                 quoted(config.directBind), list(config.staticPeers),
                 config.cueBatteries.isEmpty() ? QStringLiteral("false")
                                               : QStringLiteral("true"),
                 list(config.cueBatteries), quoted(config.organisation));
}

} // namespace omahouse
