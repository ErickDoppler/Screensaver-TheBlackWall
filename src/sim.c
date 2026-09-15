#include "sim.h"
#include "models.h"
#include "platform.h"
#include <string.h>
#include <time.h>

#define ORIGIN_STEP 64.0      /* world units between origin snaps */
#define CPU_TAU     3.0f      /* how lazily the wall follows CPU load */
#define WAVE_TAU    2.5f
#define TRAFFIC_TAU 2.0f

/* Network traffic thresholds from the design: below ~300 kbps the wall shows
 * 20-30 soft bulges, above 10 Mbps long pointy jabs. Log-interpolated between. */
#define TRICKLE_BPS 3.0e5
#define HEAVY_BPS   1.0e7
#define IDLE_BPS    2.0e3     /* below this the network counts as silent */
#define SPIKE_ZONE_FULL  3.0f /* spikes at full size within this many view half-widths */
#define SPIKE_ZONE_OUTER 6.0f /* ... tapering to 20 % at this many */

#define WAVE_K0 0.028f   /* idle spatial frequency: a ~225-unit wavelength */

void sim_init(Sim *sim) {
    memset(sim, 0, sizeof *sim);
    /* a different order of figures and spikes on every run */
    sim->rng.s = 0xB1ACC0DEu ^ (unsigned)time(NULL);
    if (!sim->rng.s) sim->rng.s = 0xB1ACC0DEu;
    sim->wave_k = WAVE_K0;
    sim->wave_amp = 0.9f;
    sim->ghost.deck_pos = SIM_MAX_MODELS;   /* forces a shuffle on first use */
    sim->ghost.last_model = -1;
    sim->ghost.ember_t = -1.f;
}

static void spawn_spike(Sim *sim, double cam_x, float view_half_w, float view_h) {
    for (int i = 0; i < SIM_MAX_SPIKES; ++i) {
        Spike *s = &sim->spikes[i];
        if (s->active) continue;
        float t = clampf(sim->traffic_t + rng_range(&sim->rng, -0.15f, 0.15f), 0.f, 1.f);
        s->active = 1;
        /* Spikes spread over three times the visible width at full size, then
         * taper out to six times, so the activity is seen far along the wall. */
        float off = rng_range(&sim->rng, -SPIKE_ZONE_OUTER, SPIKE_ZONE_OUTER) * view_half_w;
        float taper = 1.f - smoothstepf(SPIKE_ZONE_FULL, SPIKE_ZONE_OUTER, fabsf(off) / view_half_w) * 0.8f;
        s->x = cam_x + off;
        s->y = rng_range(&sim->rng, 0.4f, view_h * 0.9f);
        s->pointy = t;
        s->amp = lerpf(0.35f, 4.5f, powf(t, 0.8f)) * rng_range(&sim->rng, 0.7f, 1.3f) * taper;
        s->width = lerpf(2.4f, 0.4f, t) * rng_range(&sim->rng, 0.8f, 1.25f);
        s->life = lerpf(4.0f, 1.4f, t) * rng_range(&sim->rng, 0.7f, 1.3f);
        s->age = 0.f;
        /* Heavy traffic mostly pushes toward the viewer (+z), like something
         * trying to break through; light traffic breathes both ways. */
        float toward = t > 0.5f ? 0.8f : 0.5f;
        if (rng_f(&sim->rng) > toward) s->amp = -s->amp;
        return;
    }
}

/* Ghost timeline. Distances in world units ("virtual metres"). */
#define GHOST_SPAWN_AHEAD 36.0f   /* figure appears this far ahead along the wall */
#define GHOST_TRIGGER_D   10.0f   /* camera closer than this starts the sequence */
#define GHOST_ABANDON_D   50.0f   /* ... and so does leaving the figure this far behind */
/* The figure resolves out of the dark at a fixed distance, not after a
 * fixed time: however fast we are travelling it appears at the same place
 * ahead rather than materialising on top of us. */
