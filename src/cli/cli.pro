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

INCLUDEPATH += $$PWD/../core
SOURCES     += main.cpp

LIBS           += -L$$OUT_PWD/../core -lomahousecore
PRE_TARGETDEPS += $$OUT_PWD/../core/libomahousecore.a
