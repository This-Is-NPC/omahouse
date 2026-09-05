#!/usr/bin/env bash
# Record a VM case as something a person can watch.
#
#     vm/record.sh --case the_parent_administers
#
# Two tracks, and the second is why this exists at all. `virsh screenshot` takes
# frames straight from the qemu framebuffer, so nothing is installed in the
# guest and nothing about the recording can change what is being recorded -- no
# agent, no X client, no compositor asked to cooperate. And the run's own output
# is captured beside it, because a film of two terminals with no commentary is a
# film nobody can follow.
#
# The result is one MP4 with both machines side by side, and a log with the same
# clock on it.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
out="${OMAHOUSE_RECORD_DIR:-$root/.temp/recordings}"
fps="${OMAHOUSE_RECORD_FPS:-2}"
domains=(omahouse-poc omahouse-dad)

stamp="$(date +%Y%m%d-%H%M%S)"
work="$out/$stamp"
mkdir -p "$work"
for domain in "${domains[@]}"; do mkdir -p "$work/$domain"; done

# What `virsh screenshot` actually writes, which is not what its filename says.
# On a virtio framebuffer it hands back a PNG whatever extension you give it --
# it says so, `with type of image/png` -- and ffmpeg picks its decoder from the
# extension. Named `.ppm`, every frame is rejected as invalid data and the film
# comes out empty. So the type is asked for once, from virsh's own answer, and
# every frame is named for what it really is.
frame_ext() {
    local probe reply
    probe=$(mktemp "${TMPDIR:-/tmp}/omahouse-frame.XXXXXX")
    for domain in "${domains[@]}"; do
        reply=$(virsh -c qemu:///system screenshot "$domain" "$probe" 2>/dev/null) || continue
        rm -f "$probe"
        case "$reply" in
            *image/png*) printf 'png\n'; return 0 ;;
            *image/x-portable-pixmap*|*image/ppm*) printf 'ppm\n'; return 0 ;;
        esac
    done
    rm -f "$probe"
    # Nothing was running yet. PNG is what every framebuffer in this suite has
    # answered, and a wrong guess here costs a film rather than a run.
    printf 'png\n'
}