#define GHOST_FADE_FAR    45.0f   /* invisible beyond this */
#define GHOST_FADE_NEAR   30.0f   /* fully there by this */
#define GHOST_FADE_S      8.0f    /* how long the wall allows for it to arrive */
#define SCAR_HOLD_S       5.0f    /* the shadow stays whole and quiet this long after the wave */
#define SCAR_S            15.0f   /* then its points die one by one over this long */
#define BULGE_S           5.0f    /* the wall swells out to half the distance */
#define HOLD_S            2.0f    /* ... and holds there, breathing */
#define WAVE_S            3.5f    /* the giant wave rolls over the figure */
#define RETREAT_S         5.0f
#define SURGE_WIDTH       25.0f   /* half width of the swell: the whole view leans */
#define SCAR_FADE_TAU     4.0f

/* Next figure: dealt from a shuffled deck, so everyone appears once before
 * anyone repeats; a new deck never starts with the figure just shown. */
static int pick_model(Sim *sim) {
    Ghost *g = &sim->ghost;
    int n = bw_model_count > 0 ? bw_model_count : 1;
    if (n > SIM_MAX_MODELS) n = SIM_MAX_MODELS;
    if (n == 1) return 0;
    if (g->deck_pos >= n) {
        for (int i = 0; i < n; ++i) g->deck[i] = i;
        for (int i = n - 1; i > 0; --i) {              /* Fisher-Yates */
            int j = (int)(rng_u32(&sim->rng) % (unsigned)(i + 1));
            int t = g->deck[i]; g->deck[i] = g->deck[j]; g->deck[j] = t;
        }
        if (g->deck[0] == g->last_model) {             /* avoid a back-to-back repeat */
            int t = g->deck[0]; g->deck[0] = g->deck[n - 1]; g->deck[n - 1] = t;
        }
        g->deck_pos = 0;
    }
    return g->deck[g->deck_pos++];
}

