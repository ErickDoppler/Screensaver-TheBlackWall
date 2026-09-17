#include "app.h"
#include "platform.h"
#include "gl_loader.h"
#include "render.h"
#include "sim.h"
#include "stats.h"
#include "models.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

typedef struct App {
    const AppConfig *cfg;
    Settings    s;
    SDL_Window *win;
    SDL_GLContext gl;
    Renderer    r;
    Sim         sim;
    Stats       stats;
    Camera      cam;
    int         width, height;
    float       refresh_hz;
    int         use_vsync;
    int         swap_n;             /* present every n-th vblank when vsynced */
    int         running;
    int         side_movement;      /* runtime toggle, starts from settings */
    double      prev_x;             /* camera x last frame (travel measurement) */
    int         pose_model;         /* pose tool: which figure is shown */
    float       auto_turn_at;       /* elapsed time when the auto-turn may start */
    int         took_control;       /* the user has touched the controls at all */
    float       last_input;         /* when they last did */
    int         was_manual;         /* to notice the moment the screensaver takes over again */
    int         engulfed;           /* the wall has swept over the camera */
    /* On foot: a standing eye height that Q/E adjust, a crouch that eases in
     * and out under it, and a jump that rides on top. Chaining jumps while
     * moving builds speed and air time, the way bunny-hopping does. */
    float       base_y;             /* standing eye height */
    float       crouch;             /* <= 0, eased offset while Ctrl is held */
    float       jump_y, jump_v, jump_g;
    int         airborne;
    int         jump_chain;         /* consecutive jumps taken while moving */
    float       land_t;             /* when the last one came down */
    float       elapsed;            /* seconds since start */
    float       mouse_travel;       /* accumulated motion for exit-on-move */
    int         frames;
} App;

/* ------------------------------------------------------------------------ */
static SDL_Window *create_window(App *a) {
    const AppConfig *cfg = a->cfg;
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
    SDL_SetStringProperty(p, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "The Black Wall");

    if (cfg->mode == MODE_PREVIEW) {
#ifdef _WIN32
        int w = 0, h = 0;
        void *child = plat_win32_create_preview_child(cfg->parent_hwnd, &w, &h);
        if (!child) { SDL_DestroyProperties(p); return NULL; }
        SDL_SetPointerProperty(p, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, child);
#else
        SDL_DestroyProperties(p);
        return NULL;
#endif
#ifndef _WIN32
    } else if (cfg->mode == MODE_EMBEDDED) {
        /* SDL draws on a GL context whose config it picks itself, and that
         * need not match the visual of the window XScreenSaver made. So SDL
         * gets a window of its own, created hidden, which is then moved
         * inside XScreenSaver's window and mapped there. */
        unsigned long parent = (unsigned long)(uintptr_t)cfg->parent_hwnd;
        int w = 0, h = 0;
        if (!plat_x11_window_size(parent, &w, &h)) {
            SDL_SetError("window 0x%lx does not exist", parent);
            SDL_DestroyProperties(p);
            return NULL;
        }
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h);
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
        SDL_Window *win = SDL_CreateWindowWithProperties(p);
        SDL_DestroyProperties(p);
        if (!win) return NULL;
        unsigned long child = (unsigned long)SDL_GetNumberProperty(
            SDL_GetWindowProperties(win), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        if (!child || !plat_x11_embed(child, parent)) {
            SDL_SetError("could not embed into window 0x%lx", parent);
            SDL_DestroyWindow(win);
            return NULL;
        }
        return win;
#endif
    } else if (cfg->mode == MODE_FULLSCREEN) {
        int x = 0, y = 0, w = 1280, h = 720;
#ifdef _WIN32
        plat_win32_virtual_screen(&x, &y, &w, &h);
#else
        /* Let the window manager or compositor make it fullscreen: on Wayland
         * a client cannot position itself, and X11 window managers keep
         * panels above a plain borderless window. */
        SDL_Rect b;
        if (SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &b)) { x = b.x; y = b.y; w = b.w; h = b.h; }
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
#endif
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h);
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN, true);
    } else {
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, cfg->win_w);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, cfg->win_h);
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    }
    SDL_Window *w = SDL_CreateWindowWithProperties(p);
    SDL_DestroyProperties(p);
    return w;
}

