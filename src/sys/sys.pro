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
# So does the loop that joins them: `Watch` is the caller `spec.md` §5 describes,
# and it is here because the clock, the ledger file and the notification are all
# the machine. Warning is a systemd-run, and that arrived with stage 6. Stage 7
# grows this library rather than adding another for the two that are left:
# closing a scope is a write to its cgroup.kill, and logging out is
# /etc/omahouse/blocked plus terminate-user.
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
    Notify.h \
    Paths.h \
    Proc.h \
    Users.h \
    Watch.h

SOURCES += \
    Notify.cpp \
    Paths.cpp \
    Proc.cpp \
    Users.cpp \
    Watch.cpp
