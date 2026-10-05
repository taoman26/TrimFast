#!/bin/sh
# Builds TrimFast-<version>-x86_64.AppImage: one file that carries its own Qt (so it runs on
# distributions whose Qt is older than 6.5). Run on Linux (x86_64).
#
#   QT_PREFIX=$HOME/Qt/6.10.3/gcc_64 packaging/linux/build_appimage.sh
#
# QT_PREFIX  a Qt >= 6.5 with Widgets and Multimedia (aqtinstall: `aqt install-qt linux desktop
#            6.10.3 linux_gcc_64 -m qtmultimedia -O ~/Qt`).
# The result needs glibc >= the one of the machine it was built on. ffmpeg and ffprobe are NOT
# bundled: the user installs them (`sudo apt install ffmpeg`).
# linuxdeploy and its Qt plug-in are downloaded once into <build dir>/tools.
set -e

cd "$(dirname "$0")/../.."
root="$(pwd)"
: "${QT_PREFIX:?Set QT_PREFIX to a Qt >= 6.5 installation, e.g. \$HOME/Qt/6.10.3/gcc_64}"
[ "$(uname -m)" = "x86_64" ] || { echo "x86_64 only" >&2; exit 1; }

version="$(sed -n 's/^project(TrimFast VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)"
build="${BUILD_DIR:-build-appimage}"
appdir="$root/$build/AppDir"
tools="$root/$build/tools"

echo "== Building TrimFast $version"
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
      -DTRIMFAST_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$build" -j"${JOBS:-4}"
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build"

echo "== Tools"
mkdir -p "$tools"
fetch() {  # name url
    [ -x "$tools/$1" ] || { curl -fL --retry 3 -o "$tools/$1" "$2" && chmod +x "$tools/$1"; }
}
base=https://github.com/linuxdeploy
fetch linuxdeploy "$base/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
fetch linuxdeploy-plugin-qt "$base/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"

echo "== Bundling"
# No FUSE needed to run the tools themselves (CI, containers, WSL).
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$tools:$PATH"
export QMAKE="$QT_PREFIX/bin/qmake"
export LD_LIBRARY_PATH="$QT_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LINUXDEPLOY_OUTPUT_VERSION="$version"
# Qt Multimedia's FFmpeg backend is a plug-in; the xcb platform plug-in is the window system.
# xcb is always bundled; add native Wayland and "offscreen" (headless: used by the smoke test).
export EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqoffscreen.so"
export EXTRA_QT_PLUGINS="multimedia;imageformats;iconengines;wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration"
export DEPLOY_PLATFORM_THEMES=0

out="$root/$build/out"
rm -rf "$out"
mkdir -p "$out"
cd "$out"
"$tools/linuxdeploy" --appdir "$appdir" \
    --executable "$appdir/usr/bin/trimfast" \
    --desktop-file "$appdir/usr/share/applications/io.github.taoman26.TrimFast.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/trimfast.png" \
    --plugin qt --output appimage

ls -l "$out"/*.AppImage
echo "== Done: $(ls "$out"/*.AppImage)"
