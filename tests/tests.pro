OMAHOUSE_PROJECT_DIR = $$PWD
OMAHOUSE_SOURCE_ROOT = $$clean_path($$PWD/..)
include($$PWD/../qmake/layout.pri)

QT       += core testlib
QT       -= gui
CONFIG   += testcase c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = tst_omahouse

INCLUDEPATH += $$PWD/../src/core

SOURCES += \
    tst_smoke.cpp

# The suite links the same archive the CLI links, rather than recompiling the
# core's sources into itself: a test that builds its own copy of the library can
# pass against sources the shipped binary was never built from. So it needs the
# main build to have happened -- `.scripts/test.sh` runs it first -- and says so
# plainly rather than leaving make to complain about a missing rule.
OMAHOUSE_CORE_DIR = $$clean_path($$OMAHOUSE_SOURCE_ROOT/build/src/core)
!exists($$OMAHOUSE_CORE_DIR/libomahousecore.a) {
    error("libomahousecore.a is not in build/src/core. Run mise run build first, or mise run test, which does.")
}

LIBS           += -L$$OMAHOUSE_CORE_DIR -lomahousecore
PRE_TARGETDEPS += $$OMAHOUSE_CORE_DIR/libomahousecore.a
