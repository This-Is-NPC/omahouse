#include "Theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>

#include <optional>

namespace omahouse {
namespace {

QString currentThemePath()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString state = env.value(QStringLiteral("XDG_STATE_HOME"));
    if (state.isEmpty())
        state = QDir::homePath() + QStringLiteral("/.local/state");
    return state + QStringLiteral("/omarchy/current/theme");
}

// The hyprland configs that carry the corner rounding, most specific first.
// `colors.toml` is colours only; the rounding lives on the window manager's
// side of the desktop.
QStringList roundingPaths()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString config = env.value(QStringLiteral("XDG_CONFIG_HOME"));
    if (config.isEmpty())
        config = QDir::homePath() + QStringLiteral("/.config");
    QString omarchy = env.value(QStringLiteral("OMARCHY_PATH"));
    if (omarchy.isEmpty())
        omarchy = QStringLiteral("/usr/share/omarchy");
    return {config + QStringLiteral("/hypr/looknfeel.lua"),
            omarchy + QStringLiteral("/default/hypr/looknfeel.lua")};
}

// `decoration.rounding` out of a Lua config, or nothing at all.
//
// A line beginning `--` is a Lua comment, and the override omarchy ships is the
// whole config commented out -- so a match inside one would report a rounding
// the desktop is not using. A file that says nothing on the subject yields
// nothing rather than zero, which is what lets the caller fall through to the
// next file instead of stopping at the first one that happens to exist.
std::optional<int> readRounding(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    // Anchored, so the groupbar's `gradient_rounding` is a different key.
    static const QRegularExpression key(QStringLiteral("^rounding\\s*=\\s*(\\d+)"));
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QLatin1String("--")))
            continue;
        const QRegularExpressionMatch match = key.match(line);
        if (match.hasMatch())
            return match.captured(1).toInt();
    }
    return std::nullopt;
}

QHash<QString, QString> readFlatToml(const QString &path)
{
    QHash<QString, QString> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return values;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char('[')))
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        const QString key = line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);
        values.insert(key, value);
    }
    return values;
}

QColor colourOr(const QHash<QString, QString> &values, const QString &key,
                const QColor &fallback)
{
    const QColor parsed(values.value(key));
    return parsed.isValid() ? parsed : fallback;
}

QColor mix(const QColor &from, const QColor &toward, qreal amount)
{
    return QColor::fromRgbF(from.redF() + (toward.redF() - from.redF()) * amount,
                            from.greenF() + (toward.greenF() - from.greenF()) * amount,
                            from.blueF() + (toward.blueF() - from.blueF()) * amount);
}

} // namespace

Theme::Theme(QObject *parent)
    : QObject(parent)
    , m_themePath(currentThemePath())
    , m_roundingPaths(roundingPaths())
{
    reload();
    // Both signals re-take the watches before reading. A theme switcher and an
    // editor alike write `colors.toml` by writing a temporary and renaming it
    // over, and a watch follows the old file out: without this the first change
    // is seen and no later one is.
    const auto rearm = [this] {
        watch();
        reload();
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, rearm);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, rearm);
    watch();
}

void Theme::watch()
{
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    const QString colours = m_themePath + QStringLiteral("/colors.toml");
    if (QFile::exists(colours))
        m_watcher.addPath(colours);
    const QFileInfo pointer(m_themePath);
    if (pointer.dir().exists())
        m_watcher.addPath(pointer.absolutePath());
    for (const QString &looknfeel : std::as_const(m_roundingPaths)) {
        if (QFile::exists(looknfeel))
            m_watcher.addPath(looknfeel);
    }
}

void Theme::reload()
{
    // Square when there is no hyprland config to read, which is what omarchy's
    // own default says in any case.
    m_rounding = 0;
    for (const QString &looknfeel : std::as_const(m_roundingPaths)) {
        if (const std::optional<int> found = readRounding(looknfeel)) {
            m_rounding = *found;
            break;
        }
    }

    const QHash<QString, QString> values =
        readFlatToml(m_themePath + QStringLiteral("/colors.toml"));
    if (values.isEmpty()) {
        emit changed();
        return;
    }

    m_dark = values.value(QStringLiteral("mode"), QStringLiteral("dark"))
        != QLatin1String("light");
    m_background = colourOr(values, QStringLiteral("background"), m_background);
    m_foreground = colourOr(values, QStringLiteral("foreground"), m_foreground);
    m_accent = colourOr(values, QStringLiteral("accent"), m_accent);
    m_urgent = colourOr(values, QStringLiteral("red"), m_urgent);
    m_muted = colourOr(values, QStringLiteral("muted"),
                        colourOr(values, QStringLiteral("dark_foreground"), m_muted));
    emit changed();
}

QColor Theme::panel() const
{
    return mix(m_background, m_foreground, 0.05);
}

QColor Theme::sunken() const
{
    return m_dark ? m_background.darker(130) : m_background.darker(112);
}

QColor Theme::line() const
{
    QColor edge = m_foreground;
    edge.setAlphaF(0.18);
    return edge;
}

QColor Theme::dim() const
{
    return m_muted.isValid() ? m_muted : m_foreground.darker(160);
}

QColor Theme::fill(const QColor &role, qreal alpha) const
{
    QColor wash = role;
    wash.setAlphaF(qBound(0.0, alpha, 1.0));
    return wash;
}

} // namespace omahouse