static int init_gl(App *a) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    a->win = create_window(a);
    if (!a->win) { plat_log("window: %s", SDL_GetError()); return 0; }
    a->gl = SDL_GL_CreateContext(a->win);
    if (!a->gl) { plat_log("GL context: %s", SDL_GetError()); return 0; }
    SDL_GL_MakeCurrent(a->win, a->gl);
    /* When the frame cap is at or above the display refresh rate, vsync gives
     * the smoothest motion (every frame lands on a vblank). Below that we
     * disable vsync and pace frames ourselves with precise sleeps, which is
     * cheaper on CPU than a driver spinning on the vblank. */
    const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(a->win));
    a->refresh_hz = (dm && dm->refresh_rate > 0.f) ? dm->refresh_rate : 60.f;
    /* If the cap divides the refresh rate (60 -> 1, 30 -> 2, 20 -> 3 on a
     * 60 Hz panel) let the driver present every n-th vblank: perfectly even
     * frame times. Otherwise fall back to our own pacing. */
    float ratio = a->refresh_hz / (float)a->s.target_fps;
    int n = (int)floorf(ratio + 0.5f);
    a->use_vsync = n >= 1 && n <= 4 && fabsf(ratio - (float)n) < 0.05f;
    /* Drivers often ignore swap intervals above 1, so for n > 1 we vsync at 1
     * and hold the frame ourselves until just before the n-th vblank. */
    a->swap_n = n;
    if (a->use_vsync && !SDL_GL_SetSwapInterval(1)) a->use_vsync = 0;
    if (!a->use_vsync) SDL_GL_SetSwapInterval(0);
    const char *missing = gl_load_functions();
    if (missing) { plat_log("OpenGL 3.3 function missing: %s", missing); return 0; }
    SDL_GetWindowSizeInPixels(a->win, &a->width, &a->height);
    return 1;
}

/* ------------------------------------------------------------------------ */
/* How far from the wall the camera stands by default. With side movement the
 * camera hangs back so figures ahead are seen rather than slid past; without
 * it, further still, so the wave and the traffic spikes read across the wall. */
#define DEPTH_SIDE_MOVING (GHOST_STAND_Z + 1.f)   /* the drift passes 1 m from the figures */
#define DEPTH_STANDING    29.f
/* Seconds before the view turns to face the way it is drifting. */
#define AUTO_TURN_DELAY   5.f
/* Fraction of the screen width the wall should fill after that turn. */
#define AUTO_TURN_COVER   0.70f
/* Seconds of no input before the screensaver takes the controls back. */
#define IDLE_RESUME       20.f

/* While the user is driving, the screensaver keeps its hands off: no drift
 * along the wall and no turning the view. Both come back once they stop. */
static void mark_input(App *a) { a->took_control = 1; a->last_input = a->elapsed; }

static int manual_control(const App *a) {
    return a->took_control && a->elapsed - a->last_input < IDLE_RESUME;
}

static void reset_view(App *a) {
    double x = a->cam.x;
    camera_reset(&a->cam);
    a->cam.x = x;
    a->cam.z = a->side_movement ? DEPTH_SIDE_MOVING : DEPTH_STANDING;
    a->base_y = a->cam.y;
    a->crouch = a->jump_y = a->jump_v = 0.f;
    a->airborne = a->jump_chain = 0;
    a->auto_turn_at = a->elapsed + AUTO_TURN_DELAY;
}

static void request_exit(App *a) { a->running = 0; }

/* Keyboard and mouse belong to us only in our own windows. In the Windows
 * preview and inside XScreenSaver the owner of the window decides what input
 * means (XScreenSaver unlocks or kills us on its own). */
static int takes_input(const App *a) {
    return a->cfg->mode == MODE_FULLSCREEN || a->cfg->mode == MODE_WINDOW;
}

/* Full 360-degree yaw: the user may turn around to face the city horizon. */
static float wrap_angle(float a) {
    while (a > BW_PI) a -= 2.f * BW_PI;
    while (a < -BW_PI) a += 2.f * BW_PI;
    return a;
}

