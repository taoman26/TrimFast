#!/bin/sh
# Builds TrimFast and runs the whole test suite (Linux and Haiku).
#
#   scripts/run_tests.sh                 # default build dir "build"
#   BUILD_DIR=build-qt610 QT_PREFIX=$HOME/Qt/6.10.3/gcc_64 scripts/run_tests.sh
#   SANITIZE=1 scripts/run_tests.sh      # AddressSanitizer + UBSan (Linux)
#
# Optional extra inputs (their tests are skipped when unset):
#   TRIMFAST_TEST_INSV   the "_00_" file of a real Insta360 pair
#   TRIMFAST_TEST_LONG   a long clip with a keyframe every 2 s (throughput test)
set -e
cd "$(dirname "$0")/.."

build_dir="${BUILD_DIR:-build}"
case "$(uname -s)" in
    Haiku) jobs="${JOBS:-3}" ;;   # make -j8 can stall on small Haiku VMs
    *)     jobs="${JOBS:-4}" ;;
esac

cmake_args="-S . -B $build_dir -DCMAKE_BUILD_TYPE=${BUILD_TYPE:-Release}"
[ -n "$QT_PREFIX" ] && cmake_args="$cmake_args -DCMAKE_PREFIX_PATH=$QT_PREFIX"
[ -n "$SANITIZE" ] && cmake_args="$cmake_args -DTRIMFAST_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug"
# shellcheck disable=SC2086
cmake $cmake_args
cmake --build "$build_dir" -j"$jobs"

# Without a display (CI, WSL) the GUI tests use Qt's offscreen platform.
if [ "$(uname -s)" != "Haiku" ] && [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
    export QT_QPA_PLATFORM=offscreen
    export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-$(mktemp -d)}"
    chmod 700 "$XDG_RUNTIME_DIR"
fi
[ -n "$SANITIZE" ] && export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"

# Test inputs must be absolute: ctest runs every test from its own directory.
case "$build_dir" in
    /*) abs_build="$build_dir" ;;
    *)  abs_build="$(pwd)/$build_dir" ;;
esac
if [ -z "$TRIMFAST_TEST_VIDEO" ]; then
    TRIMFAST_TEST_VIDEO="$abs_build/test_media.mp4"
    [ -f "$TRIMFAST_TEST_VIDEO" ] || scripts/make_test_media.sh "$TRIMFAST_TEST_VIDEO"
fi
export TRIMFAST_TEST_VIDEO

ctest --test-dir "$build_dir" --timeout "${TEST_TIMEOUT:-180}" --output-on-failure
