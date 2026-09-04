#pragma once

#include <QString>

namespace omahouse {

// The version the binary was built with, taken from OMAHOUSE_VERSION and so
// from qmake/version.pri. A function rather than a constant because the front
// ends should read it the same way whether they are linked against this library
// or, later, asking it over a verb.
QString omahouseVersion();

} // namespace omahouse
