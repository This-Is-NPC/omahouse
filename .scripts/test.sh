#!/usr/bin/env bash
# Build and run the unit tests. Idempotent.
#
# The build comes first because the suite links build/src/core/libomahousecore.a
# rather than compiling the core's sources a second time.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

"$root/.scripts/build.sh"

# Each .pro gets its own build directory: two qmake projects in one directory
# would write over each other's Makefile.
run_suite() {
    local project="$1" binary="$2" dir="$root/build-tests/$3"
    mkdir -p "$dir"
    (
        cd "$dir"
        qmake6 "$root/tests/$project"
        make -j"$(nproc 2>/dev/null || echo 1)"
        QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= "./$binary"
    )
}

run_suite tests.pro tst_omahouse core
