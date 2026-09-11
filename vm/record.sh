#!/usr/bin/env bash
# Record a VM run as something a person can watch.
#
#     vm/record.sh --case the_parent_administers
#     vm/record.sh --machine omarchy --demo
#
# Frames come straight from the qemu framebuffer, so nothing is installed in the
# guest and nothing about the recording can change what is being recorded -- no
# agent, no X client, no compositor asked to cooperate. The run's own output is
# captured beside them and burned in as captions, because both desktops are idle
# wallpaper for most of a run: every step happens over ssh and none of it draws
# anything, so without the narration a recording is technically correct and
# shows nothing.
#
# Result: one MP4 in `.temp/recordings/<stamp>/`, and the log with it.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
out="${OMAHOUSE_RECORD_DIR:-$root/.temp/recordings}"
fps="${OMAHOUSE_RECORD_FPS:-2}"

# Which machines to point the camera at, taken from the same `--machine` the run
# is given. Hardcoding them once filmed two shut-down machines for five minutes
# while the one doing the work was never captured at all.
machine=poc
previous=""
for arg in "$@"; do
    [[ $previous == "--machine" ]] && machine=$arg
    previous=$arg
done
if [[ -n ${OMAHOUSE_RECORD_DOMAINS:-} ]]; then
    read -r -a domains <<< "$OMAHOUSE_RECORD_DOMAINS"
elif [[ $machine == omarchy ]]; then
    domains=(omahouse-omarchy)
else
    domains=(omahouse-poc omahouse-dad)
fi

work="$out/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$work"
for domain in "${domains[@]}"; do mkdir -p "$work/$domain"; done

# What `virsh screenshot` actually writes, which is not what its filename says:
# on a virtio framebuffer it hands back a PNG whatever extension you give it,
# and ffmpeg picks its decoder from the extension. Named `.ppm`, every frame is
# rejected as invalid data and the film comes out empty.
frame_ext() {
    local probe reply
    probe=$(mktemp "${TMPDIR:-/tmp}/omahouse-frame.XXXXXX")
    for domain in "${domains[@]}"; do
        reply=$(virsh -c qemu:///system screenshot "$domain" "$probe" 2>/dev/null) || continue
        rm -f "$probe"
        case "$reply" in
            *image/png*) printf 'png\n'; return 0 ;;
            *portable-pixmap*|*image/ppm*) printf 'ppm\n'; return 0 ;;
        esac
    done
    rm -f "$probe"
    printf 'png\n'
}
ext=$(frame_ext)
ffmpeg -y -loglevel error -f lavfi -i "color=c=black:s=800x600:d=1" \
    -frames:v 1 -update 1 "$work/black.$ext"

# -- the camera ---------------------------------------------------------------
#
# One loop for every machine, and that is the whole trick. Two loops cannot be
# made to agree: a screenshot of a running domain costs more than copying a
# black frame, so the machine that is down most of the time ticks faster, ends
# with more frames, and at a fixed framerate that is a longer track -- the two
# panels then show different moments and the film quietly lies about when things
# happened. Capturing all of them inside one tick makes the counts equal by
# construction.
#
# It also owns its own start and stop marks, in files. It runs inside a
# subshell, and a variable set there never reaches the script -- which is
# exactly how an earlier version came to believe the camera had never rolled
# when it had.
shot_loop() {
    local n=0 domain last
    # The camera waits for its cue. Installing a package, booting a machine and
    # typing a password at a greeter are minutes of nothing worth watching. A
    # walkthrough prints `FILM: rolling` when the first thing worth seeing is
    # about to happen; a run that never prints it is filmed from the start,
    # which is right for the ordinary cases -- they have no such moment.
    if [[ -n ${OMAHOUSE_RECORD_CUE:-} ]]; then
        until grep -q "FILM: rolling" "$work/run.log" 2>/dev/null; do sleep 0.5; done
    fi
    date +%s.%N > "$work/.began"
    while :; do
        for domain in "${domains[@]}"; do
            if ! virsh -c qemu:///system screenshot "$domain" \
                    "$work/$domain/$(printf '%06d' "$n").$ext" >/dev/null 2>&1; then
                # Every tick writes a frame for every machine, without exception.
                # A machine that is not up yet gets black; one that has gone gets
                # its last frame held.
                last=$(find "$work/$domain" -name "*.$ext" | sort | tail -1)
                if [[ -n $last ]]; then
                    cp "$last" "$work/$domain/$(printf '%06d' "$n").$ext"
                else
                    cp "$work/black.$ext" "$work/$domain/$(printf '%06d' "$n").$ext"
                fi
            fi
        done
        n=$((n + 1))
        sleep "$(awk "BEGIN{print 1/$fps}")"
    done
}

