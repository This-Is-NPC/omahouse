#!/usr/bin/env bash
# Regenerate docs/cli.md from omahouse.usage.kdl. Idempotent.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

mkdir -p docs
"$root/.scripts/usage-markdown.sh" docs/cli.md
