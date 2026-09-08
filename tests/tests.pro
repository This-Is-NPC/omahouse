OMAHOUSE_PROJECT_DIR = $$PWD
OMAHOUSE_SOURCE_ROOT = $$clean_path($$PWD/..)
include($$PWD/../qmake/layout.pri)

# `qml` is QJSEngine and nothing else -- no window toolkit, no QtQuick, and no
# screen. `tst_polkit.cpp` needs a JavaScript interpreter because the polkit
# rules file omahouse installs *is* a JavaScript program, and the only honest
# way to check what it decides is to run it.
QT       += core testlib qml
QT       -= gui
CONFIG   += testcase c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = tst_omahouse

INCLUDEPATH += $$PWD/../src/core $$PWD/../src/sys

SOURCES += \
    main.cpp \
    tst_smoke.cpp \
    tst_scopename.cpp \
    tst_policy.cpp \
    tst_duration.cpp \
    tst_focus.cpp \
    tst_ledger.cpp \
    tst_proc.cpp \
    tst_paths.cpp \
    tst_presence.cpp \
    tst_watch.cpp \
    tst_enforce.cpp \
    tst_webpolicy.cpp \
    tst_fleet.cpp \
    tst_kind.cpp \
    tst_pairing.cpp \
    tst_polkit.cpp

# The suite links the same archive the CLI links, rather than recompiling the
# core's sources into itself: a test that builds its own copy of the library can
# pass against sources the shipped binary was never built from. So it needs the
# main build to have happened -- `.scripts/test.sh` runs it first -- and says so
# plainly rather than leaving make to complain about a missing rule.
OMAHOUSE_CORE_DIR = $$clean_path($$OMAHOUSE_SOURCE_ROOT/build/src/core)
OMAHOUSE_SYS_DIR  = $$clean_path($$OMAHOUSE_SOURCE_ROOT/build/src/sys)
!exists($$OMAHOUSE_CORE_DIR/libomahousecore.a)|!exists($$OMAHOUSE_SYS_DIR/libomahousesys.a) {
    error("libomahousecore.a and libomahousesys.a are not in build/src. Run mise run build first, or mise run test, which does.")
}

# sys before core on the line: sys calls into core -- `scopeIdFromUnit` -- and a
# static archive only satisfies symbols the archives to its left still want.
LIBS           += -L$$OMAHOUSE_SYS_DIR -lomahousesys -L$$OMAHOUSE_CORE_DIR -lomahousecore
PRE_TARGETDEPS += $$OMAHOUSE_CORE_DIR/libomahousecore.a $$OMAHOUSE_SYS_DIR/libomahousesys.a
