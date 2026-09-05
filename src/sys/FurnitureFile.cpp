#include "FurnitureFile.h"

#include "Paths.h"

#include <QFile>

namespace omahouse {

QStringList furnitureOfThisMachine()
{
    QFile file(paths::furnitureFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QStringList names;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        names.append(line);
    }
    return names;
}

} // namespace omahouse
