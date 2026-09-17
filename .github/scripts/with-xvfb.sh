#!/usr/bin/env bash
# Runs a command against a private Xvfb, keeping the server's own log, which
# xvfb-run throws away. The log is printed when the command fails or the
# server has died.
#   .github/scripts/with-xvfb.sh <command> [args...]
set -uo pipefail

mkdir -p out
LOG="out/xvfb-$(basename "$1" .sh).log"
DISPLAY_NUM=99
while [ -e "/tmp/.X11-unix/X$DISPLAY_NUM" ]; do DISPLAY_NUM=$((DISPLAY_NUM + 1)); done

Xvfb ":$DISPLAY_NUM" -screen 0 1280x720x24 -noreset -nolisten tcp -ac > "$LOG" 2>&1 &
XVFB=$!
export DISPLAY=":$DISPLAY_NUM"
for _ in $(seq 100); do
    xdpyinfo >/dev/null 2>&1 && break
    kill -0 "$XVFB" 2>/dev/null || break
    sleep 0.1
done
if ! xdpyinfo >/dev/null 2>&1; then
    echo "Xvfb did not start:"; cat "$LOG"; exit 1
fi
echo "Xvfb $DISPLAY (pid $XVFB) is up"

"$@"
code=$?

if ! kill -0 "$XVFB" 2>/dev/null; then
    wait "$XVFB"; echo "Xvfb had exited, status $?"
    [ "$code" -eq 0 ] && code=1
fi
if [ "$code" -ne 0 ]; then
    echo; echo "----- Xvfb log ($LOG) -----"; cat "$LOG"
fi
kill "$XVFB" 2>/dev/null
wait "$XVFB" 2>/dev/null
exit "$code"
