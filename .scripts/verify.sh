#!/usr/bin/env bash
# omahouse local gate — the pre-commit hook execs this script.
#
# Idempotent: same tree, same result. The hook must not go through `mise run`,
# because a hook that depends on a task file is a hook that changes meaning when
# the task file does; it calls this script directly instead.
#
# Today the gate is the tests, and only the tests. It grows by gaining a line
# here as the thing it would check comes into existence: `usage-check.sh` with
# the CLI declaration of stage 4, `qml-check.sh` with the studio of stage 8. A
# step that runs before there is anything for it to look at is a green light
# nobody earned, so there is none.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

exec "$root/.scripts/test.sh"
