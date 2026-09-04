# The one place the version is written.
#
# Everything in C++ reads the define; nothing spells the number in a .cpp. When
# the usage declaration arrives with stage 4 it gets a literal of its own that a
# test pins against `omahouse --version`, which is the only copy this file will
# tolerate.
OMAHOUSE_VERSION = 0.1.0
DEFINES += OMAHOUSE_VERSION=\\\"$$OMAHOUSE_VERSION\\\"
VERSION = $$OMAHOUSE_VERSION
