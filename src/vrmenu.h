/* The menu in the headset: a small panel hanging in the air, pointed at with
 * a controller. Either hand's upper face button (B on the right hand's
 * controller, Y on the left) puts it up and takes it down.
 *
 * It draws itself into a texture with the HUD's own text engine (hud.h), and
 * the renderer hangs that texture on a quad in the world, the same for both
 * eyes. For now it holds exactly two rows: Auto move on/off, and Exit.
 */
#ifndef BW_VRMENU_H
#define BW_VRMENU_H
#include "mathx.h"

typedef struct VrMenu {
    int      open;
    int      place_pending;     /* hang it in front of the head, next frame */
    int      hover;             /* the row the pointer is on, -1 for none */
    unsigned tex;                /* what the panel shows */
    unsigned fbo;
    int      w, h;               /* the page, in its own pixels */
    /* where the panel hangs, in the play space */
    vec3     pos;
    basis3   basis;
    float    width_m, height_m;
} VrMenu;

/* What the menu asks the app to do, returned by vrmenu_click. */
enum {
    VRMENU_NONE = 0,
    VRMENU_AUTO_MOVE,           /* toggle side_movement */
    VRMENU_EXIT                 /* leave the screensaver */
};

int  vrmenu_init(VrMenu *m);
void vrmenu_shutdown(VrMenu *m);
void vrmenu_open(VrMenu *m, int open);
/* Aims the pointer: the ray in the play space. Returns 1 if it is on the
 * panel, and puts where in `u`, `v` (0..1). */
int  vrmenu_aim(VrMenu *m, vec3 from, vec3 dir, float *u, float *v);
/* Just pointing: lights up the row under the pointer. */
void vrmenu_hover(VrMenu *m, float u, float v);
/* The trigger, on the row under the pointer. */
int  vrmenu_click(VrMenu *m, float u, float v);
/* Redraws the panel's texture. */
void vrmenu_paint(VrMenu *m, int auto_move_on);
/* Hangs the panel's texture on a quad in the world, in whatever framebuffer
 * is bound. Only draws while the menu is open. */
void vrmenu_draw(const VrMenu *m, mat4 view_proj);
#endif
