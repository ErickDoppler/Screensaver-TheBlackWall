#!/usr/bin/env bash
# ===========================================================================
#  The Black Wall - build theblackwall for Linux
#
#  Uses the toolchain that 1-downloads-tools.sh put in ~/workenv (override
#  with BW_WORKENV): the pinned CMake and Ninja, and SDL3 compiled from the
#  local source copy, so no network is needed. Where a pinned tool is
#  missing, the one on PATH is used instead.
#
#  Usage:  ./2-build-for-linux.sh [debug] [clean]
#            debug   build with symbols into build/linux-debug
#            clean   throw the build folder away first (full rebuild)
# ===========================================================================
set -euo pipefail

WORKENV="${BW_WORKENV:-$HOME/workenv}"
ROOT="$(cd "$(dirname "$0")" && pwd)"
PRESET=linux
CLEAN=0
for arg in "$@"; do
    case "$arg" in
        debug)         PRESET=linux-debug ;;
        clean|rebuild) CLEAN=1 ;;
        -h|--help)     sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)             echo "Unknown argument: $arg (expected debug and/or clean)" >&2; exit 2 ;;
    esac
done

case "$(uname -m)" in
    x86_64|amd64)  CMAKE_ARCH=x86_64 ;;
    aarch64|arm64) CMAKE_ARCH=aarch64 ;;
    *)             CMAKE_ARCH=none ;;
esac
D_CMAKE="$WORKENV/cmake-4.4.3-linux-$CMAKE_ARCH"
D_NINJA="$WORKENV/ninja"
D_SDL="$WORKENV/SDL3-3.4.16"

echo
echo " The Black Wall - Linux build"
echo " ----------------------------"
echo " toolchain : $WORKENV"
echo " preset    : $PRESET"
echo

nokit() {
    echo " MISSING: $1"
    echo
    echo " The build toolchain is not in place. Run this first:"
    echo
    echo "     ./1-downloads-tools.sh"
    echo
    exit 1
}

# The pinned tools go on PATH ahead of anything the system has.
[ -x "$D_CMAKE/bin/cmake" ] && PATH="$D_CMAKE/bin:$PATH"
[ -x "$D_NINJA/ninja" ]     && PATH="$D_NINJA:$PATH"
export PATH

command -v "${CC:-cc}" >/dev/null 2>&1 || nokit "a C compiler"
command -v cmake >/dev/null 2>&1       || nokit "CMake"
command -v ninja >/dev/null 2>&1       || nokit "Ninja"
cmake_ver="$(cmake --version | head -n1 | awk '{print $3}')"
if [ "$(printf '%s\n' 3.25 "$cmake_ver" | sort -V | head -n1)" != 3.25 ]; then
    nokit "CMake 3.25 or newer (found $cmake_ver)"
fi

# SDL3 from the local copy when there is one; otherwise CMake downloads the
# same pinned, hash-checked tarball itself.
EXTRA=()
if [ -f "$D_SDL/CMakeLists.txt" ]; then
    EXTRA+=("-DFETCHCONTENT_SOURCE_DIR_SDL3=$D_SDL")
else
    echo " note: no SDL3 source in $D_SDL - CMake will download it"
    echo
fi

if [ "$CLEAN" -eq 1 ] && [ -d "$ROOT/build/$PRESET" ]; then
    echo " Removing build/$PRESET ..."
    rm -rf "${ROOT:?}/build/$PRESET"
fi

cd "$ROOT"
echo " Configuring..."
cmake --preset "$PRESET" ${EXTRA[@]+"${EXTRA[@]}"}
echo
echo " Compiling..."
cmake --build --preset "$PRESET"

OUT="$ROOT/build/$PRESET/theblackwall"
if [ ! -x "$OUT" ]; then
    echo " ERROR: the build reported success but $OUT is missing."
    echo
    echo " BUILD FAILED."
    exit 1
fi

echo
echo " Built: build/$PRESET/theblackwall  ($(wc -c < "$OUT") bytes)"
echo
echo " Try it in a window:   build/$PRESET/theblackwall --window"
echo " Install it:           ./tools/install-linux.sh"
echo
