# Tooling

All tools are open source and pinned, and every downloaded archive is checked
against a pinned SHA-256 before it is unpacked. `1-downloads-tools.cmd` /
`1-downloads-tools.sh` set everything up; `2-build-for-win64.cmd` /
`2-build-for-linux.sh` put it on PATH for the build.

## Windows: C:\workenv

Everything is portable (unzip-and-run). Nothing is installed system-wide;
`tools\env.cmd` / `tools\env.ps1` put the tools on PATH for manual use.

| Tool            | Version | Location                                   | License                          | Source                                                        |
|-----------------|---------|--------------------------------------------|----------------------------------|---------------------------------------------------------------|
| w64devkit       | 2.9.1   | `C:\workenv\w64devkit`                     | Unlicense (kit); GCC GPL w/ runtime exception; mingw-w64 permissive | github.com/skeeto/w64devkit |
| GCC             | 16.2.0  | inside w64devkit (`gcc`, `g++`, `gdb`, `windres`, `make`) | GPL-3 with GCC Runtime Library Exception | part of w64devkit |
| CMake           | 4.4.3   | `C:\workenv\cmake-4.4.3-windows-x86_64`    | BSD-3-Clause                     | github.com/Kitware/CMake (SHA-256 verified against release file) |
| Ninja           | 1.13.2  | `C:\workenv\ninja`                         | Apache-2.0                       | github.com/ninja-build/ninja                                  |
| SDL3 (source)   | 3.4.16  | `C:\workenv\SDL3-3.4.16`                   | zlib                             | github.com/libsdl-org/SDL (SHA-256 pinned in CMakeLists.txt)  |

w64devkit also ships a CMake of its own. `2-build-for-win64.cmd` puts the
pinned one first on PATH; `tools\env.cmd` does not, so a manual build through
it uses w64devkit's.

## Linux: ~/workenv

The compiler and development headers come from the distribution, since they
have to match its libraries; `1-downloads-tools.sh` installs them with apt,
dnf, pacman or zypper. The rest is pinned and lives in `~/workenv` (or
`$BW_WORKENV`), like on Windows.

| Tool            | Version | Location                                   | License        | Source |
|-----------------|---------|--------------------------------------------|----------------|--------|
| GCC or Clang    | distribution's | system                              | GPL-3 / Apache-2.0 | package manager (`build-essential`, `gcc`, `base-devel`) |
| X11, Wayland, GL headers | distribution's | system                    | MIT and similar | package manager (`libx11-dev` and friends) |
| CMake           | 4.4.3   | `~/workenv/cmake-4.4.3-linux-<arch>`       | BSD-3-Clause   | github.com/Kitware/CMake (x86_64 and aarch64; SHA-256 from Kitware's list) |
| Ninja           | 1.13.2  | `~/workenv/ninja`                          | Apache-2.0     | github.com/ninja-build/ninja (x86_64 and aarch64) |
| SDL3 (source)   | 3.4.16  | `~/workenv/SDL3-3.4.16`                    | zlib           | github.com/libsdl-org/SDL |

On other architectures the script skips CMake and Ninja and the build uses
the system's (CMake 3.25 or newer). The resulting binary links SDL3
statically and needs only libc, libX11 and libGL at run time; SDL loads the
X11 extensions, Wayland and OpenGL libraries itself when it starts.

## Both

| Tool            | Version | Location                      | License             | Source                  |
|-----------------|---------|-------------------------------|---------------------|-------------------------|
| stb_image_write | 1.16    | `third_party/` in the project | public domain / MIT | github.com/nothings/stb |
| cgltf           | 1.15    | `third_party/` in the project | MIT                 | github.com/jkuhlmann/cgltf (build-time only: `tools/gltf2points.c`) |

Downloaded archives are kept in `<workenv>/downloads`, so an environment can
be rebuilt without the network.

## Why this stack

* **C11 + OpenGL 3.3 core** - as low level as is practical while staying
  portable to Linux; drivers are universal, the API is small, and the whole
  wall is one draw call.
* **SDL3, statically linked** - the only runtime dependency beyond the
  system. It gives us window creation on a *foreign* HWND (needed for the
  Windows preview mode), OpenGL context creation, input, and precise timers.
  Unused subsystems (audio, gamepad, camera, GPU API, renderer) are compiled
  out.
* **Xlib on Linux** - XScreenSaver hands a hack a window to draw in. SDL
  could adopt that window, but it picks its GLX configuration without regard
  to the window's visual, which can fail. So SDL makes a window of its own and
  `src/platform_linux.c` moves it inside XScreenSaver's, which takes three
  Xlib calls.
* **GCC/MinGW via w64devkit** - no Visual Studio; the same compiler family
  builds the Linux version. mingw-w64 ships the Windows SDK headers we need
  (`commctrl.h`, `commdlg.h`, `iphlpapi.h`, `GL/gl.h`).
* **CMake + Ninja** - presets in `CMakePresets.json` capture the toolchain
  paths so a build is one command. SDL3 is pulled in with `FetchContent`; the
  build scripts point it at the local copy in the workenv so no network is
  needed.

## Rebuilding the environment on another machine

Copy `<workenv>/downloads` across and run `1-downloads-tools.cmd` or
`1-downloads-tools.sh`: it finds the archives already there, checks them and
unpacks them without downloading anything. (On Linux the package manager
still runs; pass `--no-packages` if the compiler and headers are already
installed.) Then run the build script.

## Continuous integration

`.github/workflows/linux.yml` runs the three Linux steps on Ubuntu 22.04,
24.04 and 24.04 Arm, then tests the result under Xvfb with software OpenGL
(`.github/scripts/linux-smoke.sh`) and inside a live XScreenSaver
(`.github/scripts/xscreensaver-e2e.sh`). Fedora, Arch, openSUSE Tumbleweed
and Debian containers run the first two steps.
