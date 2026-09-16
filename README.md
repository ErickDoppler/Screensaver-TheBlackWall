# The Black Wall

An atmospheric, energy-efficient 3D screensaver inspired by the Blackwall of
Cyberpunk 2077: an endless wall of glowing pixels standing on a dark floor in
empty space. The wall stretches to infinity left and right and fades away
upward. It breathes with your machine: CPU load tightens its wave, network
traffic pushes spikes through it.

Status: **Windows 1.0, stable.** Linux (XScreenSaver) is planned; the build
system is already laid out for it.

## Run it

### Windows

Everything is portable and open source. Nothing is installed system-wide
except the screensaver itself, and Visual Studio is not needed.

1. **Get the build tools.** Downloads GCC, CMake, Ninja and the SDL3 source
   into `C:\workenv`, checking every archive against a pinned SHA-256. Run it
   once; running it again only verifies what is already there.

   ```bat
   1-downloads-tools.cmd
   ```

2. **Build the screensaver.** Produces `build\win-mingw\TheBlackWall.scr`,
   a single static executable with no runtime dependencies. Takes a couple of
   minutes the first time, because SDL3 is compiled from source.

   ```bat
   2-build-for-win64.cmd
   ```

3. **Install it.** Copies the `.scr` into `%LOCALAPPDATA%\TheBlackWall` and
   makes it the active screensaver for the current user.

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\install-windows.ps1
   ```

To look at it before installing, run `build\win-mingw\TheBlackWall.scr /w`
for a window or `/s` for fullscreen; `Esc` always exits.

### Linux

**Not buildable yet.** XScreenSaver support is planned and the build system
is already laid out for it, but the platform layer (`src/platform_linux.c`)
and the XScreenSaver config (`res/linux/theblackwall.xml`) have not been
written, so the `linux` preset fails at configure time. Everything else -
the simulation, the renderer, the shaders - is portable C11 and OpenGL 3.3
and needs no changes.

Once those two files exist, the steps will be:

1. **Get the build tools.** GCC or Clang, CMake, Ninja, and the headers SDL3
   builds against (X11, Wayland, OpenGL). On Debian or Ubuntu:

   ```sh
   sudo apt install build-essential cmake ninja-build git \
        libx11-dev libxext-dev libxrandr-dev libwayland-dev libgl1-mesa-dev
   ```

2. **Build the screensaver.** Produces `build/linux/theblackwall`. Add
   `-DBW_SYSTEM_SDL=ON`, or use the `linux-system-sdl` preset, to link an
   SDL3 already on the system instead of compiling it.

   ```sh
   cmake --preset linux
   cmake --build --preset linux
   ```

3. **Install it.** Puts the binary in XScreenSaver's hack directory and its
   config in `share/xscreensaver/config`, after which it can be picked in
   `xscreensaver-settings`.

   ```sh
   sudo cmake --install build/linux
   ```

## Build options (Windows)

The two numbered scripts above are the whole story for a normal build; this
section is for when you want something other than a plain Release build.

`2-build-for-win64.cmd` takes `debug` (symbols, into
`build\win-mingw-debug`) and `clean` (discard the build folder first). Set
`BW_WORKENV` if the toolchain lives somewhere other than `C:\workenv`.

By hand, with the toolchain on PATH:

```bat
call tools\env.cmd
cmake --preset win-mingw
cmake --build --preset win-mingw
```

`tools\build-windows.cmd` does those three lines in one step. Note that
`tools\env.cmd` puts w64devkit first on PATH, so it uses the CMake bundled
inside w64devkit rather than the pinned one in `C:\workenv`;
`2-build-for-win64.cmd` prefers the pinned copy.

Output either way: `build\win-mingw\TheBlackWall.scr`, a single static
executable. See [docs/TOOLING.md](docs/TOOLING.md) for what is in the
toolchain and why.

Besides the installer script, you can right-click the `.scr` in Explorer and
choose **Install**.

## Windows Defender

An unsigned, freshly built `.scr` can trip Defender's machine-learning
heuristics (typically reported as `Trojan:Win32/Wacatac.*!ml`). The binary is
stripped, statically linked, carries version info, a manifest and DEP/ASLR
flags, and never launches other programs, which keeps it clean in on-demand
scans; behavioural detections on first run are still possible. If it
happens:

1. Report it as a false positive at
   https://www.microsoft.com/wdsi/filesubmission (choose "Software developer"),
   attaching `TheBlackWall.scr`. Microsoft usually clears it within a day or
   two and the fix reaches everyone through the definition updates.
2. Meanwhile, restore the file from Defender's protection history and add
   an exclusion for the install folder (`%LOCALAPPDATA%\TheBlackWall`).
3. Code-signing the `.scr` with a certificate from a public CA removes the
   problem at the root, since reputation attaches to the signer.

## Command line

Windows calls the screensaver with the standard switches:

| Switch      | Meaning                                              |
|-------------|------------------------------------------------------|
| `/s`        | run fullscreen across all monitors                   |
| `/p <hwnd>` | live preview inside the Windows screensaver dialog   |
| `/c`        | settings dialog (also the default with no arguments) |

Developer switches:

| Switch                          | Meaning                                          |
|---------------------------------|--------------------------------------------------|
| `/w` or `--window 1600x900`     | run in a resizable window                        |
| `--dump out.png --frames 90`    | render 90 frames, save the last one, exit        |
| `--cpu 0.8` / `--net 20000000`  | fake CPU load (0..1) / network bits per second   |
| `--log file.txt`                | append diagnostics to a file                     |
| `--<setting> <value>`           | override any setting for this run, e.g. `--density 80 --pixel-type matrix --wall-color #ff3b1f` |