/* Pose tool camera: in front of the figure, far enough to see all of it,
 * with the wall behind. */
static void pose_camera(App *a) {
    float h = bw_model_height(a->pose_model);
    a->cam.z = GHOST_STAND_Z + 2.6f * h;
    a->cam.y = 0.5f * h + 0.3f;
    a->cam.pitch = 0.0f;
    a->cam.yaw = 0.f;
}

/* --- on foot ------------------------------------------------------------ */
#define SPRINT_MULT   3.0f    /* Shift */
#define CROUCH_MULT   0.5f    /* Ctrl, which overrides Shift */
#define CROUCH_DROP   0.7f    /* how far the eye sinks */
#define HOP_FROM      3       /* chained jump that starts paying off */
#define HOP_MULT      2.0f    /* ... and by how much, so Shift + hop is six */
#define HOP_WINDOW    0.35f   /* jump again within this of landing to chain */
#define HOP_DROP      0.45f   /* stop jumping this long and the chain is gone */
#define JUMP_APEX_LOW 0.5f
#define JUMP_APEX_TOP 1.0f
#define JUMP_G_LOW    9.8f    /* a short hop */
#define JUMP_G_TOP    6.0f    /* chained: floatier, so it also flies further */

static int moving_on_foot(void) {
    const bool *k = SDL_GetKeyboardState(NULL);
    return k[SDL_SCANCODE_W] || k[SDL_SCANCODE_S] || k[SDL_SCANCODE_A] || k[SDL_SCANCODE_D] ||
           k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_RIGHT];
}

static void jump(App *a, int moving) {
    if (a->airborne || a->cfg->pose) return;
    /* Jumping again the moment we land, while moving, builds the chain. */
    if (moving && a->elapsed - a->land_t < HOP_WINDOW) a->jump_chain++;
    else a->jump_chain = 1;
    float t = clampf((float)(a->jump_chain - (HOP_FROM - 1)) / 3.f, 0.f, 1.f);
    a->jump_g = lerpf(JUMP_G_LOW, JUMP_G_TOP, t);
    float apex = lerpf(JUMP_APEX_LOW, JUMP_APEX_TOP, t);
    a->jump_v = sqrtf(2.f * a->jump_g * apex);
    a->airborne = 1;
    if (a->cfg->hop_test)
        plat_log("jump %d: apex %.2f m, air %.2f s, horizontal x%.1f",
                 a->jump_chain, apex, 2.f * a->jump_v / a->jump_g,
                 a->jump_chain >= HOP_FROM ? HOP_MULT : 1.f);
}

/* How much faster we are travelling on foot right now. */
static float foot_speed_mult(const App *a) {
    const bool *k = SDL_GetKeyboardState(NULL);
    float m = 1.f;
    if (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL]) m = CROUCH_MULT;   /* beats Shift */
    else if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) m = SPRINT_MULT;
    if (a->jump_chain >= HOP_FROM) m *= HOP_MULT;
    return m;
}

static void pose_title(App *a) {
    char title[200];
    snprintf(title, sizeof title,
             "The Black Wall - position the ghosts: %s %d deg   [Up/Down = figure, Left/Right = rotate 45, Enter = OK, Esc = cancel]",
             bw_models[a->pose_model].name, settings_figure_yaw(&a->s, a->pose_model) * 45);
    SDL_SetWindowTitle(a->win, title);
}

