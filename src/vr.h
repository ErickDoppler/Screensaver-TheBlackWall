/* The headset, through OpenXR - which is what both the Oculus runtime and
 * SteamVR speak, so one path serves both.
 *
 * No loader library is linked or shipped: the active runtime is found in the
 * registry, its own DLL is loaded, and every entry point comes from that (see
 * vr.c). The screensaver stays a single file with nothing beside it, and a
 * machine with no runtime simply reports no headset.
 *
 * The world is drawn twice a frame, once per eye, into the runtime's own
 * images (see render.c).
 */
#ifndef BW_VR_H
#define BW_VR_H
#include "mathx.h"

#define VR_HANDS 2                 /* 0 left, 1 right */

/* One eye, for this frame. */
typedef struct VrView {
    /* the runtime's frustum, as the tangents of its four half-angles: an eye
     * looks off to one side of its own screen, so these are not symmetric */
    float  tan_l, tan_r, tan_d, tan_u;
    basis3 basis;                  /* where this eye looks, in the play space */
    vec3   pos;                    /* where this eye is, metres from its origin */
    int    w, h;
    unsigned tex;                  /* the runtime's image to draw into */
} VrView;

/* What the controllers are doing. Edges are true on the frame a button goes
 * down, so a caller need not remember the last one. */
typedef struct VrInput {
    int    active[VR_HANDS];       /* the runtime is tracking this hand */
    basis3 aim_basis[VR_HANDS];    /* the pointer ray: for the menu */
    vec3   aim_pos[VR_HANDS];
    basis3 grip_basis[VR_HANDS];   /* the hand itself */
    vec3   grip_pos[VR_HANDS];
    float  squeeze[VR_HANDS];      /* the middle-finger grip, 0..1 */
    float  trigger[VR_HANDS];      /* the index trigger, 0..1 */
    float  stick_x[VR_HANDS], stick_y[VR_HANDS];
    int    stick_click[VR_HANDS], stick_click_edge[VR_HANDS];
    int    lower[VR_HANDS], lower_edge[VR_HANDS];   /* A (right), X (left) */
    int    upper[VR_HANDS], upper_edge[VR_HANDS];   /* B (right), Y (left) */
} VrInput;

/* Is there a runtime with a headset on this machine? Cheap, and safe to call
 * before the GL context exists. */
int  vr_present(void);

/* Starts the session on the current GL context. 0 if it could not, with the
 * reason in the log. */
int  vr_init(void);
void vr_shutdown(void);
int  vr_running(void);             /* a session is up and drawing */

/* Waits for the runtime's own cadence and takes this frame's poses.
 * Returns 0 when the runtime says not to draw (the headset is off the head,
 * the session is idle): the caller should still keep simulating. */
int  vr_begin_frame(void);
int  vr_should_render(void);
int  vr_view_count(void);
const VrView *vr_view(int i);      /* valid between begin and end */
void vr_end_frame(void);           /* hands both eyes back to the runtime */

const VrInput *vr_input(void);
/* The play space's own up: recentres the seated/standing origin under the
 * player's current position and forward direction. */
void vr_recentre(void);
#endif
