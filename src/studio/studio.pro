# The window, and the fourth subdir plan.md said would arrive with stage 8.
#
# It links both libraries and reads with them directly, because everything it
# reads is world readable and needs no privilege. It writes nothing: `Admin`
# runs `pkexec omahouse <verb>`, so the privileged half of this program is the
# same CLI an operator would have typed, with the same checks and the same
# refusals.
OMAHOUSE_PROJECT_DIR = $$PWD
OMAHOUSE_SOURCE_ROOT = $$clean_path($$PWD/../..)
include($$PWD/../../qmake/layout.pri)

QT       += core gui qml quick
CONFIG   += c++17 qmltypes

# So the three types this ships are registered by the build rather than by a
# line in main(), and so `qmllint` can see them: a type registered in code is
# invisible to it, and every use of one in Main.qml would read as a failed
# import. `.scripts/qml-check.sh` builds this and points the linter at the
# qmltypes the build writes here.
QML_IMPORT_NAME = omahouse
QML_IMPORT_MAJOR_VERSION = 1
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = omahouse-studio

DESTDIR = $$OUT_PWD/../../bin

INCLUDEPATH += $$PWD/../core $$PWD/../sys

HEADERS += Theme.h House.h Admin.h Catalog.h
SOURCES += main.cpp Theme.cpp House.cpp Admin.cpp Catalog.cpp
RESOURCES += resources.qrc

# sys before core on the line, for the reason cli.pro gives: sys calls into core
# and a static archive only satisfies symbols the archives to its left still want.
LIBS           += -L$$OUT_PWD/../sys -lomahousesys -L$$OUT_PWD/../core -lomahousecore
PRE_TARGETDEPS += $$OUT_PWD/../sys/libomahousesys.a $$OUT_PWD/../core/libomahousecore.a
