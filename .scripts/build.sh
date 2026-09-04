#!/usr/bin/env bash
# Build the core and the CLI. Idempotent.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

"$root/.scripts/deps.sh" >/dev/null

build_dir="$root/build"
mkdir -p "$build_dir"
cd "$build_dir"
qmake6 "$root/omahouse.pro"
make -j"$(nproc 2>/dev/null || echo 1)"
echo "cli  $build_dir/bin/omahouse"
