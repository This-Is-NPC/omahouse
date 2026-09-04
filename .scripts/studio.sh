#!/usr/bin/env bash
# Open the studio out of the build tree. Idempotent.
#
# Never with sudo, and the binary refuses it: everything the window reads is
# world readable, and everything it writes goes out through `pkexec omahouse`.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

"$root/.scripts/build.sh" >/dev/null
exec "$root/build/bin/omahouse-studio" "$@"