Setting keys: `side-movement`, `movement-speed`, `mouse-rotation`,
`exit-on-mouse-move`, `mouse-sensitivity`, `click-resets-view`,
`exit-on-any-key`, `density`, `pixel-size` (the dot itself, from about
2 x 2 screen pixels up to a chunky LED), `blur`, `ghost-tail`,
`shimmer` (0 = smooth, 100 = pixels flicker as if tearing off),
`pixel-type` (square/round/matrix), `horizon` (distant city, default on),
`fps` (10..120, default 60), `wall-color`, `floor-color`, `space-color`,
`horizon-color` (default blue).
Settings persist in `HKCU\Software\TheBlackWall`.

**Figures.** Every 100 m of travel along the wall a character from
Cyberpunk 2077 slowly fades in 36 m ahead, standing 10 m from the wall; come
within 10 m and the wall rises as a giant wave and swallows it, leaving its
shadow burnt into the wall, which then dissolves into sparkling static.
By default the mouse looks around and the keys move the camera.
Seven figures (Johnny, Alt, Songbird, Brendan, Skippy, David, Jackie) are
built from the GLB models in `res/models`, colored by nature: engrams
crimson, half-human gold, AIs yellow, humans blue. The **Show ghosts**
checkbox turns them off. Each figure's facing is set once in the pose tool
(**Position the ghosts...** in the dialog, or `/w --pose`): Up/Down pick the
figure, Left/Right rotate it in 45-degree steps, Enter saves, Esc cancels.

**The corridor.** You can step back up to 1 km from the wall. Beyond 50 m it
looms taller, figures appear at your depth and the wave comes all the way out
to take them. From 900 m the picture picks up static, from 970 m it breaks up
like a damaged video, and at 999 m the screen switches off, leaving faint
CCTV lines. Walk back toward the wall and the picture returns.

More developer switches: `--yaw 180` starts the camera facing away from the
wall; `--back 300` starts the camera 300 m from the wall; `--figure-every <metres>` changes the spacing of the figures (`-1`
summons one at once); `--model <n>` picks the figure the pose tool starts
with. `-DBW_MODEL_POINTS=<n>` at configure time changes how many points are
kept per model (default 40000). `tools/make_icon.py` regenerates the icon.

## Controls

When **Exit on any button** is off, the keyboard steers the camera:

| Key                  | Action                            |
|----------------------|-----------------------------------|
| Up/Down or W/S, wheel| move forward / back along the view direction |
| Left/Right or A/D    | strafe left / right relative to the view     |
| Shift + movement     | three times faster                |
| Ctrl                 | crouch, at half speed (overrides Shift) |
| Space                | jump, half a metre up             |
| PageUp/PageDown, Q/E | standing height                   |
| J/L, I/K             | yaw, pitch                        |
| T                    | toggle side movement              |
| Home or R            | reset the view                    |
| Esc                  | exit (always)                     |

Touch any control and the screensaver lets go: the automatic drift along the
wall stops and the view stays where you put it. Both come back after 20
seconds of no input.

Jumping again the instant you land, while moving, chains. From the third
jump in a row you travel twice as fast and the hop grows: half a metre and
0.64 s in the air become a full metre and 1.15 s by the fifth. With Shift
that is six times normal speed. Stop jumping for half a second and the chain
is gone.

With **Mouse controlled rotation** on, moving the mouse looks around and a
click exits.

## Layout

```
src/            portable C11 core (SDL3 + OpenGL 3.3)
src/shaders/    GLSL, embedded into the binary at build time
res/win32/      dialog, manifest, version info
cmake/          toolchain file + shader embedding script
tools/          env / build / install helpers
docs/           design and tooling notes
third_party/    stb_image_write.h (public domain)
```

See [docs/DESIGN.md](docs/DESIGN.md) for how the wall is simulated and drawn.

## License

MIT. Dependencies: SDL3 (zlib), stb (public domain / MIT).
