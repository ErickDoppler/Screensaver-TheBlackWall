/* OpenGL 3.3 core renderer. Draws the floor, the wall (as point sprites
 * generated on the GPU), its reflection, then optional ghost-tail and blur
 * post-processing. No vertex buffers: everything comes from gl_VertexID. */
#ifndef BW_RENDER_H
#define BW_RENDER_H
#include "mathx.h"
#include "settings.h"
#include "sim.h"

typedef struct Camera {
    double x;            /* world x (double: side movement runs for hours) */
    float  y, z;         /* height above the floor, distance from the wall */
    float  yaw, pitch;   /* radians; yaw 0 looks straight at the wall */
    float  fov_y;        /* radians */
    /* VR: when set, render_frame draws with this eye's own view/projection
     * and position instead of building them from yaw/pitch/fov_y. x/y/z stay
     * the player's logical position regardless - the fog, the level of
     * detail and the corridor are all keyed on it, not on where either eye
     * actually is. */
    int    vr_active;
    mat4   vr_view, vr_proj;
    vec3   vr_eye;
} Camera;

void camera_reset(Camera *c);
/* How far from the wall the camera may go (the "walking corridor"). */
#define CORRIDOR_DEPTH 1000.f
/* Beyond this distance from the wall, the wall grows and figures appear at
 * the camera's depth. */
#define FAR_BACK_Z 50.f
/* The projection's far plane - shared with app.c, which builds a VR eye's
 * own (off-axis) projection matrix rather than calling m4_perspective. */
#define BW_FAR_PLANE 2500.0f

typedef struct WallUniforms {
    int view_proj, cam_pos, cols, cell_base, spacing, grid_x0, point_world, proj_scale,
        max_point_px, time, wave_k, wave_amp, wave_phase, wave_phase2, fade_height, fog_dist,
        mirror, alpha_mul, shimmer, spike_count, spikes, pixel_type, color, glyphs,
        surge, skip_half, wall_height, scar_mask, scar_rect, scar_state, scar_color;
} WallUniforms;

typedef struct FloorUniforms {
    int view_proj, cam_x, half_width, z_near, z_far, cam_pos, floor_color,
        space_color, wall_color, fog_dist, wave_k, wave_phase, wave_amp;
} FloorUniforms;

typedef struct Renderer {
    unsigned prog_wall, prog_floor, prog_ghost, prog_blur, prog_blit;
    unsigned prog_debris, prog_city, prog_man;      /* all share wall.frag */
    struct { unsigned vao, vbo; int count; } models[8];   /* figure point clouds */
    int      model_count;
    WallUniforms  wu;
    FloorUniforms fu;
    int ghost_cur, ghost_prev, ghost_decay, blur_tex, blur_step, blit_tex;
    unsigned prog_glitch;                           /* signal loss at the corridor's end */
    unsigned prog_mask, fbo_mask, tex_mask;         /* the figure's silhouette for the scar */
    unsigned prog_ember;                            /* the last sparks a figure leaves */
    int      mask_model, mask_yaw;                  /* what the mask currently holds */
    unsigned vao, glyph_tex;
    unsigned fbo_scene, tex_scene, fbo_acc[2], tex_acc[2], fbo_tmp, tex_tmp, fbo_out, tex_out;
    int acc_index, acc_valid;
    float tv_off;                                   /* 0 = picture, 1 = collapsed, animated */
    /* Per-frame ghost-tail decay override (per 60 Hz frame, 0 = use the
     * slider). Lets the wall hold a much longer streak than the slider's max
     * while it has swept over the camera. */
    float ghost_decay_override;
    /* The wall has swept over the camera. Only from in there is the dust of
     * a pulverised figure visible; from outside there is just the imprint. */
    int viewer_inside;
    int width, height;      /* current viewport */
    int fbo_w, fbo_h;       /* size the offscreen targets were built for */
    float max_point_px;
    int points_drawn;       /* diagnostics */
    /* Where the finished frame goes when nothing needs post-processing: 0 for
     * the window, or a VR eye's swapchain framebuffer. */
    unsigned target_fbo;
} Renderer;

int  render_init(Renderer *r);
void render_resize(Renderer *r, int w, int h);
void render_frame(Renderer *r, const Settings *s, const Sim *sim,
                  const Camera *cam, float time, float dt);
void render_shutdown(Renderer *r);
/* Reads back the default framebuffer and writes a PNG. 1 on success. */
int  render_dump_png(const Renderer *r, const char *path);

/* Helpers shared with the simulation: how much wall the camera can see. */
float render_view_half_width(const Camera *cam, float aspect);
float render_view_height(const Camera *cam);
#endif
