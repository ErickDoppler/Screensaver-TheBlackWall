/* The wall simulation: a calm horizontal wave whose period shrinks with CPU
 * load, plus spikes driven by network traffic. Geometry itself is generated
 * on the GPU; this module only produces the small parameter set per frame.
 *
 * Coordinates: the wall lies in the x/y plane at z = 0 and is infinite along
 * x. To keep float precision after hours of side movement, everything sent to
 * the GPU is expressed relative to `origin_x`, a double snapped near the
 * camera (see sim_update). */
#ifndef BW_SIM_H
#define BW_SIM_H
#include "mathx.h"

#define SIM_MAX_SPIKES 64
#define SIM_MAX_MODELS 8

typedef struct Spike {
    double x;          /* world x of the spike center */
    float  y;          /* height on the wall */
    float  amp;        /* peak displacement (world units), sign = direction */
    float  width;      /* gaussian radius */
    float  age, life;  /* seconds */
    float  pointy;     /* 0 = soft bulge, 1 = sharp jab */
    int    active;
} Spike;

/* Easter egg. After enough keyboard walking along the wall a ghost figure
 * fades in a few units from the wall, standing still. When the camera comes
 * close the wall bulges out halfway toward it, big and tall, holds for two
 * seconds, then a wave swallows the figure and leaves a fading scar. */
typedef enum GhostPhase {
    GHOST_IDLE = 0,
    GHOST_STANDING,   /* fading in / waiting for the camera */
    GHOST_BULGE,      /* wall bulges out to half the distance */
    GHOST_HOLD,       /* bulge holds, tension */
    GHOST_WAVE,       /* the big wave engulfs the figure */
    GHOST_RETREAT     /* wall settles back, scar remains */
} GhostPhase;

#define GHOST_STAND_Z 10.0f   /* figure's distance from the wall (camera near the wall) */
#define GHOST_FAR_BACK 50.0f  /* camera further back than this: figures appear at its depth */

typedef struct Ghost {
    float      travel;        /* distance walked along the wall since the last figure */
    int        dir;           /* last direction of travel along x (+1/-1) */
    float      speed;         /* smoothed speed along the wall, units/s */
    float      spawn_d;       /* how far ahead this figure appeared */
    float      fade_far;      /* invisible beyond this, fixed when it appeared */
    float      fade_near;     /* fully there by this */
    int        model;         /* index into bw_models */
    int        last_model;
    int        deck[SIM_MAX_MODELS];   /* shuffled order; everyone appears once per round */
    int        deck_pos;      /* next card; == deck size means reshuffle */
    GhostPhase phase;
    float      t;             /* seconds in the current phase */
    double     x;             /* world x of the figure */
    float      z;             /* distance from the wall */
    float      alpha;
    float      reach;         /* current bulge amplitude toward the figure */
    float      crash;         /* reach at the end of the wave */
    float      wave_t;        /* seconds since the wave began, through the retreat */
    float      wave_start;    /* reach when the wave began */
    float      wave_s;        /* how long the wave itself lasts */
    float      spike_reach;   /* current amplitude of the pointy spikes */
    float      width;         /* bulge width */
    /* the last sparks where a figure stood, left once the wall draws back */
    float      ember_t;       /* < 0 = not started */
    int        ember_armed;   /* waiting for the wall to draw back past them */
    double     ember_x;
    float      ember_z, ember_h;
    int        ember_model;
    int        scar_active;
    int        scar_model;    /* whose shadow is on the wall */
    float      scar_birth;    /* fraction of the shadow formed, 0..1 */
    double     scar_x;        /* world x where the wall consumed the figure */
    float      scar_intensity;
    float      scar_age;
} Ghost;

typedef struct Sim {
    float  time;
    float  cpu;                 /* smoothed 0..1 as seen by the wall */
    float  wave_k;              /* spatial frequency (radians / unit) */
    float  wave_amp;            /* z amplitude */
    float  wave_phase;          /* accumulated temporal phase */
    float  wave_phase2;         /* phase of the 2.3 k harmonic (own accumulator) */
    double origin_x;            /* current snapping origin */
    Spike  spikes[SIM_MAX_SPIKES];
    int    target_spikes;
    float  spawn_timer;
    float  traffic_t;           /* 0 = trickle, 1 = heavy (log scale) */
    rng_t  rng;
    Ghost  ghost;

    /* Packed per-frame GPU data (x_rel, y, amp*envelope, width/pointy). */
    float  gpu_spikes[SIM_MAX_SPIKES * 4];
    int    gpu_spike_count;
} Sim;

void sim_init(Sim *sim);
/* cam_x: camera world x; view_half_w: half of the wall width the camera sees;
 * view_h: height of wall in view. bps: smoothed network bits/second.
 * moved_dx: how far the camera moved along the wall this frame (signed, any
 * source: keys or automatic side movement); figure_every: distance of travel
 * that summons the next figure (0 disables, < 0 summons at once); cam_z:
 * camera distance from the wall, used to detect the approach; at_home: the
 * camera is at its default resting depth, where the wave is made to reach
 * far enough to take the viewer along with the figure. */
void sim_update(Sim *sim, float dt, float cpu, double bps,
                double cam_x, float cam_z, float view_half_w, float view_h,
                float moved_dx, float figure_every, int at_home);
/* The wall's surface z at a world point, wave and surge included (the same
 * formula the wall shader uses). Used to tell when the wall has swept over
 * the camera. */
float sim_wall_z(const Sim *sim, double x_world, float y);
#define EMBER_COUNT 16
#define EMBER_LIFE  9.5f      /* the slow fall, then dying on the floor */
#endif
