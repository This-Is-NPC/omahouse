# Everything that touches the machine, and the only place allowed to.
#
# `src/core` is pure by rule, and the rule is checkable:
#
#     grep -rn 'currentDateTime\|QProcess\|getenv\|/proc\|/sys/fs' src/core/
#
# has to come back empty. So the cgroup walk, the environment the paths come out
# of and the account table live on this side of the line instead, and the core
# stays a thing that can be proved with a QDateTime made up on the spot.
#
# Stage 7 of plan.md grows this library rather than adding another: closing a
# scope is a write to its cgroup.kill, warning is a systemd-run, and logging out
# is /etc/omahouse/blocked plus terminate-user. All three are the machine, and
# all three belong beside the reading of it.
OMAHOUSE_PROJECT_DIR = $$PWD
OMAHOUSE_SOURCE_ROOT = $$clean_path($$PWD/../..)
include($$PWD/../../qmake/layout.pri)

QT       += core
QT       -= gui
CONFIG   += c++17 staticlib
CONFIG   -= app_bundle
TEMPLATE  = lib
TARGET    = omahousesys

INCLUDEPATH += $$PWD/../core

HEADERS += \
    Paths.h \
    Proc.h \
    Users.h

SOURCES += \
    Paths.cpp \
    Proc.cpp \
    Users.cpp
