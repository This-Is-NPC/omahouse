#include "Focus.h"

#include <QSet>
#include <QStringList>

namespace omahouse {

namespace {

/// Suffixes that take three labels rather than two.
///
/// The same list as `THREE_LABEL_SUFFIXES` in `extension/background.js`, and the
/// two are meant to stay the same list. They are not generated from one source
/// because the source would be a build step producing a JavaScript literal, and
/// a build step is a worse thing to keep honest than twenty-six strings; what
/// keeps them honest is that both sides reduce, so a name either side missed
/// arrives reduced by the other and lands in the same row.
const QSet<QString> &threeLabelSuffixes()
{
    static const QSet<QString> suffixes {
        QStringLiteral("com.br"), QStringLiteral("net.br"), QStringLiteral("org.br"),
        QStringLiteral("gov.br"), QStringLiteral("edu.br"), QStringLiteral("art.br"),
        QStringLiteral("blog.br"),
        QStringLiteral("co.uk"), QStringLiteral("org.uk"), QStringLiteral("ac.uk"),
        QStringLiteral("gov.uk"), QStringLiteral("me.uk"),
        QStringLiteral("com.au"), QStringLiteral("net.au"), QStringLiteral("org.au"),
        QStringLiteral("edu.au"),
        QStringLiteral("co.jp"), QStringLiteral("ne.jp"), QStringLiteral("or.jp"),
        QStringLiteral("ac.jp"),
        QStringLiteral("com.ar"), QStringLiteral("com.mx"), QStringLiteral("com.pt"),
        QStringLiteral("com.es"), QStringLiteral("co.in"), QStringLiteral("co.nz"),
        QStringLiteral("co.za"),
    };
    return suffixes;
}

/// The furthest ahead a stamp may be and still be a time rather than a number.
///
/// 2100-01-01 as seconds since the epoch. A line saying `99999999999` is not a
/// clock that is wrong, it is somebody trying the parser, and the answer to it
/// is the same as the answer to a letter: this is not a line.
constexpr qint64 kFurthestStamp = 4102444800LL;

bool isLabelCharacter(QChar character)
{
    const char16_t code = character.unicode();
    return (code >= u'a' && code <= u'z') || (code >= u'0' && code <= u'9')
        || code == u'-';
}

/// An address has no registrable domain to find, and is not a site with a name.
bool looksLikeAnAddress(const QString &host)
{
    if (host.startsWith(QLatin1Char('[')))
        return true;
    const QStringList parts = host.split(QLatin1Char('.'));
    if (parts.size() != 4)
        return false;
    for (const QString &part : parts) {
        if (part.isEmpty() || part.size() > 3)
            return false;
        for (QChar character : part) {
            if (!character.isDigit())
                return false;
        }
    }
    return true;
}

} // namespace

QString registrableDomain(const QString &host)
{
    QString lower = host.toLower();
    // A trailing dot is the same name written absolutely, and `youtube.com.` and
    // `youtube.com` must not be two rows of the day.
    while (lower.endsWith(QLatin1Char('.')))
        lower.chop(1);
    if (lower.isEmpty() || looksLikeAnAddress(lower))
        return lower;

    const QStringList labels = lower.split(QLatin1Char('.'));
    if (labels.size() <= 2)
        return lower;

    const QString lastTwo = labels.mid(labels.size() - 2).join(QLatin1Char('.'));
    if (threeLabelSuffixes().contains(lastTwo))
        return labels.mid(labels.size() - 3).join(QLatin1Char('.'));
    return lastTwo;
}

bool isPlausibleDomain(const QString &candidate)
{
    if (candidate.isEmpty() || candidate.size() > 253)
        return false;
    // At least one dot, so that a single word -- `localhost`, or anything a
    // reduction went wrong on -- is never a row in somebody's day.
    if (!candidate.contains(QLatin1Char('.')))
        return false;

    const QStringList labels = candidate.split(QLatin1Char('.'));
    for (const QString &label : labels) {
        if (label.isEmpty() || label.size() > 63)
            return false;
        if (label.startsWith(QLatin1Char('-')) || label.endsWith(QLatin1Char('-')))
            return false;
        for (QChar character : label) {
            if (!isLabelCharacter(character))
                return false;
        }
    }
    return true;
}

FocusLine parseFocusLine(const QByteArray &line)
{
    FocusLine parsed;

    QByteArray trimmed = line;
    // A carriage return is not part of any field, and a line that picked one up
    // is still the line that was written.
    while (trimmed.endsWith('\r') || trimmed.endsWith(' '))
        trimmed.chop(1);
    if (trimmed.isEmpty() || trimmed.size() > kFocusLongestLine)
        return parsed;

    // Nothing outside printable ASCII. A domain has no business holding a byte
    // above 127, and refusing them here is what keeps a name that would print as
    // an escape sequence out of somebody's terminal when they run `report`.
    for (char byte : trimmed) {
        const unsigned char value = static_cast<unsigned char>(byte);
        if (value < 0x20 || value > 0x7e)
            return parsed;
    }

    const int space = trimmed.indexOf(' ');
    if (space <= 0 || space == trimmed.size() - 1)
        return parsed;

    const QByteArray stamp = trimmed.left(space);
    const QByteArray site = trimmed.mid(space + 1);
    // Exactly two fields. A third would be a line this did not write, and
    // guessing which two of three were meant is guessing.
    if (site.contains(' '))
        return parsed;

    for (char byte : stamp) {
        if (byte < '0' || byte > '9')
            return parsed;
    }
    bool ok = false;
    const qint64 seconds = stamp.toLongLong(&ok);
    if (!ok || seconds <= 0 || seconds >= kFurthestStamp)
        return parsed;

    parsed.at = QDateTime::fromSecsSinceEpoch(seconds);
    if (!parsed.at.isValid())
        return parsed;

    if (site == "-") {
        // The browser saying there is nothing in front. A read line with no site
        // in it, which is not the same as a line that would not read.
        parsed.read = true;
        return parsed;
    }

    const QString domain = registrableDomain(QString::fromLatin1(site));
    if (!isPlausibleDomain(domain))
        return parsed;

    parsed.read = true;
    parsed.site = domain;
    return parsed;
}

FocusLine lastFocusLine(const QByteArray &blob)
{
    const int end = blob.lastIndexOf('\n');
    if (end < 0)
        return {};
    const int start = blob.lastIndexOf('\n', end - 1);
    // `start` is -1 when there is only one newline in the blob, and the line
    // begins at 0 -- which is right for a whole small file and wrong for the
    // tail of a large one, where byte 0 may be the middle of a line. The caller
    // reads more than one line's worth for that reason, and a truncated first
    // line simply does not parse.
    return parseFocusLine(blob.mid(start + 1, end - start - 1));
}

QString siteInFrontOf(const QByteArray &blob, const QDateTime &now, int freshnessSeconds)
{
    const FocusLine line = lastFocusLine(blob);
    if (!line.read || line.site.isEmpty())
        return QString();

    const qint64 age = line.at.secsTo(now);
    // Ahead of the clock: a stamp the daemon has not reached yet. A little of it
    // is two unsynchronised processes; a lot of it is somebody having written the
    // file by hand.
    if (age < -kFocusSkewSeconds)
        return QString();
    if (age > freshnessSeconds)
        return QString();
    return line.site;
}

QByteArray focusLineFor(const QDateTime &at, const QString &site)
{
    const QString name = site.isEmpty() ? QStringLiteral("-") : site;
    return QByteArray::number(at.toSecsSinceEpoch()) + ' ' + name.toLatin1() + '\n';
}

} // namespace omahouse
