/* Test helper: a plain X11 window standing in for the one XScreenSaver hands
 * to a hack. Prints its id on stdout, then waits to be killed; the window
 * goes away with the connection.
 *   xparent [WxH+X+Y]
 * Built by .github/scripts/linux-smoke.sh. */
#include <X11/Xlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int w = 640, h = 360, x = 40, y = 40;
    if (argc > 1 && sscanf(argv[1], "%dx%d+%d+%d", &w, &h, &x, &y) != 4) {
        fprintf(stderr, "usage: xparent [WxH+X+Y]\n");
        return 2;
    }
    const char *name = getenv("DISPLAY");
    Display *d = XOpenDisplay(NULL);
    if (!d) {
        fprintf(stderr, "xparent: cannot open display %s (%s)\n",
                name ? name : "(DISPLAY unset)", strerror(errno));
        return 1;
    }
    int screen = DefaultScreen(d);
    Window win = XCreateSimpleWindow(d, RootWindow(d, screen), x, y,
                                     (unsigned)w, (unsigned)h, 0,
                                     BlackPixel(d, screen), BlackPixel(d, screen));
    XStoreName(d, win, "xparent");
    XMapWindow(d, win);
    XSync(d, False);
    printf("0x%lx\n", (unsigned long)win);
    fflush(stdout);
    for (;;) pause();
}
