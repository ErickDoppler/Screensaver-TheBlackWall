/* Entry point. Decodes the screensaver command line and dispatches.
 *
 * Windows launches a .scr with one of:
 *   /s            run the screensaver (fullscreen)
 *   /p <hwnd>     draw a live preview inside the given window
 *   /c[:<hwnd>]   show the settings dialog          (also: no arguments)
 *   /a <hwnd>     change password (legacy, ignored)
 *
 * XScreenSaver (Linux) launches a hack with one of:
 *   --root              draw into $XSCREENSAVER_WINDOW (the saver's window);
 *                       without it, run fullscreen like -s
 *   --window-id <id>    draw into that window (the settings dialog preview)
 * With no mode at all the Linux build opens a window, as /w does.
 *
 * Developer extras (any platform):
 *   /w | --window [WxH]     run in a resizable window
 *   --dump <file.png> --frames <n>   render n frames, save the last one, exit
 *   --cpu <0..1>  --net <bits/s>     fake system load for testing the wall
 *   --log <file>                      append diagnostics to a file
 *   --<setting> <value>               override any setting (see settings.h)
 */
#include "app.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void lower_ascii(char *s) { for (; *s; ++s) *s = (char)tolower((unsigned char)*s); }

static void *parse_hwnd(const char *text) {
    if (!text) return NULL;
    unsigned long long v = strtoull(text, NULL, 10);
    if (v == 0) v = strtoull(text, NULL, 16);
    return (void *)(uintptr_t)v;
}

#ifndef _WIN32
/* X11 window ids come as "0x2c00001" or in decimal, as in XScreenSaver's
 * vroot.h. Returns NULL for anything else. */
static void *parse_xid(const char *text) {
    if (!text || !*text) return NULL;
    char *end;
    unsigned long long v = strtoull(text, &end, 0);
    while (*end == ' ') end++;
    return *end ? NULL : (void *)(uintptr_t)v;
}
#endif

int main(int argc, char **argv) {
    AppConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.mode = MODE_FULLSCREEN;
    cfg.win_w = 1280;
    cfg.win_h = 720;
    cfg.cpu_override = -1.f;
    cfg.net_override = -1.0;
    cfg.figure_every = 100.f;  /* a figure every 100 m of travel along the wall */
    settings_load(&cfg.settings);

    int mode_given = 0, want_config = 0;
    void *config_parent = NULL;

    for (int i = 1; i < argc; ++i) {
        char opt[64];
        const char *raw = argv[i];
        if (raw[0] != '/' && raw[0] != '-') continue;
        snprintf(opt, sizeof opt, "%s", raw + (raw[0] == '/' ? 1 : (raw[1] == '-' ? 2 : 1)));
        lower_ascii(opt);

        /* Screensaver switches (single letter, optional ":hwnd") */
        if (opt[0] && (opt[1] == 0 || opt[1] == ':') && strchr("scpaw", opt[0])) {
            const char *inline_val = opt[1] == ':' ? opt + 2 : NULL;
            switch (opt[0]) {
            case 's': cfg.mode = MODE_FULLSCREEN; mode_given = 1; break;
            case 'w': cfg.mode = MODE_WINDOW; mode_given = 1; break;
            case 'p':
                cfg.mode = MODE_PREVIEW; mode_given = 1;
                cfg.parent_hwnd = parse_hwnd(inline_val ? inline_val : (i + 1 < argc ? argv[++i] : NULL));
                break;
            case 'c':
                want_config = 1; mode_given = 1;
                config_parent = parse_hwnd(inline_val ? inline_val : (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0]) ? argv[++i] : NULL));
                break;
            case 'a':
                if (!inline_val && i + 1 < argc) ++i;
                return 0;
            }
            continue;
        }
#ifndef _WIN32
        if (!strcmp(opt, "root")) {
            cfg.parent_hwnd = parse_xid(getenv("XSCREENSAVER_WINDOW"));
            cfg.mode = cfg.parent_hwnd ? MODE_EMBEDDED : MODE_FULLSCREEN;
            mode_given = 1;
            continue;
        }
        if (!strcmp(opt, "window-id") && i + 1 < argc) {
            cfg.parent_hwnd = parse_xid(argv[++i]);
            if (!cfg.parent_hwnd) { plat_log("bad window id: %s", argv[i]); return 1; }
            cfg.mode = MODE_EMBEDDED;
            mode_given = 1;
            continue;
        }
#endif
        if (!strcmp(opt, "window")) {
            cfg.mode = MODE_WINDOW; mode_given = 1;
            if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0])) {
                int w = 0, h = 0;
                if (sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) { cfg.win_w = w; cfg.win_h = h; }
            }
            continue;
        }
        if (!strcmp(opt, "dump") && i + 1 < argc)   { cfg.dump_path = argv[++i]; continue; }
        if (!strcmp(opt, "frames") && i + 1 < argc) { cfg.frame_limit = atoi(argv[++i]); continue; }
        if (!strcmp(opt, "cpu") && i + 1 < argc)    { cfg.cpu_override = (float)atof(argv[++i]); continue; }
        if (!strcmp(opt, "net") && i + 1 < argc)    { cfg.net_override = atof(argv[++i]); continue; }
        if (!strcmp(opt, "log") && i + 1 < argc)    { plat_log_set_file(argv[++i]); continue; }
        if ((!strcmp(opt, "figure-every") || !strcmp(opt, "ghost-after")) && i + 1 < argc) { cfg.figure_every = (float)atof(argv[++i]); continue; }
        if (!strcmp(opt, "model") && i + 1 < argc) { cfg.pose_model = atoi(argv[++i]); continue; }
        if (!strcmp(opt, "scar-test")) { cfg.scar_test = 1; continue; }
        if (!strcmp(opt, "hop-test")) { cfg.hop_test = 1; continue; }
        if (!strcmp(opt, "drift") && i + 1 < argc) { cfg.drift_mps = (float)atof(argv[++i]); continue; }
        if (!strcmp(opt, "back") && i + 1 < argc) { cfg.start_z = (float)atof(argv[++i]); continue; }
        if (!strcmp(opt, "yaw") && i + 1 < argc) { cfg.start_yaw = (float)(atof(argv[++i]) * 3.14159265 / 180.0); continue; }
        if (!strcmp(opt, "pose")) { cfg.pose = 1; cfg.mode = MODE_WINDOW; mode_given = 1; continue; }
        if (settings_parse_arg(&cfg.settings, argc, argv, &i)) continue;
        plat_log("unknown argument: %s", raw);
    }
    if (cfg.dump_path && cfg.frame_limit <= 0) cfg.frame_limit = 60;

#ifdef _WIN32
    if (want_config || !mode_given) {
        if (!plat_win32_config_dialog(config_parent)) return 0;
        /* the user pressed "Position the ghosts...": run the pose tool here */
        settings_load(&cfg.settings);
        cfg.pose = 1;
        cfg.mode = MODE_WINDOW;
    }
    if (cfg.mode == MODE_PREVIEW && !cfg.parent_hwnd) return 1;
#else
    (void)want_config; (void)config_parent;
    if (!mode_given) cfg.mode = MODE_WINDOW;
#endif
    return app_run(&cfg);
}