# One loop for both machines, and that is the whole trick.
#
# Two loops, one per domain, cannot be made to agree: a screenshot of a running
# domain costs more than copying a black frame, so the machine that is down most
# of the time ticks faster, ends with more frames, and at a fixed framerate that
# is a longer track. The two panels then show different moments and the film
# quietly lies about what happened when. Measured twice while getting here --
# 62s against 90s, and then 2036s against 930s when the sleep was made cleverer.
#
# Capturing both inside one tick makes the counts equal by construction, and
# nothing about the timing has to be reasoned about at all.
shot_loop() {
    local n=0 domain last
    while :; do
        for domain in "${domains[@]}"; do
            if ! virsh -c qemu:///system screenshot "$domain" \
                    "$work/$domain/$(printf '%06d' "$n").$ext" >/dev/null 2>&1; then
                # Every tick writes a frame for every machine, without exception.
                # A machine that has not started yet gets black; one that has
                # gone gets its last frame held.
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

# One machine is usually already running when a recording starts; if neither
# is, the probe falls back and the loops sort themselves out.
ext=$(frame_ext)
export ext

# The frame a machine that is not up yet shows.
ffmpeg -y -loglevel error -f lavfi -i "color=c=black:s=800x600:d=1" \
    -frames:v 1 -update 1 "$work/black.$ext"

capture_began=$(date +%s.%N)
shot_loop & shot_pid=$!
trap 'kill "$shot_pid" 2>/dev/null || true' EXIT

# The run's own words, stamped as they arrive. Both desktops are idle wallpaper
# for the whole film -- every step happens over ssh and none of it draws
# anything -- so without this the recording is technically correct and shows
# nothing. The narration is what makes it watchable, and it has to carry the
# clock of when each line was really printed rather than being pasted on after.
began=$(date +%s)
set +e
# No process substitution and no bare `wait`. The first version used both, and
# `wait` with no argument waits for *every* background job -- which here
# includes the two screenshot loops, and those never end. The run finished, the
# recording did not, and the frames piled up until somebody noticed.
"$root/vm/run.sh" "$@" 2>&1 \
    | while IFS= read -r line; do
          printf '%s\t%s\n' "$(( $(date +%s) - began ))" "$line" >> "$work/run.stamped"
          printf '%s\n' "$line"
      done | tee "$work/run.log"
status=${PIPESTATUS[0]}
set -e

kill "$shot_pid" 2>/dev/null || true
wait "$shot_pid" 2>/dev/null || true
trap - EXIT
capture_ended=$(date +%s.%N)

# The framerate the loop really achieved, not the one it was asked for. Two
# screenshots and a sleep per tick cost more than 1/fps, so encoding at the
# nominal rate compresses the film: 135 seconds of run came out as 106 seconds
# of video, and the narration -- which carries the run's own clock -- drifted a
# whole half-minute off the pictures by the end.
frames=$(find "$work/${domains[0]}" -name "*.$ext" | wc -l)
real_fps=$(awk "BEGIN{ elapsed = $capture_ended - $capture_began;
    printf \"%.4f\", (elapsed > 0 && $frames > 0) ? $frames / elapsed : $fps }")

# The two tracks are not the same length -- the second machine starts late and
# stops early -- so each becomes a film of its own first, and the shorter one is
# held on its last frame while the other finishes. `hstack` on two streams of
# different length would otherwise end the whole thing at the shorter one.
tracks=()
for domain in "${domains[@]}"; do
    count=$(find "$work/$domain" -name "*.$ext" | wc -l)
    [[ $count -gt 0 ]] || continue
    ffmpeg -y -loglevel error -framerate "$real_fps" -pattern_type glob \
        -i "$work/$domain/*.$ext" -vf "scale=800:600,drawtext=text='$domain':x=10:y=10:\
fontsize=20:fontcolor=white:box=1:boxcolor=black@0.6" \
        -c:v libx264 -pix_fmt yuv420p "$work/$domain.mp4"
    tracks+=("$work/$domain.mp4")
done

if [[ ${#tracks[@]} -eq 2 ]]; then
    ffmpeg -y -loglevel error -i "${tracks[0]}" -i "${tracks[1]}" \
        -filter_complex "[0:v]tpad=stop_mode=clone:stop_duration=600[a];\
[1:v]tpad=stop_mode=clone:stop_duration=600[b];[a][b]hstack=inputs=2,\
trim=duration=$(ffprobe -v error -show_entries format=duration -of csv=p=0 \
    "${tracks[0]}" | cut -d. -f1)[v]" \
        -map "[v]" -c:v libx264 -pix_fmt yuv420p "$work/both.mp4"
    film="$work/both.mp4"
else
    film="${tracks[0]:-}"
fi

# The narration, burned in. An `.srt` and not `drawtext` per line: a caption
# track is one filter however many lines there are, and it stays legible when
# somebody scrubs.
if [[ -s "$work/run.stamped" && -n ${film:-} ]]; then
    awk -F'\t' '
        function clock(t) { return sprintf("%02d:%02d:%02d,000", t/3600, (t%3600)/60, t%60) }
        NF > 1 && $2 !~ /^ *$/ {
            n++
            printf "%d\n%s --> %s\n%s\n\n", n, clock($1), clock($1 + 6), $2
        }' "$work/run.stamped" > "$work/narration.srt"
    if [[ -s "$work/narration.srt" ]]; then
        ffmpeg -y -loglevel error -i "$film" \
            -vf "subtitles=$work/narration.srt:force_style=\
'FontSize=13,PrimaryColour=&H00FFFFFF,BackColour=&HB0000000,BorderStyle=3,\
Alignment=2,MarginV=8'" \
            -c:v libx264 -pix_fmt yuv420p "$work/watch.mp4"
        [[ -s "$work/watch.mp4" ]] && film="$work/watch.mp4"
    fi
fi

find "$work" -name "*.$ext" -delete
printf '\n  film: %s\n  log:  %s\n' "${film:-none}" "$work/run.log"
exit "$status"
