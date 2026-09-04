#!/usr/bin/env bash
# Drive the studio window by keyboard and by mouse, with no screen. Idempotent.
#
# A separate binary from the model suite, and a separate build directory, for
# the reason tests/studio.pro gives: this one needs QtQuick and a QML engine,
# and the model tests should not drag a window toolkit in behind them. Two
# qmake projects in one directory would write over each other's Makefile.
#
# It runs the real `omahouse` verbs against a temporary tree, so it needs the
# build to have happened -- which is what the first line does.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

"$root/.scripts/build.sh" >/dev/null

dir="$root/build-tests/studio"
mkdir -p "$dir"
cd "$dir"
qmake6 "$root/tests/studio.pro"
make -j"$(nproc 2>/dev/null || echo 1)"
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= ./tst_studio
