#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace omahouse {

// The omarchy theme, read live: the colours out of the current theme's
// `colors.toml` and the corner rounding out of hyprland's `looknfeel.lua`.
//
// Carried over from omafiles rather than rewritten. Two windows on the same
// desktop that disagree about what the accent colour is are two windows from two
// programs, and the reading is not the interesting part of either of them: a
// flat TOML, a Lua key, and a watcher that re-takes its watches because a theme
// switch writes by renaming over.
class Theme : public QObject
{
    Q_OBJECT
    // A QML singleton rather than a context property: a context property is
    // invisible to `qmllint`, so every use of it is an unqualified access the
    // linter can only warn about. A declared singleton is a type the tooling
    // and the engine agree on, and it is looked up once rather than searched
    // for in the context chain.
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int rounding READ rounding NOTIFY changed)
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor urgent READ urgent NOTIFY changed)
    Q_PROPERTY(QColor panel READ panel NOTIFY changed)
    Q_PROPERTY(QColor sunken READ sunken NOTIFY changed)
    Q_PROPERTY(QColor line READ line NOTIFY changed)
    Q_PROPERTY(QColor dim READ dim NOTIFY changed)
    Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)

public:
    explicit Theme(QObject *parent = nullptr);

    int rounding() const { return m_rounding; }

    QColor background() const { return m_background; }
    QColor foreground() const { return m_foreground; }
    QColor accent() const { return m_accent; }
    QColor urgent() const { return m_urgent; }

    QColor panel() const;
    QColor sunken() const;
    QColor line() const;
    QColor dim() const;

    QString fontFamily() const { return QStringLiteral("monospace"); }
    Q_INVOKABLE QColor fill(const QColor &role, qreal alpha) const;

signals:
    void changed();

private:
    void reload();
    void watch();

    QFileSystemWatcher m_watcher;
    QString m_themePath;
    /// The hyprland configs that carry `decoration.rounding`, most specific
    /// first: the user's override, then omarchy's default.
    QStringList m_roundingPaths;
    int m_rounding = 0;
    bool m_dark = true;
    QColor m_background{"#16161e"};
    QColor m_foreground{"#c0caf5"};
    QColor m_accent{"#7aa2f7"};
    QColor m_urgent{"#f7768e"};
    QColor m_muted{"#565f89"};
};

} // namespace omahouse