static void handle_key(App *a, const SDL_KeyboardEvent *k) {
    if (k->repeat) return;
    if (k->key == SDLK_ESCAPE) { request_exit(a); return; }
    if (a->cfg->pose) {
        /* Positioning tool: Up/Down pick the figure, Left/Right rotate it in
         * 45-degree steps, Enter saves every figure's facing. */
        int yaw = settings_figure_yaw(&a->s, a->pose_model);
        if (k->key == SDLK_LEFT)  { settings_set_figure_yaw(&a->s, a->pose_model, (yaw + 7) & 7); pose_title(a); }
        if (k->key == SDLK_RIGHT) { settings_set_figure_yaw(&a->s, a->pose_model, (yaw + 1) & 7); pose_title(a); }
        if (k->key == SDLK_UP)    { a->pose_model = (a->pose_model + 1) % bw_model_count; pose_camera(a); pose_title(a); }
        if (k->key == SDLK_DOWN)  { a->pose_model = (a->pose_model + bw_model_count - 1) % bw_model_count; pose_camera(a); pose_title(a); }
        if (k->key == SDLK_RETURN || k->key == SDLK_KP_ENTER) {
            plat_store_write_int("ghost-yaws", a->s.ghost_yaws);
            plat_log("figure facings saved (packed): %d", a->s.ghost_yaws);
            request_exit(a);
        }
        return;
    }
    if (a->cfg->mode == MODE_FULLSCREEN && a->s.exit_on_any_key) { request_exit(a); return; }
    switch (k->key) {
    case SDLK_HOME: case SDLK_R: reset_view(a); break;
    case SDLK_SPACE: jump(a, moving_on_foot()); break;
    case SDLK_T:
        a->side_movement = !a->side_movement;
        a->auto_turn_at = a->elapsed + AUTO_TURN_DELAY;
        break;
    default: break;
    }
}

static void handle_mouse_motion(App *a, const SDL_MouseMotionEvent *m) {
    if (a->s.mouse_rotation) {
        a->cam.yaw   = wrap_angle(a->cam.yaw + m->xrel * 0.0022f);
        a->cam.pitch = clampf(a->cam.pitch - m->yrel * 0.0022f, -0.35f, 1.10f);
        if (fabsf(m->xrel) + fabsf(m->yrel) > 2.f) { a->took_control = 1; a->last_input = a->elapsed; }
        return;
    }
    if (a->cfg->mode != MODE_FULLSCREEN || !a->s.exit_on_mouse_move) return;
    if (a->elapsed < 0.5f) return;   /* ignore the cursor jump at start-up */
    a->mouse_travel += fabsf(m->xrel) + fabsf(m->yrel);
    float threshold = lerpf(200.f, 4.f, a->s.mouse_sensitivity / 100.f);
    if (a->mouse_travel > threshold) request_exit(a);
}

static void handle_mouse_button(App *a) {
    if (a->cfg->mode != MODE_FULLSCREEN) { if (a->s.click_resets_view) reset_view(a); return; }
    if (a->s.mouse_rotation) { request_exit(a); return; }   /* no other way out with the mouse */
    if (a->s.click_resets_view) reset_view(a);
    else if (a->s.exit_on_mouse_move) request_exit(a);
}

static void poll_events(App *a) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT: request_exit(a); break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: request_exit(a); break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            SDL_GetWindowSizeInPixels(a->win, &a->width, &a->height);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (takes_input(a)) {
                if (!e.key.repeat && e.key.key != SDLK_ESCAPE) mark_input(a);
                handle_key(a, &e.key);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (takes_input(a)) handle_mouse_motion(a, &e.motion);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (takes_input(a)) handle_mouse_button(a);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (takes_input(a) && !a->s.exit_on_any_key) {
                /* Wheel moves along the view direction, like the W/S keys. */
                float step = e.wheel.y * 0.6f;
                a->cam.x += (double)(sinf(a->cam.yaw) * step);
                a->cam.z = clampf(a->cam.z - cosf(a->cam.yaw) * step, 2.5f, CORRIDOR_DEPTH);
            }
            break;
        default: break;
        }
    }
}

