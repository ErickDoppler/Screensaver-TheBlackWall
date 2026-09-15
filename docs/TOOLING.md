# Tooling in C:\workenv

All tools are portable (unzip-and-run), open source, and pinned. Nothing is
installed system-wide; `tools\env.cmd` / `tools\env.ps1` put them on PATH.

| Tool            | Version | Location                                   | License                          | Source                                                        |
|-----------------|---------|--------------------------------------------|----------------------------------|---------------------------------------------------------------|
| w64devkit       | 2.9.1   | `C:\workenv\w64devkit`                     | Unlicense (kit); GCC GPL w/ runtime exception; mingw-w64 permissive | github.com/skeeto/w64devkit |
| GCC             | 16.2.0  | inside w64devkit (`gcc`, `g++`, `gdb`, `windres`, `make`) | GPL-3 with GCC Runtime Library Exception | part of w64devkit |
| CMake           | 4.4.3   | `C:\workenv\cmake-4.4.3-windows-x86_64`    | BSD-3-Clause                     | github.com/Kitware/CMake (SHA-256 verified against release file) |
| Ninja           | 1.13.2  | `C:\workenv\ninja`                         | Apache-2.0                       | github.com/ninja-build/ninja                                  |
| SDL3 (source)   | 3.4.16  | `C:\workenv\SDL3-3.4.16`                   | zlib                             | github.com/libsdl-org/SDL (SHA-256 pinned in CMakeLists.txt)  |
| stb_image_write | 1.16    | `third_party/` in the project              | public domain / MIT              | github.com/nothings/stb                                       |

Downloaded archives are kept in `C:\workenv\downloads` for re-installation.

## Why this stack

* **C11 + OpenGL 3.3 core** - as low level as is practical while staying
  portable to Linux; drivers are universal, the API is small, and the whole
  wall is one draw call.
* **SDL3, statically linked** - the only dependency. It gives us window
  creation on a *foreign* HWND (needed for the Windows preview mode) and on a
  foreign X11 window (needed for XScreenSaver later), OpenGL context creation,
  input, and precise timers. Unused subsystems (audio, gamepad, camera, GPU
  API, renderer) are compiled out.
* **GCC/MinGW via w64devkit** - no Visual Studio; the same compiler family
  builds the Linux version. mingw-w64 ships the Windows SDK headers we need
  (`commctrl.h`, `commdlg.h`, `iphlpapi.h`, `GL/gl.h`).
* **CMake + Ninja** - presets in `CMakePresets.json` capture the toolchain
  paths so a build is one command. SDL3 is pulled in with `FetchContent`; the
  preset points it at the local copy in `C:\workenv` so no network is needed.

## Rebuilding the environment on another machine

1. Extract the same archives from `C:\workenv\downloads` to the paths above
   (or adjust `cmake/toolchain-w64devkit.cmake` and `CMakePresets.json`).
2. `tools\build-windows.cmd`.
