# One core, thin front ends. `core` is a static library that holds the model and
# has no opinion about how it is driven; `cli` is a thin thing on top of it.
#
# The studio is a third subdir and arrives with stage 8 of plan.md. The daemon
# is not a project at all -- it is `omahouse watch`, a verb of this same CLI.
OMAHOUSE_PROJECT_DIR = $$PWD
OMAHOUSE_SOURCE_ROOT = $$PWD
include($$PWD/qmake/layout.pri)

TEMPLATE = subdirs
CONFIG  += ordered

SUBDIRS = \
    src/core \
    src/cli

core.subdir = src/core
cli.subdir  = src/cli
cli.depends = core
