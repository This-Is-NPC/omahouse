#!/usr/bin/env bash
# Lint the QML, with every warning fatal. Idempotent.
#
# It used to say there was no QML to look at, and that was the honest answer
# while it was true: a linter handed nothing to read is green because it is
# empty, not because it approved of anything. The studio of stage 8 is here, so
# it reads it, and `verify.sh` calls it.
#
# `--max-warnings 0` is only affordable because of how the C++ side is wired.
# `Theme`, `House` and `Admin` are declared singletons -- QML_ELEMENT and
# QML_SINGLETON, registered by the build -- rather than context properties.
# A context property is invisible to qmllint by construction, so every one of
# the several hundred uses of one would be an unqualified access it could only
# shrug at, and a linter that shrugs cannot refuse anything. Declared, they are
# types the tooling and the engine agree on, and what is left over is zero.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

lint="/usr/lib/qt6/bin/qmllint"
if [ ! -x "$lint" ]; then
    lint="$(command -v qmllint || true)"
fi
if [ -z "$lint" ]; then
    echo "qml-check: no qmllint found (qt6-declarative ships it)" >&2
    exit 1
fi

# It needs the type description the build generates, under a qmldir, so that
# `import omahouse` resolves to the same three types the engine registers.
"$root/.scripts/build.sh" >/dev/null

types="$root/build/src/studio/omahouse-studio.qmltypes"
if [ ! -f "$types" ]; then
    echo "qml-check: $types is missing; the build should have written it" >&2
    exit 1
fi

imports="$(mktemp -d)"
trap 'rm -rf "$imports"' EXIT
mkdir -p "$imports/omahouse"
cp "$types" "$imports/omahouse/"
cat > "$imports/omahouse/qmldir" <<EOF
module omahouse
typeinfo $(basename "$types")
depends QtQuick
depends QtCore
EOF

exec "$lint" --max-warnings 0 -I "$imports" -I "$root/src/studio/qml" \
     "$root"/src/studio/qml/*.qml
