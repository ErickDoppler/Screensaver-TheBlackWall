#!/usr/bin/env bash
# ===========================================================================
#  The Black Wall - install the Linux build toolchain
#
#  1. Installs a C compiler and the X11 / Wayland / OpenGL development
#     headers SDL3 is built against, with the distribution's package manager
#     (apt, dnf, pacman or zypper; asks for sudo).
#  2. Downloads the pinned CMake, Ninja and SDL3 source into ~/workenv
#     (override with the first argument or BW_WORKENV), checking every archive
#     against a pinned SHA-256 before unpacking it. Nothing from this step is
#     installed system-wide.
#
#  Usage:  ./1-downloads-tools.sh [workenv-dir] [--force] [--no-packages]
#            --force         unpack again even if a tool folder already exists
#            --no-packages   skip step 1 (you manage the compiler and headers)
# ===========================================================================
set -euo pipefail

WORKENV="${BW_WORKENV:-$HOME/workenv}"
FORCE=0
PACKAGES=1
for arg in "$@"; do
    case "$arg" in
        --force)       FORCE=1 ;;
        --no-packages) PACKAGES=0 ;;
        -h|--help)     sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*)            echo "Unknown option: $arg" >&2; exit 2 ;;
        *)             WORKENV="$arg" ;;
    esac
done
DOWNLOADS="$WORKENV/downloads"

# --- pinned versions --------------------------------------------------------
V_CMAKE=4.4.3
V_NINJA=1.13.2
V_SDL=3.4.16
SHA_SDL=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68

case "$(uname -m)" in
    x86_64|amd64)
        CMAKE_ARCH=x86_64
        SHA_CMAKE=d6c83076c575bc00b823522ac974bda66d0af05d6ddc30e739c12385cf32c6cc
        NINJA_ZIP=ninja-linux.zip
        SHA_NINJA=5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6 ;;
    aarch64|arm64)
        CMAKE_ARCH=aarch64
        SHA_CMAKE=2efc974dbd63b4444c0e8494b92f2e80c2d7e635b4b80eac2916985ddd8f72a6
        NINJA_ZIP=ninja-linux-aarch64.zip
        SHA_NINJA=fd2cacc8050a7f12a16a2e48f9e06fca5c14fc4c2bee2babb67b58be17a607fc ;;
    *)
        CMAKE_ARCH="" ;;   # no prebuilt CMake/Ninja: the system ones are used
esac

D_CMAKE="$WORKENV/cmake-$V_CMAKE-linux-$CMAKE_ARCH"
D_NINJA="$WORKENV/ninja"
D_SDL="$WORKENV/SDL3-$V_SDL"

say()  { printf ' %s\n' "$*"; }
ok()   { printf '   [ok]   %s\n' "$*"; }
fail() { printf '\n ERROR: %s\n\n FAILED.\n' "$*" >&2; exit 1; }

echo
say "The Black Wall - Linux build toolchain"
say "--------------------------------------"
say "target folder : $WORKENV"
echo

# ===========================================================================
#  Step 1: compiler and development headers from the distribution
# ===========================================================================
as_root() {
    if [ "$(id -u)" -eq 0 ]; then "$@"
    elif command -v sudo >/dev/null 2>&1; then sudo "$@"
    else fail "need root to install packages; install sudo, run as root, or use --no-packages"
    fi
}

# Required packages must install: the compiler, and the X11 and OpenGL
# development files the screensaver links against. Optional ones (the extra
# SDL backends: Wayland, XInput, window decorations...) are added where the
# distribution has them.
install_apt() {
    as_root env DEBIAN_FRONTEND=noninteractive apt-get update -q
    # libgl-dev is the libglvnd-era name (Debian 10, Ubuntu 20.04 and later)
    local gl=libgl-dev
    apt-cache show libgl-dev >/dev/null 2>&1 || gl=libgl1-mesa-dev
    local req=(build-essential pkg-config curl ca-certificates unzip tar
               libx11-dev libxext-dev "$gl")
    local opt=(libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev
               libxtst-dev libxkbcommon-dev libegl-dev libwayland-dev
               wayland-protocols libdecor-0-dev libdbus-1-dev libudev-dev
               libdrm-dev libgbm-dev)
    local have=() p
    for p in "${opt[@]}"; do
        if apt-cache show "$p" >/dev/null 2>&1; then have+=("$p"); fi
    done
    as_root env DEBIAN_FRONTEND=noninteractive \
        apt-get install -y -q --no-install-recommends "${req[@]}" ${have[@]+"${have[@]}"}
}

