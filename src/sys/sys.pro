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
# So does the loop that joins them: `Watch` is the caller `docs/design.md` §5 describes,
# and it is here because the clock, the ledger file and the notification are all
# the machine. Warning is a systemd-run, and that arrived with stage 6. Stage 7
# grew this library rather than adding another for the two that were left:
# `Enforce` closes a scope by writing its cgroup.kill and ends a session with
# `loginctl terminate-user`, and `Blocked` is the /etc/omahouse/blocked that
# stock `pam_listfile` reads -- the half of `logout` without which the other half
# is theatre.
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
    Blocked.h \
    Chromium.h \
    Enforce.h \
    FocusFile.h \
    FurnitureFile.h \
    Notify.h \
    Omakure.h \
    Paths.h \
    Presence.h \
    Proc.h \
    Users.h \
    Watch.h

SOURCES += \
    Blocked.cpp \
    Chromium.cpp \
    Enforce.cpp \
    FocusFile.cpp \
    FurnitureFile.cpp \
    Notify.cpp \
    Omakure.cpp \
    Paths.cpp \
    Presence.cpp \
    Proc.cpp \
    Users.cpp \
    Watch.cpp
