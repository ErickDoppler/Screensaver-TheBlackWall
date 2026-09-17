/* Linux implementation of platform.h: a settings file, CPU/network counters
 * from /proc, and the X11 calls for drawing inside XScreenSaver's window. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "platform.h"

/* ------------------------------------------------------------------ log -- */
static FILE *g_log;

void plat_log_set_file(const char *path) {
    if (g_log) fclose(g_log);
    g_log = fopen(path, "a");
}

void plat_log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stderr, "theblackwall: %s\n", buf);
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}

/* ------------------------------------------------------------- settings -- */
/* $XDG_CONFIG_HOME/theblackwall/settings.conf, one "key = value" per line.
 * It is read once and rewritten whole (via a temporary file) on each write. */
#define STORE_MAX 64
typedef struct { char key[48]; int value; } StoreEntry;
static StoreEntry g_store[STORE_MAX];
static int g_store_n = -1;              /* -1: not loaded yet */

static int config_dir(char *out, size_t cap) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int n;
    if (xdg && xdg[0] == '/') n = snprintf(out, cap, "%s/theblackwall", xdg);
    else if (home && *home)   n = snprintf(out, cap, "%s/.config/theblackwall", home);
    else return 0;
    return n > 0 && (size_t)n < cap;
}

static int store_path(char *out, size_t cap, const char *suffix) {
    char dir[512];
    if (!config_dir(dir, sizeof dir)) return 0;
    int n = snprintf(out, cap, "%s/settings.conf%s", dir, suffix);
    return n > 0 && (size_t)n < cap;
}

static void store_load(void) {
    if (g_store_n >= 0) return;
    g_store_n = 0;
    char path[600];
    if (!store_path(path, sizeof path, "")) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (g_store_n < STORE_MAX && fgets(line, sizeof line, f)) {
        char key[48], text[64];
        if (line[0] == '#') continue;
        if (sscanf(line, " %47[A-Za-z0-9_-] = %63s", key, text) != 2) continue;
        /* decimal, or a color written as #RRGGBB / 0xRRGGBB */
        const char *digits = text;
        int base = 10;
        if (text[0] == '#') { digits = text + 1; base = 16; }
        else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { digits = text + 2; base = 16; }
        char *end;
        long value = strtol(digits, &end, base);
        if (end == digits || *end) continue;
        memcpy(g_store[g_store_n].key, key, sizeof key);
        g_store[g_store_n].value = (int)value;
        g_store_n++;
    }
    fclose(f);
}

static int mkdir_p(const char *path) {
    char tmp[512];
    int n = snprintf(tmp, sizeof tmp, "%s", path);
    if (n <= 0 || (size_t)n >= sizeof tmp) return 0;
    for (char *p = tmp + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return 0;
        *p = '/';
    }
    return mkdir(tmp, 0700) == 0 || errno == EEXIST;
}

int plat_store_read_int(const char *key, int *out) {
    store_load();
    for (int k = 0; k < g_store_n; ++k)
        if (!strcmp(g_store[k].key, key)) { *out = g_store[k].value; return 1; }
    return 0;
}

int plat_store_write_int(const char *key, int value) {
    if (strlen(key) >= sizeof g_store[0].key) return 0;
    store_load();
    int k = 0;
    while (k < g_store_n && strcmp(g_store[k].key, key)) ++k;
    if (k == g_store_n) {
        if (g_store_n == STORE_MAX) return 0;
        snprintf(g_store[k].key, sizeof g_store[k].key, "%s", key);
        g_store_n++;
    }
    g_store[k].value = value;

    char dir[512], path[600], tmp[600];
    if (!config_dir(dir, sizeof dir) || !mkdir_p(dir)) return 0;
    if (!store_path(path, sizeof path, "") || !store_path(tmp, sizeof tmp, ".tmp")) return 0;
    FILE *f = fopen(tmp, "w");
    if (!f) return 0;
    fprintf(f, "# The Black Wall settings. Command-line options override these.\n");
    for (int j = 0; j < g_store_n; ++j) fprintf(f, "%s = %d\n", g_store[j].key, g_store[j].value);
    int ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp, path) != 0) { unlink(tmp); return 0; }
    return 1;
}

/* ---------------------------------------------------------------- stats -- */
static int read_cpu(uint64_t *busy, uint64_t *total) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;
    /* cpu  user nice system idle iowait irq softirq steal guest guest_nice;
     * guest time is already counted in user, so it is left out. */
    unsigned long long v[8] = {0};
    int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
    fclose(f);
    if (n < 4) return 0;
    uint64_t sum = 0;
    for (int k = 0; k < 8; ++k) sum += v[k];
    uint64_t idle = v[3] + v[4];        /* waiting on I/O is not load */
    *total = sum;
    *busy = sum - idle;
    return 1;
}

/* A real adapter has a device behind it in sysfs; loopback, bridges, veth,
 * tun and the like do not. */
static int is_hardware_if(const char *name) {
    char path[64];
    struct stat st;
    /* interface names are at most 15 characters (IFNAMSIZ - 1) */
    snprintf(path, sizeof path, "/sys/class/net/%.15s/device", name);
    return stat(path, &st) == 0;
}

