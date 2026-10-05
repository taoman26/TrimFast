#!/bin/sh
# Generates a small test clip (6 s, 640x360, 25 fps, a keyframe every 1 s, with audio).
# Uses H.264/AAC when this ffmpeg has libx264 (Linux), MPEG-4 Part 2/AAC otherwise (Haiku's build).
# Usage: scripts/make_test_media.sh [output.mp4]
set -e
out="${1:-test_h264.mp4}"
if ffmpeg -hide_banner -encoders 2>/dev/null | grep -q libx264; then
    vcodec="-c:v libx264 -preset veryfast"
else
    vcodec="-c:v mpeg4 -q:v 5"
fi
# shellcheck disable=SC2086
ffmpeg -v error -y \
    -f lavfi -i testsrc=duration=6:size=640x360:rate=25 \
    -f lavfi -i sine=frequency=440:duration=6 \
    $vcodec -g 25 -pix_fmt yuv420p -c:a aac -shortest "$out"
echo "wrote $out"
