#!/usr/bin/env bash
# End to end: install with tools/install-linux.sh, start a real XScreenSaver,
# blank the screen and check that The Black Wall is what it shows. Run under
# Xvfb:
#   xvfb-run -a -s "-screen 0 1280x720x24 -noreset" .github/scripts/xscreensaver-e2e.sh
# Writes logs and a screenshot to out/xscreensaver.
set -euo pipefail

OUT=out/xscreensaver
mkdir -p "$OUT"
step() { printf '\n=== %s\n' "$*"; }
die()  {
    printf 'FAIL: %s\n' "$*" >&2
    xscreensaver-command -exit >/dev/null 2>&1 || true
    exit 1
}
IM=convert
command -v magick >/dev/null 2>&1 && IM=magick

# Software rendering at full density is slow; the settings file keeps the
# frame times short (the saver is started with no options of its own).
mkdir -p "$HOME/.config/theblackwall"
printf 'density = 80\nfps = 30\n' > "$HOME/.config/theblackwall/settings.conf"

step "install"
rm -f "$HOME/.xscreensaver"
tools/install-linux.sh | tee "$OUT/install.txt"
# no password prompt when the test unblanks
printf 'lock:\t\tFalse\n' >> "$HOME/.xscreensaver"
cp "$HOME/.xscreensaver" "$OUT/dot-xscreensaver.txt"
cat "$OUT/dot-xscreensaver.txt"
HACK_BIN="$(sed -n 's/.*"The Black Wall"[[:space:]]*\([^[:space:]]*\).*/\1/p' "$HOME/.xscreensaver")"
[ -x "$HACK_BIN" ] || die "installed binary not found: $HACK_BIN"
echo "hack: $HACK_BIN"
page="$(sed -n 's/^Installed \(.*\.xml\)$/\1/p' "$OUT/install.txt")"
[ -n "$page" ] && [ -f "$page" ] || die "settings page not installed"
echo "settings page: $page"

step "start the daemon"
xscreensaver --version 2>&1 | head -n1 || true
xscreensaver -no-splash -verbose > "$OUT/daemon.log" 2>&1 &
DAEMON=$!
for _ in $(seq 100); do
    xscreensaver-command -time >/dev/null 2>&1 && break
    sleep 0.2
done
xscreensaver-command -time || die "the daemon did not come up"

step "blank the screen"
xscreensaver-command -activate
pid=""
for _ in $(seq 150); do
    pid="$(pgrep -f "theblackwall --root" | head -n1 || true)"
    [ -n "$pid" ] && break
    sleep 0.2
done
[ -n "$pid" ] || { cat "$OUT/daemon.log"; die "XScreenSaver did not start the hack"; }
echo "hack running as pid $pid: $(tr '\0' ' ' < "/proc/$pid/cmdline")"
echo "XSCREENSAVER_WINDOW=$(tr '\0' '\n' < "/proc/$pid/environ" | sed -n 's/^XSCREENSAVER_WINDOW=//p')"

sleep 10    # the fade, then a few software-rendered frames
xwininfo -root -tree > "$OUT/tree.txt"
grep -q '"The Black Wall"' "$OUT/tree.txt" || die "no The Black Wall window on screen"
# ours must sit inside XScreenSaver's window: top-level windows are the ones
# xwininfo indents by exactly five spaces
awk '/"The Black Wall"/ { print; exit }' "$OUT/tree.txt"
if grep -E '^ {5}0x[0-9a-f]+ "The Black Wall"' "$OUT/tree.txt"; then
    die "the hack's window is a top-level window"
fi
import -window root "$OUT/screen.png"
r="$($IM "$OUT/screen.png" -format '%[fx:mean.r]' info:)"
g="$($IM "$OUT/screen.png" -format '%[fx:mean.g]' info:)"
echo "screen mean red $r, green $g"
awk -v r="$r" -v g="$g" 'BEGIN { exit !(r > 0.01 && r > 2 * g) }' || die "the screen does not show the wall"

step "unblank"
xscreensaver-command -deactivate
for _ in $(seq 100); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.1
done
if kill -0 "$pid" 2>/dev/null; then die "the hack is still running after unblanking"; fi
echo "the hack has exited"

xscreensaver-command -exit || true
wait "$DAEMON" 2>/dev/null || true
cp "$HOME/.xscreensaver" "$OUT/dot-xscreensaver-after.txt" 2>/dev/null || true
printf '\nXScreenSaver end-to-end test passed.\n'
