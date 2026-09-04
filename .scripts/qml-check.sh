#!/usr/bin/env bash
# Lint the QML, with every warning fatal. Idempotent.
#
# There is no QML in the tree until the studio of stage 8, and a linter handed
# nothing to read is green because it is empty, not because it approved of
# anything -- so it says which of the two it is and the gate in verify.sh does
# not call it yet.
set -euo pipefail
# So an empty tree yields nothing rather than an unexpanded glob.
shopt -s nullglob globstar

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

files=("$root"/src/**/*.qml)
if [ ${#files[@]} -eq 0 ]; then
    echo "qml-check: no QML in the tree yet — the studio arrives with stage 8 of plan.md."
    exit 0
fi

lint="/usr/lib/qt6/bin/qmllint"
if [ ! -x "$lint" ]; then
    lint="$(command -v qmllint || true)"
fi
if [ -z "$lint" ]; then
    echo "qml-check: no qmllint found (qt6-declarative ships it)" >&2
    exit 1
fi

exec "$lint" --max-warnings 0 -I "$root/src/studio/qml" "${files[@]}"
