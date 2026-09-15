/* Platform services used by the portable core. Implemented per OS. */
#ifndef BW_PLATFORM_H
#define BW_PLATFORM_H
#include <stdint.h>

/* Persistent settings store (Windows: HKCU\Software\TheBlackWall). */
int plat_store_read_int(const char *key, int *out);   /* 1 if the key existed */
int plat_store_write_int(const char *key, int value); /* 1 on success */

/* Cumulative system counters; the sampler turns them into rates.
 * cpu_busy/cpu_total are in arbitrary but consistent ticks;
 * net_bytes is total bytes in+out over real network adapters. */
int plat_stats_read(uint64_t *cpu_busy, uint64_t *cpu_total, uint64_t *net_bytes);

/* Diagnostics. Goes to the debugger/stderr and, if enabled, a log file. */
void plat_log_set_file(const char *path);
void plat_log(const char *fmt, ...);

#ifdef _WIN32
/* Bounding box of all monitors, in virtual-screen pixels. */
void  plat_win32_virtual_screen(int *x, int *y, int *w, int *h);
/* Creates a child HWND filling `parent` (the little monitor in the Windows
 * screensaver dialog). Returns the child HWND and its size in pixels. */
void *plat_win32_create_preview_child(void *parent_hwnd, int *w, int *h);
int   plat_win32_window_alive(void *hwnd);
/* Runs the modal settings dialog (the /c mode). Returns 1 if the user asked
 * for the figure positioning tool, which the caller then runs in-process. */
int   plat_win32_config_dialog(void *parent_hwnd);
#endif
#endif
