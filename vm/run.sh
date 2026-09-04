#!/usr/bin/env bash
# The VM suite, from a clean build.
#
# Not in `mise run verify` and not in the pre-commit hook, on purpose. This
# starts a virtual machine, waits for a graphical session, closes processes and
# ends somebody's login; it takes minutes, and the kind of regression it catches
# does not arrive once an hour. It is manual and nightly, which is what
# `docs/design.md` says of it under "Tests and the gate".
#
# Every duration it waits on lives in `vm/manifest.toml`, under one of two
# regimes, and `--pace` is the only knob that picks between them. `quick` is the
# default and holds the smallest window that still proves each case; `long` holds
# the windows that catch what only shows with time -- a browser's service worker
# over minutes of silence, a real idle cycle, a grace window a household would
# actually set. docs/design.md, "The two regimes", says which to run when.
#
#     vm/run.sh                 every case, quick, then shut the machine down
#     vm/run.sh --pace long     the same cases with the long windows
#     vm/run.sh --keep          leave it running, for looking at
#     vm/run.sh --case grace    one case, by a piece of its name
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

# The binary under test is built from the working tree every run. docs/design.md's
# artefact is the thing being tested; a copy frozen into the guest image would be
# testing last week's.
"$root/.scripts/build.sh" >/dev/null

exec python3 "$root/vm/e2e.py" "$@"
