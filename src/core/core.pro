# The model, and nothing else. No window, no argument parsing, no reading of the
# machine, no environment, and no clock: `evaluate` is handed `now`, which is
# what lets a two hour budget be proved in microseconds and what keeps the hard
# part testable without root. The one thing here that touches the disk is the
# atomic write, and it is told where to write by its caller.
#
# Stage 3 adds Proc beside this, and that is where the cgroup tree is read.
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
    AppScope.h \
    Duration.h \
    Focus.h \
    Furniture.h \
    Json.h \
    Ledger.h \
    Policy.h \
    Profile.h \
    Version.h \
    WebPolicy.h

SOURCES += \
    AppScope.cpp \
    Duration.cpp \
    Focus.cpp \
    Furniture.cpp \
    Json.cpp \
    Ledger.cpp \
    Policy.cpp \
    Profile.cpp \
    Version.cpp \
    WebPolicy.cpp
