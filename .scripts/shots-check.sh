#!/usr/bin/env bash
# The pictures in docs/img are what the studio draws today, not what it drew
# once. Same discipline as docs/cli.md: regenerate into a scratch directory and
# compare. Idempotent.
#
# In the gate, which it is only allowed to be because everything that could make
# the same tree draw two different pictures was pinned first. The theme is read
# live from omarchy's `colors.toml` and falls through to its own defaults under
# the empty $XDG_STATE_HOME shots.sh hands it; the text caret does not blink; the
# day's ledger has the clock written into it rather than taken from
# `QTime::currentTime`; the header says `root` or `nobody` rather than whoever
# ran it; and every frame is grabbed twice with a real wait between, so nothing
# is caught half way through a 90 millisecond fade.
#
# One thing is not pinned and cannot be short of shipping a font: which face
# `monospace` resolves to is fontconfig's answer, not this repository's. So a
# machine with a different monospace font, or this machine after one is
# installed, will find every picture stale at once -- and that is what it looks
# like when it happens, rather than a puzzle. `mise run shots` is the answer to
# it, and the diff is a diff of the whole set.
set -euo pipefail
# So an empty directory yields nothing rather than the literal `*.png`.
shopt -s nullglob

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/elsewhere"

# Deliberately under a TMPDIR that is not the default: the fixture tree is made
# with QTemporaryDir, and anything that ever started printing that path into the
# window would produce different bytes on a machine that sets one. Regenerating
# under an odd TMPDIR here is what makes that a failure on this machine rather
# than only on somebody else's.
TMPDIR="$tmp/elsewhere" "$root/.scripts/shots.sh" "$tmp/img" >/dev/null

stale=()
for fresh in "$tmp"/img/*.png; do
    committed="$root/docs/img/$(basename "$fresh")"
    if [ ! -f "$committed" ] || ! cmp -s "$fresh" "$committed"; then
        stale+=("$(basename "$fresh")")
    fi
done

# The other direction: a picture in docs/img that nothing generates any more is
# just as stale, and comparing only what was generated would never notice it.
for committed in "$root"/docs/img/*.png; do
    [ -f "$tmp/img/$(basename "$committed")" ] \
        || stale+=("$(basename "$committed") (nothing generates it)")
done

if [ ${#stale[@]} -ne 0 ]; then
    echo "shots-check: docs/img is stale (run mise run shots): ${stale[*]}" >&2
    exit 1
fi