static void update_ghost(Sim *sim, float dt, double cam_x, float cam_z, float moved_dx,
                         float figure_every, int at_home) {
    Ghost *g = &sim->ghost;
    if (fabsf(moved_dx) > 1e-6f) {
        g->travel += fabsf(moved_dx);
        g->dir = moved_dx > 0.f ? 1 : -1;
    }
    if (g->dir == 0) g->dir = 1;
    if (dt > 1e-4f) g->speed = approachf(g->speed, fabsf(moved_dx) / dt, 0.5f, dt);

    /* The wall needs WINDUP_S from the moment it notices the figure until the
     * wave crashes. Everything is measured in the ground the camera covers in
     * that time, so the figure is taken while it is still in front of us
     * rather than somewhere behind the viewport. */
    const float windup = BULGE_S + HOLD_S + WAVE_S;
    float trigger_d = fmaxf(GHOST_TRIGGER_D, g->speed * windup + 2.f);
    /* The figure has to be in sight well before the wall reacts to it, and
     * the wall reacts from further out the faster we travel. So the band it
     * fades in over is pushed out past the trigger, and it is placed beyond
     * that band again: at a run it appears as a distant speck and grows,
     * instead of being taken while still invisible. */
    float fade_far = fmaxf(GHOST_FADE_FAR, trigger_d + 35.f);
    float fade_near = fmaxf(GHOST_FADE_NEAR, trigger_d + 12.f);
    float spawn_ahead = fmaxf(GHOST_SPAWN_AHEAD,
                              fmaxf(g->speed * (GHOST_FADE_S + windup) + 15.f, fade_far + 12.f));
    switch (g->phase) {
    case GHOST_IDLE:
        /* figure_every < 0 summons the figure at once (testing hook). */
        if (figure_every != 0.f && (figure_every < 0.f || g->travel >= figure_every)) {
            g->phase = GHOST_STANDING;
            g->t = 0.f;
            g->model = pick_model(sim);
            g->last_model = g->model;
            g->spawn_d = spawn_ahead;
            g->fade_far = fade_far;
            g->fade_near = fade_near;
            g->x = cam_x + g->dir * spawn_ahead;
            /* near the wall the figure stands 10 units out; when the camera
             * has stepped far back, the figure appears at the camera's depth */
            g->z = cam_z > GHOST_FAR_BACK ? cam_z - 5.f : GHOST_STAND_Z;
            g->alpha = 0.f;
            g->reach = g->spike_reach = 0.f;
            /* a wave that must travel far is also broad */
            g->width = fmaxf(SURGE_WIDTH, 0.6f * g->z);
            plat_log("figure %d spawns %.0f m ahead, fades in %.0f-%.0f m, wall reacts at %.0f m (speed %.1f)",
                     g->model, spawn_ahead, fade_far, fade_near, trigger_d, g->speed);
        }
        break;
    case GHOST_STANDING: {
        g->t += dt;
        double dx = cam_x - g->x;
        float dz = cam_z - g->z;
        double dist = sqrt(dx * dx + dz * dz);
        g->alpha = 1.f - smoothstepf(g->fade_near, g->fade_far, (float)dist);
        /* approach starts the sequence; so does walking away and leaving the
         * figure behind, otherwise it would stand there forever (measured
         * from where it appeared, which is further off at speed) */
        float abandon_d = fmaxf(GHOST_ABANDON_D, g->spawn_d + 15.f);
        if (g->t > 1.f && (dist < trigger_d || dist > abandon_d)) {
            g->phase = GHOST_BULGE;
            g->t = 0.f;
        }
        break;
    }
    case GHOST_BULGE:
        g->t += dt;
        g->reach = smoothstepf(0.f, BULGE_S, g->t) * g->z * 0.5f;
        if (g->t >= BULGE_S) { g->phase = GHOST_HOLD; g->t = 0.f; }
        break;
    case GHOST_HOLD:
        g->t += dt;
        /* a slow breathing of the bulge while it holds */
        g->reach = g->z * 0.5f * (1.f + 0.06f * sinf(g->t * 4.f));
        if (g->t >= HOLD_S) {
            g->phase = GHOST_WAVE;
            g->t = 0.f;
            g->wave_t = 0.f;
            g->wave_start = g->z * 0.5f;
            g->wave_s = WAVE_S;
        }
        break;
    case GHOST_WAVE: {
        g->t += dt;
        g->wave_t += dt;
        float u = smoothstepf(0.f, WAVE_S, g->t);
        /* The crash carries the foot of the wave a little past the figure.
         * At the camera's default resting depth it is made to come far
         * enough to sweep over the viewer as well, compensating (up to 2x)
         * for the swell's falloff at their distance along the wall. Once the
         * viewer has stepped away from that depth the wave may pass them by. */
        float depth = g->z;
        if (at_home && cam_z > depth) {
            double dx = cam_x - g->x;
            float prof = expf(-(float)(dx * dx) / (g->width * g->width) * 0.7f);
            depth = fmaxf(depth, (cam_z + 2.f) / fmaxf(prof, 0.5f));
        }
        g->crash = depth + 2.5f;
        g->reach = lerpf(g->z * 0.5f, g->crash, u);
        /* Which points of the figure are gone and which points of its shadow
         * exist is decided per point in the shader, by testing the wall's
         * surface against where that piece of the body stands. This global
         * alpha only sweeps up the remainder once the front is well past. */
        /* the body itself is now dust, handled per point in the shader; the
         * figure keeps being drawn until the retreating wall has scooped it */
        g->alpha = 1.f;
        if (!g->scar_active) {
            g->scar_active = 1;
            g->scar_model = g->model;
            g->scar_x = g->x;
            g->scar_intensity = 1.f;
            g->scar_birth = 0.f;
            g->scar_age = 0.f;
        }
        if (g->t >= WAVE_S) {
            g->phase = GHOST_RETREAT;
            g->t = 0.f;
            g->scar_birth = 1.f;
            /* sparks are left behind, but they only start once the wall
             * has actually drawn back past the place where they stand */
            g->ember_armed = 1;
            g->ember_t = -1.f;
            g->ember_x = g->x;
            g->ember_z = g->z;
            g->ember_h = bw_model_height(g->model);
            g->ember_model = g->model;
        }
        break;
    }
    case GHOST_RETREAT: {
        g->t += dt;
        g->wave_t += dt;
        float u = smoothstepf(0.f, RETREAT_S, g->t);
        g->reach = g->crash * (1.f - u);
        if (g->ember_armed && g->reach <= g->ember_z) {
            g->ember_armed = 0;
            g->ember_t = 0.f;
        }
        /* the dust hangs until the wall, drawing back in, scoops it up */
        g->alpha = 1.f - smoothstepf(0.85f, 1.f, u);
        if (g->t >= RETREAT_S) {
            g->phase = GHOST_IDLE;
            g->reach = g->spike_reach = 0.f;
            g->travel = 0.f;
        }
        break;
    }
    }
    if (g->ember_t >= 0.f) {
        g->ember_t += dt;
        if (g->ember_t > EMBER_LIFE) g->ember_t = -1.f;
    }
    if (g->scar_active && g->phase != GHOST_WAVE) {
        /* whole for SCAR_HOLD_S, then intensity runs 1 -> 0 over SCAR_S; the
         * shader gives every point its own death moment on that scale */
        g->scar_age += dt;
        float t = (g->scar_age - SCAR_HOLD_S) / SCAR_S;
        g->scar_intensity = 1.f - clampf(t, 0.f, 1.f);
        if (t >= 1.f) { g->scar_active = 0; g->scar_intensity = 0.f; }
    }
}

