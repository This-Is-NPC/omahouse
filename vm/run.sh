#!/usr/bin/env bash
# The VM suite, from a clean build.
#
# Not in `mise run verify` and not in the pre-push hook, on purpose. This
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
# There are two machines. `--machine poc` is the default and is disposable: its
# cases empty /etc/omahouse, close processes and end a login. `--machine omarchy`
# is the owner's demonstration VM, which has a real browser on it and is put back
# byte for byte afterwards. A case declares which one it needs and is not even
# imported for the other.
#
#     vm/run.sh                 every case, quick, then shut the machine down
#     vm/run.sh --pace long     the same cases with the long windows
#     vm/run.sh --machine omarchy   the browser case, on real Omarchy
#     vm/run.sh --keep          leave it running, for looking at
#     vm/run.sh --case grace    one case, by a piece of its name
#     vm/run.sh --demo          the walkthroughs meant to be filmed, and only those
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

# The binary under test is built from the working tree every run. docs/design.md's
# artefact is the thing being tested; a copy frozen into the guest image would be
# testing last week's.
"$root/.scripts/build.sh" >/dev/null

exec python3 "$root/vm/e2e.py" "$@"
