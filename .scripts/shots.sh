#!/usr/bin/env bash
# Write the pictures in docs/img/ from the studio itself.
#
# Offscreen, so it needs no session, no compositor and no VM, and gives the same
# image on any machine: a screenshot pasted in by hand goes stale the first time
# a column moves, and nobody notices until somebody reads the page. There is
# already a whole set of those under vm/shots -- 43 frames of one run of one
# script through a live desktop -- and they are worth having and are not an
# inventory. They cover what that afternoon happened to touch. This covers every
# screen the window knows how to draw, and can be run again when it changes.
#
# Regenerated the same way docs/cli.md is, and checked the same way:
# `shots-check.sh` runs this into a scratch directory and compares. Called with
# an argument it writes there instead and leaves docs/img alone, which is what
# the check uses.
#
# The machinery is tests/tst_studio.cpp, which already drives the real window
# under real key events with no screen involved. Two cases in it -- and two runs
# of the binary -- because the two faces cannot be reached from one account:
# `$OMAHOUSE_AS` moves what the window reads, `root` is in wheel on every
# machine and `nobody` is in wheel on none, and that is also what keeps the
# operator's own name out of the header of every frame.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

out="${1:-$root/docs/img}"
build="$root/build-tests/studio"

# The verbs the fixture is written with are the shipped ones, run for real.
"$root/.scripts/build.sh" >/dev/null

mkdir -p "$out" "$build"

# Nothing of this machine's own gets into the picture.
#
# The theme is read live -- omarchy's `colors.toml` out of $XDG_STATE_HOME and
# hyprland's rounding out of $XDG_CONFIG_HOME -- which is the right thing for a
# window on somebody's desktop and the wrong thing for a file in a repository:
# it would mean the pictures are of whatever theme the person regenerating them
# happens to be running, and every contributor would produce a diff. Pointed at
# an empty directory, Theme falls through to its own defaults, which are the
# same everywhere.
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
mkdir -p "$scratch/home"

shoot() {
    local face="$1"
    local case="$2"
    HOME="$scratch/home" \
    XDG_STATE_HOME="$scratch/home/.local/state" \
    XDG_CONFIG_HOME="$scratch/home/.config" \
    XDG_DATA_HOME="$scratch/home/.local/share" \
    OMARCHY_PATH="$scratch/home/no-omarchy" \
    OMAHOUSE_AS="$face" OMAHOUSE_SHOTS="$out" \
    QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
    QT_SCALE_FACTOR=1 QT_FONT_DPI=96 \
        ./tst_studio "$case"
}

(
    cd "$build"
    qmake6 "$root/tests/studio.pro"
    make -j"$(nproc 2>/dev/null || echo 1)"
    shoot root writesTheOperatorShots
    shoot nobody writesTheSubjectShots
)

for shot in "$out"/*.png; do
    printf '%s  %s\n' "$(basename "$shot")" \
        "$(identify -format '%wx%h' "$shot" 2>/dev/null || echo '?')"
done