static uint64_t read_net(void) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return 0;
    char line[512];
    uint64_t hw = 0, all = 0;
    /* two header lines, then "  eth0: rx_bytes rx_packets ... tx_bytes ..." */
    for (int skip = 0; skip < 2 && fgets(line, sizeof line, f); ++skip) {}
    while (fgets(line, sizeof line, f)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = 0;
        char *name = line;
        while (*name == ' ') name++;
        if (!strcmp(name, "lo")) continue;
        unsigned long long rx, tx, skipv;
        if (sscanf(colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &rx, &skipv, &skipv, &skipv, &skipv, &skipv, &skipv, &skipv, &tx) != 9)
            continue;
        all += rx + tx;
        if (is_hardware_if(name)) hw += rx + tx;
    }
    fclose(f);
    /* Like the Windows build: physical adapters, or everything but loopback
     * when there are none (containers, some VMs). */
    return hw ? hw : all;
}

int plat_stats_read(uint64_t *cpu_busy, uint64_t *cpu_total, uint64_t *net_bytes) {
    if (!read_cpu(cpu_busy, cpu_total)) return 0;
    *net_bytes = read_net();
    return 1;
}

/* ------------------------------------------------------------------ x11 -- */
/* A connection of our own, separate from SDL's. X errors on it (a window
 * that has gone away) are expected, so they are caught instead of letting
 * Xlib's default handler end the process. */
static Display *g_dpy;
static int g_x_error;

/* Set by plat_x11_embed: our window and the owner's. g_embed_lost becomes 1
 * once either is known to be gone; it is read from a signal handler. */
static unsigned long g_embedded, g_embed_parent;
static volatile sig_atomic_t g_embed_lost;

static int x_error_handler(Display *d, XErrorEvent *e) {
    (void)d;
    g_x_error = e->error_code ? e->error_code : 1;
    return 0;
}

static Display *x_display(void) {
    if (!g_dpy) g_dpy = XOpenDisplay(NULL);
    return g_dpy;
}

/* Brackets a group of requests so errors land in g_x_error. The handler is
 * process-wide, so the previous one (SDL's) is put back afterwards. */
typedef int (*XErrorFn)(Display *, XErrorEvent *);
static XErrorFn x_trap_begin(Display *d) {
    XSync(d, False);
    g_x_error = 0;
    return XSetErrorHandler(x_error_handler);
}

static int x_trap_end(Display *d, XErrorFn previous) {
    XSync(d, False);
    XSetErrorHandler(previous);
    return g_x_error == 0;
}

int plat_x11_window_size(unsigned long win, int *w, int *h) {
    Display *d = x_display();
    if (!d || !win) return 0;
    XWindowAttributes attr;
    XErrorFn prev = x_trap_begin(d);
    Status st = XGetWindowAttributes(d, (Window)win, &attr);
    if (!x_trap_end(d, prev) || !st || attr.width <= 0 || attr.height <= 0) {
        if (win == g_embed_parent) g_embed_lost = 1;   /* ours went with it */
        return 0;
    }
    *w = attr.width;
    *h = attr.height;
    return 1;
}

/* Once embedded, our window dies with XScreenSaver's, usually between two
 * frames: the next buffer swap then fails with BadDrawable on SDL's own
 * connection, and Xlib's default handler would end the process on the spot.
 * This handler stays installed instead; it notes that the window is gone,
 * which the main loop checks every frame, and logs anything else. */
static int x_embed_error_handler(Display *d, XErrorEvent *e) {
    (void)d;
    if ((e->error_code == BadWindow || e->error_code == BadDrawable) &&
        e->resourceid == g_embedded) {
        if (!g_embed_lost) plat_log("our window went away with its parent");
        g_embed_lost = 1;
    } else {
        plat_log("X error %d (request %d.%d) on 0x%lx", e->error_code,
                 e->request_code, e->minor_code, (unsigned long)e->resourceid);
    }
    return 0;
}

/* Software OpenGL (Mesa's llvmpipe) does not survive the failed swap: it
 * carries on with the drawable it could not query and crashes inside the
 * same call. By then the window is known to be gone and there is nothing
 * left to draw into, so that crash is turned into a normal exit. Any other
 * crash keeps its default behaviour. */
static void on_fatal_signal(int sig) {
    if (g_embed_lost) {
        static const char msg[] = "theblackwall: exiting, our window is gone\n";
        ssize_t n = write(STDERR_FILENO, msg, sizeof msg - 1);
        (void)n;
        _exit(0);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

int plat_x11_embed(unsigned long child, unsigned long parent) {
    Display *d = x_display();
    if (!d || !child || !parent) return 0;
    XErrorFn prev = x_trap_begin(d);
    XReparentWindow(d, (Window)child, (Window)parent, 0, 0);
    XMapWindow(d, (Window)child);
    int ok = x_trap_end(d, prev);
    if (!ok) {
        plat_log("X error %d while embedding into window 0x%lx", g_x_error, parent);
        return 0;
    }
    g_embedded = child;
    g_embed_parent = parent;
    XSetErrorHandler(x_embed_error_handler);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_fatal_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    return 1;
}

int plat_x11_embed_lost(void) {
    return g_embed_lost;
}