float sim_wall_z(const Sim *sim, double x_world, float y) {
    float x = (float)(x_world - sim->origin_x);
    float d = sim->wave_amp * sinf(sim->wave_k * x + sim->wave_phase + 0.25f * y)
            + 0.3f * sim->wave_amp * sinf(2.3f * sim->wave_k * x + sim->wave_phase2 + 1.3f);
    const Ghost *g = &sim->ghost;
    if (g->reach > 0.001f) {
        float sx = (float)(g->x - sim->origin_x);
        float px = (x - sx) / g->width;
        float profile = expf(-px * px * 0.7f);
        float lean = fminf(0.35f * g->reach, 8.f) * powf(clampf(y / 9.f, 0.f, 1.f), 1.2f);
        float roll_amp = 0.10f * fminf(1.f, 25.f / g->width);
        float roll = 1.f + roll_amp * sinf(0.35f * (x - sx) - sim->time * 1.2f) * clampf(y / 3.f, 0.f, 1.f);
        d += (g->reach + lean) * profile * roll;
    }
    return d;
}

/* The wall's reaction to the figure (ghost.reach / ghost.width) is sent to the
 * wall shader as the uSurge uniform and shaped there; see wall.vert. */

void sim_update(Sim *sim, float dt, float cpu, double bps,
                double cam_x, float cam_z, float view_half_w, float view_h,
                float moved_dx, float figure_every, int at_home) {
    sim->time += dt;
    update_ghost(sim, dt, cam_x, cam_z, moved_dx, figure_every, at_home);

    /* --- origin snapping keeps GPU-side coordinates small ------------- */
    double new_origin = floor(cam_x / ORIGIN_STEP) * ORIGIN_STEP;
    if (new_origin != sim->origin_x) {
        /* Every term sin(a*k*x + phi) needs phi += a*k*delta to stay
         * continuous when x is re-based. Primary: a = 1, harmonic: a = 2.3. */
        float delta = (float)(new_origin - sim->origin_x);
        sim->wave_phase  += sim->wave_k * delta;
        sim->wave_phase2 += 2.3f * sim->wave_k * delta;
        sim->origin_x = new_origin;
    }

    /* --- CPU load -> wave character ------------------------------------ */
    sim->cpu = approachf(sim->cpu, clampf(cpu, 0.f, 1.f), CPU_TAU, dt);
    float k_target   = WAVE_K0 * (1.f + 7.f * sim->cpu);
    float amp_target = 0.9f * powf(1.f - sim->cpu, 1.6f);   /* 100 % -> flat line */
    float speed      = 0.7f + 1.8f * sim->cpu;
    /* Changing k while keeping the phase continuous at the origin. */
    sim->wave_k   = approachf(sim->wave_k, k_target, WAVE_TAU, dt);
    sim->wave_amp = approachf(sim->wave_amp, amp_target, WAVE_TAU, dt);
    sim->wave_phase = fmodf(sim->wave_phase + speed * dt, 2.f * BW_PI);
    if (sim->wave_phase < 0.f) sim->wave_phase += 2.f * BW_PI;
    sim->wave_phase2 = fmodf(sim->wave_phase2 - 0.7f * speed * dt, 2.f * BW_PI);
    if (sim->wave_phase2 < 0.f) sim->wave_phase2 += 2.f * BW_PI;

    /* --- network -> spikes ---------------------------------------------- */
    float t_target = 0.f;
    if (bps > TRICKLE_BPS) {
        double lt = (log10(bps) - log10(TRICKLE_BPS)) / (log10(HEAVY_BPS) - log10(TRICKLE_BPS));
        t_target = clampf((float)lt, 0.f, 1.f);
    }
    sim->traffic_t = approachf(sim->traffic_t, t_target, TRAFFIC_TAU, dt);

    /* counts are for the whole (six-times-wider) spawn zone, so that the
     * visible third still holds the 20-30 bulges of the design */
    if (bps < IDLE_BPS) {
        sim->target_spikes = 0;
    } else if (bps <= TRICKLE_BPS) {
        sim->target_spikes = (int)(lerpf(40.f, 60.f, (float)(bps / TRICKLE_BPS)) + 0.5f);
    } else {
        sim->target_spikes = (int)(lerpf(60.f, 28.f, sim->traffic_t) + 0.5f);
    }
    if (sim->target_spikes > SIM_MAX_SPIKES) sim->target_spikes = SIM_MAX_SPIKES;

    int active = 0;
    for (int i = 0; i < SIM_MAX_SPIKES; ++i) {
        Spike *s = &sim->spikes[i];
        if (!s->active) continue;
        s->age += dt;
        if (s->age >= s->life) { s->active = 0; continue; }
        active++;
    }
    if (active < sim->target_spikes) {
        sim->spawn_timer -= dt;
        if (sim->spawn_timer <= 0.f) {
            spawn_spike(sim, cam_x, view_half_w, view_h);
            float mean_life = lerpf(4.0f, 1.4f, sim->traffic_t);
            /* Spread spawns so the population stays near the target. */
            sim->spawn_timer = mean_life / (float)(sim->target_spikes > 0 ? sim->target_spikes : 1)
                             * rng_range(&sim->rng, 0.4f, 1.4f);
            /* First spikes after silence should appear promptly. */
            if (active == 0) sim->spawn_timer *= 0.25f;
        }
    } else {
        sim->spawn_timer = 0.f;
    }

    /* --- pack for the GPU ------------------------------------------------ */
    int n = 0;
    for (int i = 0; i < SIM_MAX_SPIKES; ++i) {
        Spike *s = &sim->spikes[i];
        if (!s->active) continue;
        float u = s->age / s->life;
        float env = sinf(BW_PI * u);
        env = powf(env, lerpf(1.5f, 0.6f, s->pointy));  /* soft breath vs quick jab */
        float *d = &sim->gpu_spikes[n * 4];
        d[0] = (float)(s->x - sim->origin_x);
        d[1] = s->y;
        d[2] = s->amp * env;
        /* Pack width and pointiness into one float: width is always < 100,
         * so w = 100 * round(pointy * 99) + width. The shader unpacks it. */
        d[3] = floorf(s->pointy * 99.f + 0.5f) * 100.f + s->width;
        n++;
    }
    sim->gpu_spike_count = n;
}
