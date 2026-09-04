# One core, thin front ends. `core` is a static library that holds the model and
# has no opinion about how it is driven; `sys` is the one library that reads and
# writes the machine; `cli` is a thin thing on top of both.
#
# Two libraries and not one because the split is checkable. The core has no
# clock, no environment and no /sys/fs/cgroup in it, which is what lets a two
# hour budget be proved in microseconds; everything that does have those is on
# the other side of the line, in `sys`.
#
# The studio is the fourth subdir, and it is a front end like the CLI: it links
# both libraries to read and shells out to the CLI to write. The daemon is not a
# project at all -- it is `omahouse watch`, a verb of that same CLI.
OMAHOUSE_PROJECT_DIR = $$PWD
OMAHOUSE_SOURCE_ROOT = $$PWD
include($$PWD/qmake/layout.pri)

TEMPLATE = subdirs
CONFIG  += ordered

SUBDIRS = \
    src/core \
    src/sys \
    src/cli \
    src/studio

core.subdir   = src/core
sys.subdir    = src/sys
sys.depends   = core
cli.subdir    = src/cli
cli.depends   = core sys
studio.subdir = src/studio
# The window reads with both libraries directly -- everything it reads is world
# readable, so nothing about looking needs a privilege -- and writes nothing:
# it runs `pkexec omahouse <verb>`, which is the CLI above, with the same
# checks and the same refusals. It does not depend on `cli` in qmake's sense,
# because it calls the built program and does not link it.
studio.depends = core sys
