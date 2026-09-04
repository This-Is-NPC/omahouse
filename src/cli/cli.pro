OMAHOUSE_PROJECT_DIR = $$PWD
OMAHOUSE_SOURCE_ROOT = $$clean_path($$PWD/../..)
include($$PWD/../../qmake/layout.pri)

QT       += core
QT       -= gui
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = omahouse

DESTDIR = $$OUT_PWD/../../bin

INCLUDEPATH += $$PWD/../core $$PWD/../sys
SOURCES     += main.cpp

# sys before core on the line: sys calls into core -- `scopeIdFromUnit` -- and a
# static archive only satisfies symbols the archives to its left still want.
LIBS           += -L$$OUT_PWD/../sys -lomahousesys -L$$OUT_PWD/../core -lomahousecore
PRE_TARGETDEPS += $$OUT_PWD/../sys/libomahousesys.a $$OUT_PWD/../core/libomahousecore.a
