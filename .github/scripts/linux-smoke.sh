#!/usr/bin/env bash
# Headless checks of the Linux build. Run under Xvfb with software OpenGL:
#   xvfb-run -a -s "-screen 0 1280x720x24" .github/scripts/linux-smoke.sh
# Writes screenshots and logs to out/smoke.
set -euo pipefail

BIN="${BIN:-build/linux/theblackwall}"
OUT=out/smoke
mkdir -p "$OUT"
# Keep software rendering quick; also exercises the settings file.
export XDG_CONFIG_HOME="$PWD/$OUT/config"
mkdir -p "$XDG_CONFIG_HOME/theblackwall"
printf 'density = 30\nfps = 30\n' > "$XDG_CONFIG_HOME/theblackwall/settings.conf"

step() { printf '\n=== %s\n' "$*"; }
die()  { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

IM=convert
command -v magick >/dev/null 2>&1 && IM=magick

# is_wall <png> [geometry]: the picture (or a crop of it) shows the red wall
is_wall() {
    local crop=()
    [ -n "${2:-}" ] && crop=(-crop "$2" +repage)
    local r g
    r="$($IM "$1" ${crop[@]+"${crop[@]}"} -format '%[fx:mean.r]' info:)"
    g="$($IM "$1" ${crop[@]+"${crop[@]}"} -format '%[fx:mean.g]' info:)"
    echo "  mean red $r, green $g in $1 ${2:-}"
    awk -v r="$r" -v g="$g" 'BEGIN { exit !(r > 0.01 && r > 2 * g) }'
}

# alive <pid>: running (an exited child waiting to be reaped does not count)
alive() { [ -r "/proc/$1/stat" ] && ! grep -q '^[0-9]* (.*) Z' "/proc/$1/stat" 2>/dev/null; }

# wait_gone <pid> <seconds>: true if the process ends in time
wait_gone() {
    local t=0
    while alive "$1"; do
        [ "$t" -ge $(( $2 * 10 )) ] && return 1
        sleep 0.1; t=$((t + 1))
    done
}

xwin_id() { xwininfo -name "$1" 2>/dev/null | awk '/Window id:/ { print $4 }'; }

start_parent() {
    xeyes -geometry 640x360+40+40 &
    PARENT=$!
    PARENT_ID=""
    for _ in $(seq 100); do
        PARENT_ID="$(xwin_id xeyes)"
        [ -n "$PARENT_ID" ] && break
        sleep 0.1
    done
    [ -n "$PARENT_ID" ] || die "the parent window did not appear"
    echo "  parent window $PARENT_ID"
}

glxinfo -B > "$OUT/glxinfo.txt" 2>&1 || true
grep -E "OpenGL (renderer|core profile version)" "$OUT/glxinfo.txt" || true

# ---------------------------------------------------------------------------
step "window mode renders the wall"
timeout 120 "$BIN" --window 640x360 --dump "$OUT/window.png" --frames 30 --log "$OUT/window.log"
cat "$OUT/window.log"
grep -q "start: mode=2 640x360" "$OUT/window.log" || die "unexpected start line"
grep -q "density=30" "$OUT/window.log" || die "settings.conf was not read"
is_wall "$OUT/window.png" || die "window.png does not show the wall"

# ---------------------------------------------------------------------------
step "--window-id draws inside another program's window"
start_parent
"$BIN" --window-id "$PARENT_ID" --log "$OUT/embedded.log" &
HACK=$!
sleep 6
xwininfo -children -id "$PARENT_ID" > "$OUT/embedded-tree.txt"
cat "$OUT/embedded-tree.txt"
grep -q '"The Black Wall"' "$OUT/embedded-tree.txt" || die "no child window inside the parent"
import -window root "$OUT/embedded-screen.png"
is_wall "$OUT/embedded-screen.png" 640x360+40+40 || die "the wall is not visible inside the parent"
alive "$HACK" || die "the embedded process died"

step "the embedded process follows its window when it goes away"
kill "$PARENT"
wait "$PARENT" 2>/dev/null || true
wait_gone "$HACK" 5 || die "still running 5 s after its window was destroyed"
cat "$OUT/embedded.log"
grep -q "start: mode=3 640x360" "$OUT/embedded.log" || die "unexpected start line"

# ---------------------------------------------------------------------------
step "--root uses \$XSCREENSAVER_WINDOW and exits cleanly on SIGTERM"
start_parent
XSCREENSAVER_WINDOW="$PARENT_ID" "$BIN" --root --log "$OUT/root.log" &
HACK=$!
sleep 4
kill -TERM "$HACK"
wait_gone "$HACK" 3 || die "did not exit on SIGTERM"
code=0; wait "$HACK" || code=$?
[ "$code" -eq 0 ] || die "exit code $code after SIGTERM"
cat "$OUT/root.log"
grep -q "start: mode=3" "$OUT/root.log" || die "--root did not embed"
kill "$PARENT"; wait "$PARENT" 2>/dev/null || true

step "--root without \$XSCREENSAVER_WINDOW runs fullscreen"
timeout 120 env -u XSCREENSAVER_WINDOW "$BIN" --root --frames 20 --dump "$OUT/fullscreen.png" --log "$OUT/fullscreen.log"
cat "$OUT/fullscreen.log"
grep -q "start: mode=0" "$OUT/fullscreen.log" || die "expected fullscreen mode"
is_wall "$OUT/fullscreen.png" || die "fullscreen.png does not show the wall"

step "a bad window id is refused"
if "$BIN" --window-id 0x7fffffff --frames 1 2> "$OUT/badid.txt"; then die "accepted a window that does not exist"; fi
cat "$OUT/badid.txt"
if "$BIN" --window-id nonsense 2> /dev/null; then die "accepted a malformed window id"; fi

printf '\nAll smoke tests passed.\n'
