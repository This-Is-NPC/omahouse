#!/usr/bin/env bash
# The declaration parses, and docs/cli.md is what it generates. Idempotent.
#
# In the gate because a document that drifted from the thing it documents is a
# document that lies, and nothing else in the tree would notice.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

kdl="omahouse.usage.kdl"
out="docs/cli.md"

if [ ! -f "$kdl" ]; then
  echo "usage-check: missing $kdl" >&2
  exit 1
fi

if ! command -v mise >/dev/null 2>&1; then
  echo "usage-check: mise is not on PATH; it pins the 'usage' that generates $out" >&2
  echo "             (git hooks inherit a thin PATH -- see .scripts/verify.sh)" >&2
  exit 1
fi

mise exec -- usage lint "$kdl" >/dev/null

if [ ! -f "$out" ]; then
  echo "usage-check: missing $out (run mise run usage:gen)" >&2
  exit 1
fi

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

mise exec -- usage g markdown -f "$kdl" --out-file "$tmp"

if ! cmp -s "$out" "$tmp"; then
  echo "usage-check: $out is stale (run mise run usage:gen)" >&2
  exit 1
fi
