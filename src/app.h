/* Application shell: window creation, input rules, frame pacing. */
#ifndef BW_APP_H
#define BW_APP_H
#include "settings.h"

typedef enum RunMode {
    MODE_FULLSCREEN,   /* real screensaver run (/s): covers all monitors */
    MODE_PREVIEW,      /* inside the Windows screensaver dialog (/p hwnd) */
    MODE_WINDOW,       /* developer window (/w) */
    MODE_EMBEDDED      /* inside an X11 window XScreenSaver owns (--root,
                          --window-id): full quality, input left to the owner */
} RunMode;

typedef struct AppConfig {
    RunMode     mode;
    void       *parent_hwnd;    /* MODE_PREVIEW: HWND; MODE_EMBEDDED: X11 window id */
    Settings    settings;
    int         win_w, win_h;   /* MODE_WINDOW size */
    const char *dump_path;      /* write a PNG of the frame after frame_limit */
    int         frame_limit;    /* 0 = run until exit */
    float       cpu_override;   /* <0 = real CPU load */
    double      net_override;   /* <0 = real network throughput (bits/s) */
    float       figure_every;   /* metres of travel along the wall between figures */
    float       start_yaw;      /* initial camera yaw in radians (testing) */
    int         pose;           /* figure positioning tool (/w --pose) */
    int         pose_model;     /* figure shown first in the pose tool */
    float       drift_mps;      /* testing: drive the drift at this speed, past the slider cap */
    int         hop_test;       /* testing: hop continuously and log the chain */
    int         scar_test;      /* testing: start with a fresh scar of pose_model on the wall */
    float       start_z;        /* testing: initial camera distance from the wall (0 = default) */
} AppConfig;

/* Returns a process exit code. */
int app_run(const AppConfig *cfg);
#endif
