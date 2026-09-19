#!/usr/bin/env bash
# Write the markdown `usage` generates from omahouse.usage.kdl to "$1".
#
# The one generator both usage-gen.sh and usage-check.sh run, so the two cannot
# disagree about what the page should be. The version line gets release-please's
# marker, because release-please rewrites the number on the lines it marks and
# `usage` has no way to write a comment of its own: without it a release would
# bump the declaration, leave the page behind, and fail usage-check on master.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$1"

mise exec -- usage g markdown -f "$root/omahouse.usage.kdl" --out-file "$out"
sed -i -E 's|^(- \*\*Version:\*\* [0-9][^ ]*)$|\1 <!-- x-release-please-version -->|' "$out"
grep -q 'x-release-please-version' "$out" || {
  echo "usage-markdown: no version line to mark in $out" >&2
  exit 1
}
