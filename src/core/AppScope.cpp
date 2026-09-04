#include "AppScope.h"

#include <QByteArray>
#include <QRegularExpression>
#include <QStringList>

namespace omahouse {

namespace {

/// The trailing field systemd appends to keep two launches of one app apart.
/// Hex, and the reason the suffix is stripped before the name is split rather
/// than after: `app-code-3579042.scope` has two fields and the id is the first,
/// while `app-Hyprland-chromium-031bdc27.scope` has three and it is the second.
bool isRandomSuffix(const QString &field)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]+$"));
    return re.match(field).hasMatch();
}

/// Undoes systemd's `\xNN`. Byte by byte, and only then read as UTF-8, because
/// systemd escapes a non-ASCII character as one `\xNN` per byte of it and
/// turning each of those into a character of its own would produce mojibake
/// that still looks like an id.
bool unescape(const QString &escaped, QString *out, QString *error)
{
    QByteArray bytes;
    const QByteArray raw = escaped.toUtf8();
    for (int i = 0; i < raw.size(); ++i) {
        if (raw.at(i) != '\\') {
            bytes.append(raw.at(i));
            continue;
        }
        if (i + 3 >= raw.size() || raw.at(i + 1) != 'x') {
            if (error)
                *error = QStringLiteral("truncated \\x escape");
            return false;
        }
        bool ok = false;
        const int value = QString::fromLatin1(raw.mid(i + 2, 2)).toInt(&ok, 16);
        if (!ok) {
            if (error)
                *error = QStringLiteral("\\x escape that is not two hex digits");
            return false;
        }
        // 0x20 and below is a control character and 0x7f is delete. systemd has
        // no reason to produce either, and letting one through would put a
        // newline or a NUL in an id that ends up in a notification and a report.
        if (value < 0x20 || value == 0x7f) {
            if (error)
                *error = QStringLiteral("\\x escape of a control character");
            return false;
        }
        bytes.append(static_cast<char>(value));
        i += 3;
    }

    const QString decoded = QString::fromUtf8(bytes);
    if (decoded.contains(QChar::ReplacementCharacter)) {
        if (error)
            *error = QStringLiteral("escapes that do not decode as utf-8");
        return false;
    }
    *out = decoded;
    return true;
}

QString refuse(const QString &unit, const QString &why, QString *error)
{
    if (error)
        *error = QStringLiteral("%1 is not an app scope: %2").arg(unit, why);
    return {};
}

} // namespace

QString scopeIdFromUnit(const QString &unit, QString *error)
{
    static const QString prefix = QStringLiteral("app-");
    static const QString suffix = QStringLiteral(".scope");

    if (!unit.startsWith(prefix))
        return refuse(unit, QStringLiteral("it does not start with app-"), error);
    if (!unit.endsWith(suffix))
        return refuse(unit, QStringLiteral("it does not end in .scope"), error);

    const QString body =
        unit.mid(prefix.size(), unit.size() - prefix.size() - suffix.size());
    if (body.isEmpty())
        return refuse(unit, QStringLiteral("it has no name between app- and .scope"), error);

    QStringList fields = body.split(QLatin1Char('-'));
    if (fields.size() < 2 || !isRandomSuffix(fields.constLast())) {
        return refuse(unit, QStringLiteral("it has no trailing launch id"), error);
    }
    fields.removeLast();

    // What is left is the id, or a launcher and the id. Three fields would mean
    // an unescaped '-' inside a name, which systemd does not write, so it is a
    // unit of some other shape and gets refused rather than guessed at.
    if (fields.size() > 2)
        return refuse(unit, QStringLiteral("it has more names than a launcher and an app"), error);

    const QString escaped = fields.constLast();
    if (escaped.isEmpty())
        return refuse(unit, QStringLiteral("it has an empty app name"), error);

    QString id;
    QString why;
    if (!unescape(escaped, &id, &why))
        return refuse(unit, why, error);
    if (id.isEmpty())
        return refuse(unit, QStringLiteral("it has an empty app name"), error);
    return id;
}

} // namespace omahouse
