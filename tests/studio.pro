# The studio's keyboard and its mouse, tested without a screen.
#
# A second test binary rather than more cases in tst_omahouse: this one needs
# QtQuick, a QML engine and the studio's resources, and the model tests should
# not have to drag a window toolkit in behind them to check a JSON file.
OMAHOUSE_PROJECT_DIR = $$PWD
OMAHOUSE_SOURCE_ROOT = $$clean_path($$PWD/..)
include($$PWD/../qmake/layout.pri)

QT       += core gui qml quick testlib
CONFIG   += testcase c++17 console qmltypes

# The same module name and version the studio registers, or the test binary and
# the shipped one would disagree about what `import omahouse` means.
QML_IMPORT_NAME = omahouse
QML_IMPORT_MAJOR_VERSION = 1
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = tst_studio

INCLUDEPATH += $$PWD/../src/core $$PWD/../src/sys $$PWD/../src/studio

SOURCES += \
    tst_studio.cpp \
    ../src/studio/Theme.cpp \
    ../src/studio/House.cpp \
    ../src/studio/Admin.cpp \
    ../src/studio/Catalog.cpp

HEADERS += \
    ../src/studio/Theme.h \
    ../src/studio/House.h \
    ../src/studio/Admin.h \
    ../src/studio/Catalog.h

# The same qrc the studio ships, so the test drives the QML that runs.
RESOURCES += ../src/studio/resources.qrc

# The same archives the studio links rather than a second copy compiled in here,
# for the reason tests.pro gives: a test that builds its own library can pass
# against sources the shipped binary was never built from.
OMAHOUSE_CORE_DIR = $$clean_path($$OMAHOUSE_SOURCE_ROOT/build/src/core)
OMAHOUSE_SYS_DIR  = $$clean_path($$OMAHOUSE_SOURCE_ROOT/build/src/sys)
!exists($$OMAHOUSE_CORE_DIR/libomahousecore.a)|!exists($$OMAHOUSE_SYS_DIR/libomahousesys.a) {
    error("libomahousecore.a and libomahousesys.a are not in build/src. Run mise run build first.")
}

# sys before core on the line: sys calls into core, and a static archive only
# satisfies symbols the archives to its left still want.
LIBS           += -L$$OMAHOUSE_SYS_DIR -lomahousesys -L$$OMAHOUSE_CORE_DIR -lomahousecore
PRE_TARGETDEPS += $$OMAHOUSE_CORE_DIR/libomahousecore.a $$OMAHOUSE_SYS_DIR/libomahousesys.a
