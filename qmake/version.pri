# The one place the version is written.
#
# Everything in C++ reads OMAHOUSE_VERSION; nothing spells the number in a .cpp.
# The usage declaration carries a literal of its own, which a test pins against
# `omahouse --version`, and release-please moves both.
OMAHOUSE_VERSION = 0.1.0 # x-release-please-version
VERSION = $$OMAHOUSE_VERSION

# Handed to C++ through a header rather than DEFINES. A changed DEFINES rewrites
# the Makefile and recompiles nothing, so an incremental build went on saying
# the previous version after a release bumped it. A header is a dependency make
# tracks, and it is only rewritten when the number in it changes, so an
# unchanged version costs no rebuild.
OMAHOUSE_VERSION_HEADER = $$OUT_PWD/omahouse_version.h
omahouse_version_line = "$${LITERAL_HASH}define OMAHOUSE_VERSION \"$$OMAHOUSE_VERSION\""
omahouse_version_had = $$cat($$OMAHOUSE_VERSION_HEADER, lines)
!equals(omahouse_version_had, $$omahouse_version_line) {
    write_file($$OMAHOUSE_VERSION_HEADER, omahouse_version_line)|error("cannot write $$OMAHOUSE_VERSION_HEADER")
}
INCLUDEPATH += $$OUT_PWD