install_dnf() {
    local req=(gcc make pkgconf-pkg-config curl tar unzip libX11-devel libXext-devel
               mesa-libGL-devel)
    local opt=(libXrandr-devel libXcursor-devel libXfixes-devel libXi-devel
               libXScrnSaver-devel libXtst-devel libxkbcommon-devel
               mesa-libEGL-devel wayland-devel
               wayland-protocols-devel libdecor-devel dbus-devel systemd-devel
               libdrm-devel mesa-libgbm-devel)
    as_root dnf install -y "${req[@]}"
    # dnf5 and dnf4 spell "skip what does not exist" differently
    if dnf --version 2>/dev/null | grep -q '^dnf5'; then
        as_root dnf install -y --skip-unavailable "${opt[@]}"
    else
        as_root dnf install -y --setopt=strict=0 "${opt[@]}"
    fi
}

install_pacman() {
    as_root pacman -S --needed --noconfirm base-devel curl tar unzip \
        libx11 libxext libxrandr libxcursor libxfixes libxi libxss libxtst \
        libxkbcommon libglvnd mesa wayland wayland-protocols libdecor
}

install_zypper() {
    local req=(gcc make pkg-config curl tar unzip libX11-devel libXext-devel
               Mesa-libGL-devel)
    local opt=(libXrandr-devel libXcursor-devel libXfixes-devel libXi-devel
               libXss-devel libXtst-devel libxkbcommon-devel
               Mesa-libEGL-devel wayland-devel wayland-protocols-devel
               libdrm-devel libgbm-devel dbus-1-devel)
    as_root zypper --non-interactive install --no-recommends "${req[@]}"
    local p
    for p in "${opt[@]}"; do
        as_root zypper --non-interactive install --no-recommends "$p" >/dev/null 2>&1 || true
    done
}

if [ "$PACKAGES" -eq 1 ]; then
    say "[1/2] Compiler and development headers"
    if   command -v apt-get >/dev/null 2>&1; then install_apt
    elif command -v dnf     >/dev/null 2>&1; then install_dnf
    elif command -v pacman  >/dev/null 2>&1; then install_pacman
    elif command -v zypper  >/dev/null 2>&1; then install_zypper
    else fail "no supported package manager (apt, dnf, pacman, zypper). Install a C compiler,
        pkg-config, curl, unzip and the X11 and OpenGL development files yourself, then
        run again with --no-packages."
    fi
    ok "packages installed"
else
    say "[1/2] Compiler and development headers: skipped (--no-packages)"
fi
echo

# ===========================================================================
#  Step 2: pinned CMake, Ninja and SDL3 source
# ===========================================================================
for tool in curl tar sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || fail "$tool not found"
done
mkdir -p "$DOWNLOADS"

# fetch <file> <url> <sha256>: downloads unless a byte-identical copy exists
fetch() {
    local f="$DOWNLOADS/$1" url="$2" want="$3" got
    if [ -f "$f" ]; then
        got="$(sha256sum "$f" | cut -d' ' -f1)"
        if [ "$got" = "$want" ]; then ok "$1 already downloaded"; return 0; fi
        say "  [warn] $1 does not match its pinned hash - downloading again"
        rm -f "$f"
    fi
    say "  [get]  $1"
    curl -L --fail --progress-bar -o "$f.part" "$url" || { rm -f "$f.part"; fail "download failed: $url"; }
    got="$(sha256sum "$f.part" | cut -d' ' -f1)"
    if [ "$got" != "$want" ]; then
        rm -f "$f.part"
        fail "SHA-256 mismatch for $1 - the file was NOT what we expected.
        expected $want
        actual   $got"
    fi
    mv "$f.part" "$f"
    ok "$1 verified"
}

