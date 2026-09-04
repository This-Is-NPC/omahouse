#!/usr/bin/env bash
# Installs the versioned git hooks. Idempotent: running it again changes nothing.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

hooks_path=".githooks"
required_hooks=("pre-commit")

for hook in "${required_hooks[@]}"; do
  if [ ! -f "$hooks_path/$hook" ]; then
    echo "Missing hook: $hooks_path/$hook" >&2
    exit 1
  fi
done

chmod +x "$hooks_path"/*
git config core.hooksPath "$hooks_path"

configured="$(git config --get core.hooksPath || true)"
if [ "$configured" != "$hooks_path" ]; then
  echo "core.hooksPath ended up at '$configured'; expected '$hooks_path'." >&2
  exit 1
fi
for hook in "${required_hooks[@]}"; do
  if [ ! -x "$hooks_path/$hook" ]; then
    echo "$hooks_path/$hook did not end up executable." >&2
    exit 1
  fi
done

echo "Hooks installed: core.hooksPath=$hooks_path"
echo "  pre-commit execs '.scripts/verify.sh' and refuses the commit when it is red."
