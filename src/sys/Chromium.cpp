#include "Chromium.h"

#include "Json.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <cerrno>
#include <cstring>

namespace omahouse {

bool writeChromiumPolicy(const QString &path, const ChromiumPolicy &policy, QString *error)
{
    // The core's routine, and not a second one spelled out here: a sibling
    // temporary, fsync, rename, 0644, and the directory synced after. The
    // destination is a parameter for exactly this reason -- who owns the
    // directory is not the atomic write's business.
    return writeJsonAtomically(path, policy.toJson(), error);
}

bool removeChromiumPolicy(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.exists())
        return true;
    if (file.remove())
        return true;
    if (error)
        *error = QStringLiteral("cannot remove %1: %2").arg(path, file.errorString());
    return false;
}

bool chromiumPolicyIsAlready(const QString &path, const ChromiumPolicy &policy)
{
    QJsonObject root;
    bool missing = false;
    if (!readJsonObject(path, &root, nullptr, &missing))
        return false;
    if (missing)
        return !policy.needed();
    return root == policy.toJson();
}

QStringList chromiumPolicyBlocklist(const QString &path)
{
    QJsonObject root;
    bool missing = false;
    if (!readJsonObject(path, &root, nullptr, &missing) || missing)
        return {};
    QStringList blocked;
    for (const QJsonValue &value : root.value(QStringLiteral("URLBlocklist")).toArray()) {
        if (value.isString())
            blocked.append(value.toString());
    }
    return blocked;
}

} // namespace omahouse
