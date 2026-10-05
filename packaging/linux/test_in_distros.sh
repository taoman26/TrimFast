#!/bin/sh
# Runs the AppImage on clean distributions (Docker): installs only what a desktop has anyway
# (ffmpeg, the OpenGL libraries a desktop ships - libgl1, libopengl0, libegl1 - and an X server for the test) and checks that TrimFast starts with its
# real window-system plug-in (xcb, under Xvfb), opens a video and finds its keyframes.
#
#   packaging/linux/test_in_distros.sh [AppImage] [video.mp4] [image ...]
#
# Default images: ubuntu:22.04 (the oldest glibc the AppImage supports), debian:12, ubuntu:24.04.
cd "$(dirname "$0")/../.."
appimage="${1:-$(ls -t build-appimage/out/*.AppImage | head -1)}"
video="${2:-$TRIMFAST_TEST_VIDEO}"
[ -f "$appimage" ] && [ -f "$video" ] || { echo "usage: $0 AppImage video.mp4 [image...]" >&2; exit 99; }
if [ $# -ge 2 ]; then shift 2; else set --; fi   # what is left are image names
images="${*:-ubuntu:22.04 debian:12 ubuntu:24.04}"
appimage="$(cd "$(dirname "$appimage")" && pwd)/$(basename "$appimage")"
video="$(cd "$(dirname "$video")" && pwd)/$(basename "$video")"

status=0
for image in $images; do
    echo "== $image"
    docker run --rm -e DEBIAN_FRONTEND=noninteractive \
        -v "$appimage:/opt/TrimFast.AppImage:ro" -v "$video:/data/test.mp4:ro" "$image" sh -c '
        set -e
        apt-get update -qq >/dev/null
        apt-get install -y -qq --no-install-recommends ffmpeg xvfb xauth libgl1 libopengl0 libegl1 >/dev/null 2>&1
        cp /opt/TrimFast.AppImage /tmp/TrimFast.AppImage        # the runtime needs a writable copy
        export APPIMAGE_EXTRACT_AND_RUN=1 TRIMFAST_AUDIO=0 HOME=/tmp
        echo "glibc: $(ldd --version | head -1 | sed "s/.* //")"
        /tmp/TrimFast.AppImage --version
        out="$(TRIMFAST_PERF=quit xvfb-run -a /tmp/TrimFast.AppImage /data/test.mp4 2>&1)" || true
        echo "$out" | grep -E "perf: (window shown|ffprobe|first frame|keyframes)" | sed "s/ rss.*//" | tr -s " "
        echo "$out" | grep -q "perf: first frame shown" && echo "$out" | grep -q "perf: keyframes loaded" \
            && echo "RESULT: ok" || { echo "RESULT: FAILED"; echo "$out" | head -12; exit 1; }
    ' || status=1
done
exit $status
