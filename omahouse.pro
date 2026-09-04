# One core, thin front ends. `core` is a static library that holds the model and
# has no opinion about how it is driven; `sys` is the one library that reads and
# writes the machine; `cli` is a thin thing on top of both.
#
# Two libraries and not one because the split is checkable. The core has no
# clock, no environment and no /sys/fs/cgroup in it, which is what lets a two
# hour budget be proved in microseconds; everything that does have those is on
# the other side of the line, in `sys`.
#
# The studio is a fourth subdir and arrives with stage 8 of plan.md. The daemon
# is not a project at all -- it is `omahouse watch`, a verb of this same CLI.
OMAHOUSE_PROJECT_DIR = $$PWD
OMAHOUSE_SOURCE_ROOT = $$PWD
include($$PWD/qmake/layout.pri)

TEMPLATE = subdirs
CONFIG  += ordered

SUBDIRS = \
    src/core \
    src/sys \
    src/cli

core.subdir = src/core
sys.subdir  = src/sys
sys.depends = core
cli.subdir  = src/cli
cli.depends = core sys
