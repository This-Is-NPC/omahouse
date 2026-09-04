#!/usr/bin/env bash
# omahouse local gate — the pre-commit hook execs this script.
#
# Idempotent: same tree, same result. The hook must not go through `mise run`,
# because a hook that depends on a task file is a hook that changes meaning when
# the task file does; it calls this script directly instead. `usage-check.sh`
# does use `mise exec` — that is tool resolution and not task indirection, and it
# is the only way to reach the pinned `usage`. So `mise` has to be on PATH
# wherever `git commit` runs, and usage-check says so plainly when it is not.
#
# The gate grows by gaining a line here as the thing it would check comes into
# existence. The studio of stage 8 brought two: `qml-check.sh`, which lints the
# QML with every warning fatal, and `studio-check.sh`, which drives the window
# by keyboard and then by mouse with no screen involved. A step that runs before
# there is anything for it to look at is a green light nobody earned, which is
# why neither was here before there was a window.
#
# `shots-check.sh` is the third, and it is here for the same reason
# `usage-check.sh` is: a document generated from the program is only worth
# reading if something refuses a commit where the two have come apart. It says
# in its own head what it costs and the one thing it cannot pin.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

"$root/.scripts/usage-check.sh"
"$root/.scripts/test.sh"
"$root/.scripts/qml-check.sh"
"$root/.scripts/studio-check.sh"
exec "$root/.scripts/shots-check.sh"