/* Continuous camera control on held keys (when keys are not an exit trigger). */
static void update_camera(App *a, float dt) {
    if (a->cfg->pose) return;            /* the pose tool holds the camera still */
    /* Hands off while the user is driving; the drift resumes once they have
     * been idle for IDLE_RESUME, and the view is then eased back round too. */
    int manual = manual_control(a);
    if (a->was_manual && !manual) a->auto_turn_at = a->elapsed + AUTO_TURN_DELAY;
    a->was_manual = manual;
    if (a->side_movement && !manual) {
        float speed = a->cfg->drift_mps > 0.f ? a->cfg->drift_mps
                                              : lerpf(0.15f, 3.0f, a->s.movement_speed / 100.f);
        a->cam.x += speed * dt;
    }
    if (!takes_input(a)) return;
    if (a->cfg->mode == MODE_FULLSCREEN && a->s.exit_on_any_key) return;
    const bool *k = SDL_GetKeyboardState(NULL);
    float mv = 4.f * foot_speed_mult(a) * dt, rot = 1.2f * dt;
    /* Movement is relative to the view: forward is where the camera looks
     * (projected onto the floor), right is 90 degrees clockwise from it. */
    float fx = sinf(a->cam.yaw), fz = -cosf(a->cam.yaw);
    float rx = -fz, rz = fx;
    float dx = 0.f, dz = 0.f;
    if (k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W]) { dx += fx; dz += fz; }
    if (k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S]) { dx -= fx; dz -= fz; }
    if (k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A]) { dx -= rx; dz -= rz; }
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) { dx += rx; dz += rz; }
    if (dx != 0.f || dz != 0.f) {
        mark_input(a);
        float len = sqrtf(dx * dx + dz * dz);
        a->cam.x += (double)(dx / len * mv);
        a->cam.z = clampf(a->cam.z + dz / len * mv, 2.5f, CORRIDOR_DEPTH);
    }
    if (k[SDL_SCANCODE_PAGEUP]   || k[SDL_SCANCODE_Q]) { a->base_y = clampf(a->base_y + mv, 0.4f, 12.f); mark_input(a); }
    if (k[SDL_SCANCODE_PAGEDOWN] || k[SDL_SCANCODE_E]) { a->base_y = clampf(a->base_y - mv, 0.4f, 12.f); mark_input(a); }

    /* Crouch eases in and out under the standing height; the jump rides on
     * top of whatever that comes to. */
    int ctrl = k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL];
    if (ctrl || k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT] || a->airborne) mark_input(a);
    a->crouch = approachf(a->crouch, ctrl ? -CROUCH_DROP : 0.f, 0.09f, dt);
    if (a->airborne) {
        a->jump_v -= a->jump_g * dt;
        a->jump_y += a->jump_v * dt;
        if (a->jump_y <= 0.f) {
            a->jump_y = 0.f;
            a->jump_v = 0.f;
            a->airborne = 0;
            a->land_t = a->elapsed;
        }
    } else if (a->jump_chain > 0 && a->elapsed - a->land_t > HOP_DROP) {
        a->jump_chain = 0;                      /* stopped hopping: no more bonus */
    }
    a->cam.y = fmaxf(0.15f, a->base_y + a->crouch) + a->jump_y;
    if (k[SDL_SCANCODE_J]) { a->cam.yaw   = wrap_angle(a->cam.yaw - rot); mark_input(a); }
    if (k[SDL_SCANCODE_L]) { a->cam.yaw   = wrap_angle(a->cam.yaw + rot); mark_input(a); }
    if (k[SDL_SCANCODE_I]) { a->cam.pitch = clampf(a->cam.pitch + rot, -0.35f, 1.10f); mark_input(a); }
    if (k[SDL_SCANCODE_K]) { a->cam.pitch = clampf(a->cam.pitch - rot, -0.35f, 1.10f); mark_input(a); }
}

/* After a while adrift, turn to look the way we are going, far enough that
 * the wall fills AUTO_TURN_COVER of the screen and the rest shows the space
 * ahead, where the next figure will fade in.
 *
 * Yaw y puts the vanishing point of the wall's +x direction at NDC
 * cot(y) / tan(fovx/2); the wall covers the screen from the left edge to
 * there, so coverage c means cot(y) = (2c - 1) * tan(fovx/2). */
static void auto_turn(App *a, float dt, float aspect) {
    if (a->cfg->pose || a->cfg->mode == MODE_PREVIEW) return;
    if (!a->side_movement || manual_control(a)) return;
    if (a->elapsed < a->auto_turn_at) return;
    float tan_half_x = tanf(a->cam.fov_y * 0.5f) * aspect;
    float target = atan2f(1.f, (2.f * AUTO_TURN_COVER - 1.f) * tan_half_x);
    a->cam.yaw = approachf(a->cam.yaw, target, 2.5f, dt);
}

