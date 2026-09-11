#include "Furniture.h"

namespace omahouse {

QStringList furnitureOfOmarchy()
{
    // Two, and both were watched doing it: `autostart.lua` launches them, they
    // arrive as `app-uwsm-*.scope`, and the second one is why a session budget
    // started at login. Nothing goes on this list that was not seen on a real
    // machine -- a list padded with what a desktop probably starts is a list
    // that quietly stops counting something the child really opened.
    return {
        QStringLiteral("udiskie"),
        QStringLiteral("omarchy-hyprland-monitor-watch"),
    };
}

bool isFurniture(const QString &scopeId, const QStringList &alsoFurniture)
{
    if (scopeId.isEmpty())
        return false;
    return furnitureOfOmarchy().contains(scopeId) || alsoFurniture.contains(scopeId);
}

} // namespace omahouse