# unpacked <dir>: true if the folder is there and --force was not given
unpacked() {
    if [ "$FORCE" -eq 0 ] && [ -d "$1" ]; then ok "$(basename "$1") already unpacked"; return 0; fi
    rm -rf "$1"
    return 1
}

say "[2/2] Pinned tools"
if [ -n "$CMAKE_ARCH" ]; then
    fetch "cmake-$V_CMAKE-linux-$CMAKE_ARCH.tar.gz" \
        "https://github.com/Kitware/CMake/releases/download/v$V_CMAKE/cmake-$V_CMAKE-linux-$CMAKE_ARCH.tar.gz" \
        "$SHA_CMAKE"
    fetch "ninja-$V_NINJA-$NINJA_ZIP" \
        "https://github.com/ninja-build/ninja/releases/download/v$V_NINJA/$NINJA_ZIP" \
        "$SHA_NINJA"
else
    say "  [note] no prebuilt CMake/Ninja for $(uname -m): the system ones will be used"
fi
fetch "SDL3-$V_SDL.tar.gz" \
    "https://github.com/libsdl-org/SDL/releases/download/release-$V_SDL/SDL3-$V_SDL.tar.gz" \
    "$SHA_SDL"

echo
say "Unpacking..."
if [ -n "$CMAKE_ARCH" ]; then
    unpacked "$D_CMAKE" || { tar -xzf "$DOWNLOADS/cmake-$V_CMAKE-linux-$CMAKE_ARCH.tar.gz" -C "$WORKENV"; ok cmake; }
    if ! unpacked "$D_NINJA"; then
        command -v unzip >/dev/null 2>&1 || fail "unzip not found"
        mkdir -p "$D_NINJA"
        unzip -q -o "$DOWNLOADS/ninja-$V_NINJA-$NINJA_ZIP" -d "$D_NINJA"
        chmod +x "$D_NINJA/ninja"
        ok ninja
    fi
fi
unpacked "$D_SDL" || { tar -xzf "$DOWNLOADS/SDL3-$V_SDL.tar.gz" -C "$WORKENV"; ok SDL3; }

# --- prove the toolchain actually runs ---------------------------------------
echo
say "Verifying..."
if [ -n "$CMAKE_ARCH" ]; then
    export PATH="$D_CMAKE/bin:$D_NINJA:$PATH"
fi
CC_BIN="${CC:-cc}"
command -v "$CC_BIN" >/dev/null 2>&1 || fail "no C compiler ($CC_BIN) found"
command -v cmake >/dev/null 2>&1     || fail "cmake not found"
command -v ninja >/dev/null 2>&1     || fail "ninja not found"
command -v pkg-config >/dev/null 2>&1 || fail "pkg-config not found"
pkg-config --exists x11 || fail "the X11 development files are missing (libx11-dev / libX11-devel)"
pkg-config --exists gl  || fail "the OpenGL development files are missing (libgl-dev / mesa-libGL-devel)"
[ -f "$D_SDL/CMakeLists.txt" ] || fail "SDL3 source is incomplete"

cmake_ver="$(cmake --version | head -n1 | awk '{print $3}')"
# The presets need CMake 3.25 or newer.
if [ "$(printf '%s\n' 3.25 "$cmake_ver" | sort -V | head -n1)" != 3.25 ]; then
    fail "cmake $cmake_ver is too old (3.25 or newer is needed)"
fi

say "  $("$CC_BIN" --version | head -n1)"
say "  cmake version $cmake_ver"
say "  ninja $(ninja --version)"
say "  libX11 $(pkg-config --modversion x11), libGL $(pkg-config --modversion gl)"
say "  SDL3 $V_SDL source at $D_SDL"
echo
say "Done. The archives are kept in $DOWNLOADS so the environment can be"
say "rebuilt on another machine without downloading again."
echo
say "Next: ./2-build-for-linux.sh"
echo