/* ------------------------------------------------------------------------ */
int app_run(const AppConfig *cfg) {
    App a;
    memset(&a, 0, sizeof a);
    a.cfg = cfg;
    a.s = cfg->settings;
    settings_clamp(&a.s);
    if (cfg->mode == MODE_PREVIEW) {
        /* The preview is a few hundred pixels wide: keep it light. */
        if (a.s.density > 50) a.s.density = 50;
        if (a.s.target_fps > 20) a.s.target_fps = 20;
        a.s.ghost_tail = 0;
    }

    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    /* XScreenSaver hands us an X11 window, even under XWayland. */
    if (cfg->mode == MODE_EMBEDDED) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        plat_log("SDL_Init: %s", SDL_GetError());
        return 2;
    }
    int code = 0;
    if (!init_gl(&a)) { code = 3; goto done; }
    if (!render_init(&a.r)) { code = 4; goto done; }
    render_resize(&a.r, a.width, a.height);
    sim_init(&a.sim);
    stats_init(&a.stats, cfg->cpu_override, cfg->net_override);
    camera_reset(&a.cam);
    a.cam.yaw = cfg->start_yaw;
    a.base_y = a.cam.y;
    a.side_movement = a.s.side_movement;
    a.cam.z = a.side_movement ? DEPTH_SIDE_MOVING : DEPTH_STANDING;
    a.auto_turn_at = AUTO_TURN_DELAY;
    if (cfg->start_z > 0.f) a.cam.z = clampf(cfg->start_z, 2.5f, CORRIDOR_DEPTH);
    if (cfg->start_yaw != 0.f) a.auto_turn_at = 1e9f;   /* an aim given on the command line is not "the user driving" */
    a.prev_x = a.cam.x;
    a.pose_model = cfg->pose_model >= 0 && cfg->pose_model < bw_model_count ? cfg->pose_model : 0;
    if (cfg->scar_test) {
        a.sim.ghost.scar_active = 1;
        a.sim.ghost.scar_model = a.pose_model;
        a.sim.ghost.scar_x = a.cam.x + 1.5;
        a.sim.ghost.scar_intensity = 1.f;
        a.sim.ghost.scar_birth = 1.f;
        a.sim.ghost.scar_age = 0.f;
    }
    if (cfg->pose) {
        /* Stand in front of the figure, looking at it and the wall behind
         * it; no drift, no exit rules. */
        pose_camera(&a);
        a.side_movement = 0;
        pose_title(&a);
    }

    if (cfg->mode == MODE_FULLSCREEN) {
        SDL_HideCursor();
        if (a.s.mouse_rotation) SDL_SetWindowRelativeMouseMode(a.win, true);
        SDL_RaiseWindow(a.win);
    }

    plat_log("start: mode=%d %dx%d fps=%d (display %.0f Hz, %s) density=%d pixel=%s",
             cfg->mode, a.width, a.height, a.s.target_fps, a.refresh_hz,
             a.use_vsync ? "vsync" : "paced", a.s.density,
             settings_pixel_type_name(a.s.pixel_type));

    const Uint64 frame_ns = 1000000000ull / (Uint64)a.s.target_fps;
    Uint64 last = SDL_GetTicksNS();
    Uint64 next_deadline = last + frame_ns;
    a.running = 1;
    while (a.running) {
        Uint64 now = SDL_GetTicksNS();
        float dt = (float)(now - last) / 1e9f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        a.elapsed += dt;

        poll_events(&a);
#ifdef _WIN32
        if (cfg->mode == MODE_PREVIEW && !plat_win32_window_alive(cfg->parent_hwnd)) break;
#endif
        if (!a.running) break;

        stats_update(&a.stats, dt);
        update_camera(&a, dt);
        float aspect = (float)a.width / (float)(a.height > 0 ? a.height : 1);
        auto_turn(&a, dt, aspect);
        if (cfg->hop_test && !a.airborne) jump(&a, 1);   /* hop as soon as we land */
        float moved_dx = (float)(a.cam.x - a.prev_x);
        a.prev_x = a.cam.x;
        float figure_every = (cfg->pose || !a.s.show_ghosts) ? 0.f : cfg->figure_every;
        float home_z = a.side_movement ? DEPTH_SIDE_MOVING : DEPTH_STANDING;
        int at_home = fabsf(a.cam.z - home_z) < 1.f;
        sim_update(&a.sim, dt, a.stats.cpu, a.stats.bps, a.cam.x, a.cam.z,
                   render_view_half_width(&a.cam, aspect), render_view_height(&a.cam),
                   moved_dx, figure_every, at_home);
        if (cfg->pose) {
            /* Pose tool: the chosen figure just stands there at full strength. */
            a.sim.ghost.phase = GHOST_STANDING;
            a.sim.ghost.alpha = 1.f;
            a.sim.ghost.model = a.pose_model;
            a.sim.ghost.x = a.cam.x;
            a.sim.ghost.z = GHOST_STAND_Z;
            a.sim.ghost.t = 0.f;
        }

        /* Inside the wall: while the surge has swept over the camera, the
         * ghost tails go to maximum, so the crossing smears into a long
         * streak until the wall lets go. */
        Settings eff = a.s;
        int engulfed = !cfg->pose && sim_wall_z(&a.sim, a.cam.x, a.cam.y) >= a.cam.z - 0.3f;
        /* Three times the slider's longest tail: 0.975 per 60 Hz frame decays
         * with a 0.66 s time constant, 0.9916 with 1.97 s. */
        eff.ghost_tail = engulfed ? 100 : eff.ghost_tail;
        a.r.ghost_decay_override = engulfed ? 0.9916f : 0.f;
        a.r.viewer_inside = engulfed;
        if (engulfed != a.engulfed) {
            a.engulfed = engulfed;
            plat_log("%s the wall at t=%.1f s (z=%.1f)", engulfed ? "inside" : "out of",
                     a.elapsed, a.cam.z);
        }

#ifndef _WIN32
        if (cfg->mode == MODE_EMBEDDED) {
            /* Follow the owner's window, right before drawing into ours: once
             * the owner's is gone, so is ours, and drawing would fail (see
             * plat_x11_embed). XScreenSaver normally sends SIGTERM first,
             * which SDL turns into a quit. Also track its size, which the
             * settings preview may change. */
            int pw, ph, cw, ch;
            if (plat_x11_embed_lost() ||
                !plat_x11_window_size((unsigned long)(uintptr_t)cfg->parent_hwnd, &pw, &ph)) break;
            SDL_GetWindowSize(a.win, &cw, &ch);
            if (pw != cw || ph != ch) SDL_SetWindowSize(a.win, pw, ph);
        }
#endif
        render_resize(&a.r, a.width, a.height);
        render_frame(&a.r, &eff, &a.sim, &a.cam, a.elapsed, dt);
        a.frames++;

        if (cfg->dump_path && cfg->frame_limit > 0 && a.frames >= cfg->frame_limit) {
            if (render_dump_png(&a.r, cfg->dump_path)) plat_log("wrote %s", cfg->dump_path);
            else { plat_log("failed to write %s", cfg->dump_path); code = 5; }
        }
        SDL_GL_SwapWindow(a.win);
        if (cfg->frame_limit > 0 && a.frames >= cfg->frame_limit) break;

        /* Cap == refresh rate: vsync alone paces us. Below it we keep vsync
         * (so presents align to vblanks) and additionally hold each frame to
         * a fixed deadline schedule, which yields every n-th vblank. */
        if (a.use_vsync && a.swap_n == 1) continue;

        /* Frame pacing: sleep out the remainder instead of spinning. */
        now = SDL_GetTicksNS();
        if (next_deadline > now + 200000ull) SDL_DelayPrecise(next_deadline - now);
        now = SDL_GetTicksNS();
        next_deadline += frame_ns;
        if (next_deadline < now) next_deadline = now + frame_ns; /* we fell behind */
    }
    plat_log("stop: %d frames in %.2f s (%.1f fps avg), cpu=%.2f net=%.0f bps, points/frame=%d",
             a.frames, a.elapsed, a.elapsed > 0.f ? a.frames / a.elapsed : 0.f,
             a.stats.cpu, a.stats.bps, a.r.points_drawn);

done:
    render_shutdown(&a.r);
    if (a.gl) SDL_GL_DestroyContext(a.gl);
    if (a.win) SDL_DestroyWindow(a.win);
    SDL_Quit();
    return code;
}