: > "$work/run.log"
shot_loop & shot_pid=$!
trap 'kill "$shot_pid" 2>/dev/null || true' EXIT

set +e
# Unbuffered, or the camera never sees its cue in time. `say()` flushes but a
# case's own `print()` does not, and Python block-buffers when stdout is a pipe
# -- which it is, here. So the whole narration, `FILM: rolling` included, landed
# at once when the run ended: the camera rolled eleven seconds before the credits
# on a walk that took three and a half minutes.
export PYTHONUNBUFFERED=1
"$root/vm/run.sh" "$@" 2>&1 \
    | while IFS= read -r line; do
          printf '%s\n' "$line" >> "$work/run.log"
          printf '%s\t%s\n' "$(date +%s)" "$line" >> "$work/run.stamped"
          printf '%s\n' "$line"
      done
status=${PIPESTATUS[0]}
set -e

kill "$shot_pid" 2>/dev/null || true
wait "$shot_pid" 2>/dev/null || true
trap - EXIT
capture_ended=$(date +%s.%N)

if [[ ! -f "$work/.began" ]]; then
    printf '\n  the run never reached its cue, so there is no film\n  log:  %s\n' \
        "$work/run.log"
    exit "$status"
fi
capture_began=$(cat "$work/.began")

# The narration, stamped against the moment the camera rolled rather than the
# moment the run started -- the two are minutes apart when there is a cue.
python3 - "$work/run.stamped" "$work/.began" "$work/narration.srt" <<'SRT' || true
import pathlib, sys
stamped, began_file, srt = (pathlib.Path(a) for a in sys.argv[1:4])
began = float(began_file.read_text().strip())


def clock(t):
    t = max(0.0, t)
    return "%02d:%02d:%02d,000" % (t // 3600, (t % 3600) // 60, t % 60)


entries = []
for line in stamped.read_text(errors="replace").splitlines():
    at, _, text = line.partition("\t")
    if not text.strip():
        continue
    try:
        offset = float(at) - began
    except ValueError:
        continue
    # A line printed before the camera rolled belongs to nothing anybody will
    # see, and dragging it to zero would stack the whole setup on frame one.
    if offset < 0:
        continue
    entries.append("%d\n%s --> %s\n%s\n"
                   % (len(entries) + 1, clock(offset), clock(offset + 6), text))
srt.write_text("\n".join(entries))
SRT

tracks=()
frames=$(find "$work/${domains[0]}" -name "*.$ext" | wc -l)
real_fps=$(awk "BEGIN{ e = $capture_ended - $capture_began;
    printf \"%.4f\", (e > 0 && $frames > 0) ? $frames / e : $fps }")

for domain in "${domains[@]}"; do
    [[ $(find "$work/$domain" -name "*.$ext" | wc -l) -gt 0 ]] || continue
    ffmpeg -y -loglevel error -framerate "$real_fps" -pattern_type glob \
        -i "$work/$domain/*.$ext" \
        -vf "scale=800:600,drawtext=text='$domain':x=10:y=10:fontsize=20:\
fontcolor=white:box=1:boxcolor=black@0.6" \
        -c:v libx264 -pix_fmt yuv420p "$work/$domain.mp4"
    tracks+=("$work/$domain.mp4")
done

if [[ ${#tracks[@]} -eq 1 ]]; then
    film="${tracks[0]}"
elif [[ ${#tracks[@]} -ge 2 ]]; then
    ffmpeg -y -loglevel error -i "${tracks[0]}" -i "${tracks[1]}" \
        -filter_complex "[0:v][1:v]hstack=inputs=2[v]" -map "[v]" \
        -c:v libx264 -pix_fmt yuv420p "$work/both.mp4"
    film="$work/both.mp4"
else
    film=""
fi

if [[ -s "$work/narration.srt" && -n $film ]]; then
    ffmpeg -y -loglevel error -i "$film" \
        -vf "subtitles=$work/narration.srt:force_style=\
'FontSize=13,PrimaryColour=&H00FFFFFF,BackColour=&HB0000000,BorderStyle=3,\
Alignment=2,MarginV=8'" -c:v libx264 -pix_fmt yuv420p "$work/watch.mp4"
    [[ -s "$work/watch.mp4" ]] && film="$work/watch.mp4"
fi

find "$work" -name "*.$ext" -delete
rm -f "$work/.began"
printf '\n  film: %s\n  log:  %s\n' "${film:-none}" "$work/run.log"
exit "$status"
