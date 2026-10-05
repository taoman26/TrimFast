#!/bin/sh
# Checks a built AppImage in a clean environment (no Qt variables, no access to the Qt it was
# built with in the library search path).
#
#   packaging/linux/smoke_test_appimage.sh [path/to/TrimFast-x.y.z-x86_64.AppImage] [video.mp4]
#
# Defaults: the newest AppImage under build-appimage/out, and $TRIMFAST_TEST_VIDEO (or one made
# with scripts/make_test_media.sh). Needs ffprobe on PATH. Exit status is the number of failures.
cd "$(dirname "$0")/../.."
root="$(pwd)"

appimage="${1:-$(ls -t "$root"/build-appimage/out/*.AppImage 2>/dev/null | head -1)}"
[ -x "$appimage" ] || { echo "no AppImage found (give its path)" >&2; exit 99; }
appimage="$(cd "$(dirname "$appimage")" && pwd)/$(basename "$appimage")"
video="${2:-$TRIMFAST_TEST_VIDEO}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
if [ -z "$video" ]; then
    video="$tmp/test.mp4"
    "$root/scripts/make_test_media.sh" "$video" >/dev/null
fi
chmod 700 "$tmp"
fails=0
ok()   { echo "  ok    $1"; }
fail() { echo "  FAIL  $1"; fails=$((fails + 1)); }

# A minimal environment: only what a desktop session would have anyway. Extra VAR=value
# arguments go in front of the command (`env -i` would drop variables set outside).
run() {
    env -i HOME="$HOME" PATH=/usr/bin:/bin LANG=C.UTF-8 APPIMAGE_EXTRACT_AND_RUN=1 \
        XDG_RUNTIME_DIR="$tmp" XDG_CONFIG_HOME="$tmp/config" QT_QPA_PLATFORM=offscreen \
        TRIMFAST_AUDIO=0 "$@"
}

echo "AppImage: $appimage ($(du -h "$appimage" | cut -f1))"

echo "command line"
v="$(run "$appimage" --version)"
case "$v" in "TrimFast "[0-9]*) ok "--version: $v" ;; *) fail "--version printed '$v'" ;; esac
run "$appimage" --help | grep -q "Usage: trimfast" && ok "--help" || fail "--help"
run "$appimage" --no-such-option >/dev/null 2>&1
[ $? -eq 2 ] && ok "unknown option -> exit status 2" || fail "unknown option exit status"

echo "opening and playing a video (headless)"
out="$(run TRIMFAST_PERF=quit timeout 60 "$appimage" "$video" 2>&1)"
echo "$out" | grep -q "perf: first frame shown" && ok "first frame decoded and shown (Qt Multimedia + FFmpeg plug-in)" \
    || { fail "no first frame"; echo "$out" | head -5; }
echo "$out" | grep -q "perf: keyframes loaded" && ok "keyframes loaded (ffprobe found)" || fail "no keyframes"

echo "libraries"
libs="$(run TRIMFAST_PERF=quit LD_DEBUG=libs timeout 60 "$appimage" "$video" 2>&1 | grep "calling init:" | sed 's/.*calling init: *//')"
qtcore="$(echo "$libs" | grep 'libQt6Core\.so' | head -1)"
case "$qtcore" in
    *appimage_extracted*|*.mount_*) ok "Qt comes from the AppImage ($qtcore)" ;;
    *) fail "Qt loaded from '$qtcore'" ;;
esac
echo "$libs" | grep -q "$HOME/Qt" && fail "something was loaded from \$HOME/Qt" || ok "nothing loaded from \$HOME/Qt"

echo "contents"
x="$tmp/extracted"
mkdir -p "$x" && (cd "$x" && "$appimage" --appimage-extract >/dev/null 2>&1)
root_dir="$x/squashfs-root"
for f in usr/bin/trimfast usr/plugins/platforms/libqxcb.so usr/plugins/platforms/libqwayland.so \
         usr/plugins/multimedia/libffmpegmediaplugin.so usr/share/applications/io.github.taoman26.TrimFast.desktop \
         usr/share/metainfo/io.github.taoman26.TrimFast.metainfo.xml usr/share/icons/hicolor/256x256/apps/trimfast.png; do
    [ -e "$root_dir/$f" ] && ok "contains $f" || fail "missing $f"
done
missing="$(find "$root_dir/usr" -type f \( -name '*.so*' -o -name trimfast \) -exec ldd {} \; 2>/dev/null \
           | grep "not found" | sort -u)"
# Libraries the system must provide (GL drivers, ...) are expected to be absent from the bundle.
unexpected="$(echo "$missing" | grep -vE 'libGL|libEGL|libOpenGL|libvulkan|libdrm|libgbm|libGLX|libX11|libxcb\.so|libXau|libXdmcp|libxkbcommon|libwayland|libfontconfig|libfreetype|libharfbuzz|libglib|libgobject|libgio|libdbus' | grep -v '^$')"
[ -z "$unexpected" ] && ok "no unresolved libraries" || { fail "unresolved libraries:"; echo "$unexpected" | head; }

echo "ffprobe missing"
mkdir -p "$tmp/config/TrimFast"
printf '[tools]\nffprobe=%s\n' "$tmp/no-such-ffprobe" > "$tmp/config/TrimFast/settings.ini"
run TRIMFAST_PERF=1 timeout 6 "$appimage" "$video" >/dev/null 2>"$tmp/err.txt"
rc=$?
[ "$rc" -eq 124 ] && ok "keeps running (error shown in the window), no crash" || fail "exit status $rc"
grep -qiE "segmentation|abort|core dumped" "$tmp/err.txt" && fail "crash message" || ok "no crash message"

echo
[ "$fails" -eq 0 ] && echo "all checks passed" || echo "$fails check(s) FAILED"
exit "$fails"
