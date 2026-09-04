#include "Version.h"

#ifndef OMAHOUSE_VERSION
#error "OMAHOUSE_VERSION is not defined -- qmake/version.pri was not included."
#endif

namespace omahouse {

QString omahouseVersion()
{
    return QStringLiteral(OMAHOUSE_VERSION);
}

} // namespace omahouse
