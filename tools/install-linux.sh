#!/usr/bin/env bash
# Installs the built screensaver into XScreenSaver and makes it the active
# one for the current user - the Linux counterpart of install-windows.ps1.
#
#   System-wide (default, asks for sudo): the binary goes into XScreenSaver's
#   hack directory and its settings page into XScreenSaver's config directory,
#   so it can be tuned in xscreensaver-settings.
#   --user: no sudo; the binary goes into ~/.local/libexec/theblackwall. It
#   runs, but xscreensaver-settings only reads system-wide settings pages, so
#   it has no settings page there (the defaults and settings.conf apply).
#
# In both cases ~/.xscreensaver gets a "The Black Wall" entry at the top of
# its list (a copy of the old file is kept), and unless --no-activate is given
# XScreenSaver is set to show only this screensaver.
#
# Usage:  tools/install-linux.sh [--user] [--no-activate] [--uninstall]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/linux/theblackwall"
XML="$ROOT/res/linux/theblackwall.xml"
PREFS="$HOME/.xscreensaver"
USER_MODE=0
ACTIVATE=1
UNINSTALL=0
for arg in "$@"; do
    case "$arg" in
        --user)        USER_MODE=1 ;;
        --no-activate) ACTIVATE=0 ;;
        --uninstall)   UNINSTALL=1 ;;
        -h|--help)     sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)             echo "Unknown option: $arg" >&2; exit 2 ;;
    esac
done

fail() { echo "ERROR: $*" >&2; exit 1; }

as_root() {
    if [ "$(id -u)" -eq 0 ]; then "$@"
    elif command -v sudo >/dev/null 2>&1; then sudo "$@"
    else fail "need root for a system-wide install; use --user instead"
    fi
}

# XScreenSaver's hack directory differs between distributions. Take the one
# that already holds hacks; fall back to the most common location.
find_hack_dir() {
    local d
    for d in /usr/libexec/xscreensaver /usr/lib/xscreensaver /usr/lib64/xscreensaver \
             /usr/local/libexec/xscreensaver /usr/local/lib/xscreensaver; do
        if [ -d "$d" ] && [ -n "$(find "$d" -maxdepth 1 -type f -perm -u+x -print -quit 2>/dev/null)" ]; then
            echo "$d"; return
        fi
    done
    echo /usr/libexec/xscreensaver
}

find_config_dir() {
    local d
    for d in /usr/share/xscreensaver/config /usr/local/share/xscreensaver/config; do
        if [ -d "$d" ]; then echo "$d"; return; fi
    done
    echo /usr/share/xscreensaver/config
}

if [ "$USER_MODE" -eq 1 ]; then
    HACK_DIR="$HOME/.local/libexec/theblackwall"
    CONF_DIR=""
else
    HACK_DIR="$(find_hack_dir)"
    CONF_DIR="$(find_config_dir)"
fi
TARGET="$HACK_DIR/theblackwall"

