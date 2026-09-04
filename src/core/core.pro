# The model, and nothing else. No window, no argument parsing, and -- once the
# policy of stage 2 lands here -- no clock and no disk either.
OMAHOUSE_PROJECT_DIR = $$PWD
OMAHOUSE_SOURCE_ROOT = $$clean_path($$PWD/../..)
include($$PWD/../../qmake/layout.pri)

QT       += core
QT       -= gui
CONFIG   += c++17 staticlib
CONFIG   -= app_bundle
TEMPLATE  = lib
TARGET    = omahousecore

HEADERS += \
    Version.h

SOURCES += \
    Version.cpp