# ---------------------------------------------------------------------------
# ~/.xscreensaver. The "programs" value is an X resource continued over
# lines with a trailing backslash; the entries inside it are separated by a
# literal \n. The entry list is rebuilt with ours first (or without it, when
# uninstalling) and "selected", an index into that list, is moved to match.
# XScreenSaver adds every system hack missing from the file back on its own,
# so a file that lists only ours is complete.
# ---------------------------------------------------------------------------
update_prefs() {
    local entry="$1"      # empty: remove our entry
    local tmp prog
    tmp="$(mktemp "$PREFS.XXXXXX")"
    if [ -f "$PREFS" ]; then
        cp -p "$PREFS" "$PREFS.bak-theblackwall"
        prog="$(cat <<'AWK'
function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
function is_ours(e,   c, n, w) {
    c = e
    sub(/^-[ \t]*/, "", c)                              # disabled flag
    if (match(c, /^[^ \t:"]+:[ \t]*/)) c = substr(c, RLENGTH + 1)   # "GL:"
    if (match(c, /^"[^"]*"[ \t]*/))    c = substr(c, RLENGTH + 1)   # "Name"
    n = split(c, w, /[ \t]+/)
    sub(/.*\//, "", w[1])
    return n > 0 && w[1] == "theblackwall"
}
{ sub(/\r$/, ""); line[++N] = $0 }
END {
    pstart = 0; pend = 0; sel = 0; mode = 0
    for (i = 1; i <= N; i++) {
        if (!pstart && line[i] ~ /^programs:/) {
            pstart = i; raw = substr(line[i], 10); j = i
            while (raw ~ /\\$/ && j < N) { sub(/\\$/, "", raw); raw = raw line[++j] }
            sub(/\\$/, "", raw)
            pend = j; i = j
        } else if (line[i] ~ /^selected:/) sel = i
        else if (line[i] ~ /^mode:/) mode = i
    }
    count = 0; ours = -1
    if (pstart) {
        n = split(raw, piece, /\\n/)
        for (k = 1; k <= n; k++) {
            e = trim(piece[k])
            if (e == "") continue
            if (is_ours(e)) { if (ours < 0) ours = count; count++; continue }
            kept[++nk] = e; count++
        }
    }
    total = nk + (entry != "" ? 1 : 0)
    # where the old selection lands in the new list
    old = -1
    if (sel) { v = line[sel]; sub(/^selected:[ \t]*/, "", v); if (v ~ /^-?[0-9]+$/) old = v + 0 }
    newsel = old
    if (old >= 0) {
        if (old == ours)          newsel = (entry != "" ? 0 : -1)
        else {
            if (ours >= 0 && old > ours) newsel--
            if (entry != "")             newsel++
        }
    }
    if (entry != "" && activate == 1) newsel = 0

    for (i = 1; i <= N; i++) {
        if (i == pstart) {
            if (total == 0) { print "programs:"; i = pend; continue }
            # the same shape XScreenSaver writes: every entry ends in \n\
            # and a blank line closes the value
            printf "programs:\t\t\t\t\t\t\t      \\\n"
            if (entry != "") printf "  %s \\n\\\n", entry
            for (k = 1; k <= nk; k++) printf "  %s \\n\\\n", kept[k]
            print ""
            i = pend; continue
        }
        if (i == sel)  { printf "selected:\t%d\n", newsel; continue }
        if (i == mode && entry != "" && activate == 1) { print "mode:\t\tone"; continue }
        print line[i]
    }
    if (!pstart && entry != "") {
        printf "programs:\t\t\t\t\t\t\t      \\\n  %s \\n\\\n\n", entry
    }
    if (entry != "" && activate == 1) {
        if (!mode) print "mode:\t\tone"
        if (!sel)  print "selected:\t0"
    }
}
AWK
)"
        awk -v entry="$entry" -v activate="$ACTIVATE" "$prog" "$PREFS" > "$tmp"
    elif [ -n "$entry" ]; then
        {
            echo "# XScreenSaver Preferences File"
            echo "# Started by The Black Wall's installer. XScreenSaver adds its own"
            echo "# screensavers to this list and fills in the other settings when it"
            echo "# next saves the file."
            if [ "$ACTIVATE" -eq 1 ]; then
                printf 'mode:\t\tone\n'
                printf 'selected:\t0\n'
            fi
            printf 'programs:\t\t\t\t\t\t\t      \\\n  %s \\n\\\n\n' "$entry"
        } > "$tmp"
    else
        rm -f "$tmp"
        return 0
    fi
    chmod 600 "$tmp"
    mv "$tmp" "$PREFS"
}

# ---------------------------------------------------------------------------
if [ "$UNINSTALL" -eq 1 ]; then
    if [ "$USER_MODE" -eq 1 ]; then
        rm -f "$TARGET"
        rmdir "$HACK_DIR" 2>/dev/null || true
    else
        as_root rm -f "$TARGET" "$CONF_DIR/theblackwall.xml"
    fi
    update_prefs ""
    echo "Removed $TARGET and its entry in $PREFS."
    exit 0
fi

[ -x "$BIN" ] || fail "build first: ./2-build-for-linux.sh"

if [ "$USER_MODE" -eq 1 ]; then
    install -D -m 755 "$BIN" "$TARGET"
else
    as_root install -D -m 755 "$BIN" "$TARGET"
    as_root install -D -m 644 "$XML" "$CONF_DIR/theblackwall.xml"
fi
update_prefs "GL: \"The Black Wall\"  $TARGET --root"

echo "Installed $TARGET"
[ -n "$CONF_DIR" ] && echo "Installed $CONF_DIR/theblackwall.xml"
if [ "$ACTIVATE" -eq 1 ]; then
    echo "Selected it as the only screensaver in $PREFS."
else
    echo "Added it to the screensaver list in $PREFS."
fi
[ -f "$PREFS.bak-theblackwall" ] && echo "The previous file is saved as $PREFS.bak-theblackwall."
echo

# What still stands between this and a running screensaver.
if ! command -v xscreensaver >/dev/null 2>&1; then
    echo "XScreenSaver is not installed. Install it with your package manager, e.g."
    echo "    sudo apt install xscreensaver xscreensaver-gl     (Debian, Ubuntu)"
    echo "    sudo dnf install xscreensaver                     (Fedora)"
    echo "    sudo pacman -S xscreensaver                       (Arch)"
    echo "then start it with:  xscreensaver --no-splash &"
elif ! pgrep -u "$(id -u)" -x xscreensaver >/dev/null 2>&1; then
    echo "XScreenSaver is installed but not running. Start it with:"
    echo "    xscreensaver --no-splash &"
else
    echo "XScreenSaver is running and picks up the change by itself."
fi
echo "Adjust the timeout and options with:  xscreensaver-settings"
case "${XDG_SESSION_TYPE:-}:${XDG_CURRENT_DESKTOP:-}" in
    wayland:*)
        echo
        echo "Note: this is a Wayland session. XScreenSaver only blanks X11 sessions;"
        echo "log in to an X11 (Xorg) session to use it, or run the saver directly:"
        echo "    $TARGET -s" ;;
    *GNOME*|*KDE*)
        echo
        echo "Note: GNOME and KDE have their own screen lockers, which run instead of"
        echo "XScreenSaver unless you turn theirs off." ;;
esac
