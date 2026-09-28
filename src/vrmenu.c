/* The menu panel in the headset. See vrmenu.h.
 *
 * The page is laid out in its own pixels and drawn into a texture with the
 * HUD's text engine; the renderer hangs it on a quad in the world. The
 * pointer is a ray from a controller, turned into a position on the page, so
 * hit testing is the same arithmetic as the layout.
 */
#include "vrmenu.h"
#include "hud.h"
#include "gl_loader.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <math.h>

#define PAGE_W 900
#define PAGE_H 640
#define ROW_H  64.f
#define TOP    110.f
#define PAD    40.f
/* The slider track, in page pixels: left edge, right edge. */
#define SLIDER_X0 (PAGE_W * 0.52f)
#define SLIDER_X1 (PAGE_W - PAD - 90.f)

/* Settings-page sliders: not VRMENU_MOVEMENT/VRMENU_EXIT, since those are
 * applied straight to *s and need nothing back from the app. */
enum {
    VRMENU_EYE_DIST = 100,
    VRMENU_QUALITY,
    VRMENU_DENSITY,
    VRMENU_PIXEL_SIZE,
    VRMENU_GHOST_TAIL,
    VRMENU_BLUR,
};

extern const char shader_menu_vert[], shader_menu_frag[];
static unsigned g_quad_prog, g_quad_vao;

static unsigned compile(unsigned type, const char *src, const char *name) {
    unsigned sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    int ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(sh, sizeof log, NULL, log);
        plat_log("shader %s failed:\n%s", name, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static unsigned link_program(const char *vs, const char *fs, const char *name) {
    unsigned v = compile(GL_VERTEX_SHADER, vs, name);
    unsigned f = compile(GL_FRAGMENT_SHADER, fs, name);
    if (!v || !f) return 0;
    unsigned p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    int ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        plat_log("program %s failed to link:\n%s", name, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

typedef struct Row {
    const char *label;
    int kind;        /* 0 toggle/action, 1 submenu, 3 slider, 4 back */
    int action;
    int target_page; /* kind 1 */
    int checked;     /* kind 0: -1 for a plain action row (no ON/OFF shown) */
    float frac;       /* kind 3: where the handle sits, 0..1 */
    char right[16];   /* kind 3: the value, spelled out */
} Row;

/* Every slider on the Settings page is a Settings field with a known range;
 * this is the one place that maps an action to its field and range, so
 * rows_of/click/drag can never disagree about what a drag does. */
static int *slider_field(Settings *s, int action) {
    switch (action) {
    case VRMENU_EYE_DIST:   return &s->vr_eye_distance;
    case VRMENU_QUALITY:    return &s->vr_quality;
    case VRMENU_DENSITY:    return &s->density;
    case VRMENU_PIXEL_SIZE: return &s->pixel_size;
    case VRMENU_GHOST_TAIL: return &s->ghost_tail;
    case VRMENU_BLUR:       return &s->blur;
    default: return NULL;
    }
}
static void slider_range(int action, int *mn, int *mx) {
    *mn = 0;
    *mx = action == VRMENU_EYE_DIST ? 300 : 100;
}

static int rows_of(const Settings *s, int movement_on, int page, Row *out, int cap) {
    int n = 0;
    if (page == 0) {
        if (n < cap) out[n++] = (Row){ "Enable movement", 0, VRMENU_MOVEMENT, 0, movement_on, 0.f, "" };
        if (n < cap) out[n++] = (Row){ "Settings", 1, VRMENU_NONE, 1, 0, 0.f, "" };
        if (n < cap) out[n++] = (Row){ "Exit", 0, VRMENU_EXIT, 0, -1, 0.f, "" };
        return n;
    }
    if (n < cap) out[n++] = (Row){ "Back", 4, VRMENU_NONE, 0, -1, 0.f, "" };
    static const struct { const char *label; int action; } sliders[] = {
        { "Eye distance", VRMENU_EYE_DIST },
        { "Quality",      VRMENU_QUALITY },
        { "Wall density", VRMENU_DENSITY },
        { "Pixel size",   VRMENU_PIXEL_SIZE },
        { "Ghost tail",   VRMENU_GHOST_TAIL },
        { "Blur",         VRMENU_BLUR },
    };
    for (size_t i = 0; i < sizeof sliders / sizeof *sliders && n < cap; ++i) {
        int mn, mx;
        slider_range(sliders[i].action, &mn, &mx);
        /* rows_of only reads through this pointer; slider_field is shared
         * with the write paths (click/drag), which is why it isn't const. */
        int val = *slider_field((Settings *)s, sliders[i].action);
        Row r = { sliders[i].label, 3, sliders[i].action, 0, -1,
                  (float)(val - mn) / (float)(mx - mn), "" };
        if (sliders[i].action == VRMENU_QUALITY && val == 0) snprintf(r.right, sizeof r.right, "Auto");
        else snprintf(r.right, sizeof r.right, "%d%%", val);
        out[n++] = r;
    }
    return n;
}

static float row_y(int i) { return TOP + (float)i * ROW_H; }

static int row_at(float u, float v, int count) {
    float x = u * PAGE_W, y = v * PAGE_H;
    if (x < PAD || x > PAGE_W - PAD) return -1;
    for (int i = 0; i < count; ++i) {
        float ry = row_y(i);
        if (y >= ry && y < ry + ROW_H - 10.f) return i;
    }
    return -1;
}

int vrmenu_init(VrMenu *m) {
    memset(m, 0, sizeof *m);
    m->w = PAGE_W; m->h = PAGE_H;
    m->hover = -1;
    m->drag = -1.f;
    m->width_m = 0.8f; m->height_m = 0.8f * (float)PAGE_H / (float)PAGE_W;
    g_quad_prog = link_program(shader_menu_vert, shader_menu_frag, "menu");
    glGenVertexArrays(1, &g_quad_vao);
    glGenTextures(1, &m->tex);
    glBindTexture(GL_TEXTURE_2D, m->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PAGE_W, PAGE_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &m->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m->tex, 0);
    int ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    plat_log("vr: menu panel %dx%d, texture %u, %s", PAGE_W, PAGE_H, m->tex, ok ? "ready" : "incomplete");
    return ok;
}

void vrmenu_shutdown(VrMenu *m) {
    if (m->tex) glDeleteTextures(1, &m->tex);
    if (m->fbo) glDeleteFramebuffers(1, &m->fbo);
    m->tex = m->fbo = 0;
    if (g_quad_prog) glDeleteProgram(g_quad_prog);
    if (g_quad_vao) glDeleteVertexArrays(1, &g_quad_vao);
    g_quad_prog = g_quad_vao = 0;
}

void vrmenu_open(VrMenu *m, int open) {
    m->open = open;
    /* Put up again, it belongs in front of whoever put it up - not where it
     * was left the last time. The placing waits for a frame with poses in
     * it, since the runtime does not always hand them over. */
    if (open) { m->place_pending = 1; m->page = 0; m->drag = -1.f; }
    else { m->hover = -1; m->drag = -1.f; }
}

int vrmenu_aim(VrMenu *m, vec3 from, vec3 dir, float *u, float *v) {
    if (!m->open) return 0;
    /* the panel's plane: through m->pos, facing -z of its own basis */
    vec3 n = m->basis.z;
    float denom = v3_dot(n, dir);
    if (fabsf(denom) < 1e-4f) return 0;
    float t = v3_dot(n, v3_sub(m->pos, from)) / denom;
    if (t < 0.05f || t > 12.f) return 0;
    vec3 hit = v3_add(from, v3_scale(dir, t));
    vec3 d = v3_sub(hit, m->pos);
    float x = v3_dot(d, m->basis.x) / m->width_m + 0.5f;
    float y = 0.5f - v3_dot(d, m->basis.y) / m->height_m;
    if (x < 0.f || x > 1.f || y < 0.f || y > 1.f) return 0;
    *u = x; *v = y;
    return 1;
}

void vrmenu_hover(VrMenu *m, const Settings *s, int movement_on, float u, float v) {
    Row rows[8];
    int n = rows_of(s, movement_on, m->page, rows, 8);
    m->hover = row_at(u, v, n);
}

int vrmenu_click(VrMenu *m, Settings *s, int movement_on, float u, float v) {
    Row rows[8];
    int n = rows_of(s, movement_on, m->page, rows, 8);
    int idx = row_at(u, v, n);
    m->hover = idx;
    if (idx < 0) return VRMENU_NONE;
    Row *r = &rows[idx];
    if (r->kind == 4) { m->page = 0; return VRMENU_NONE; }
    if (r->kind == 1) { m->page = r->target_page; return VRMENU_NONE; }
    if (r->kind == 3) {
        m->drag = (float)r->action;
        int mn, mx;
        slider_range(r->action, &mn, &mx);
        float f = clampf((u * PAGE_W - SLIDER_X0) / (SLIDER_X1 - SLIDER_X0), 0.f, 1.f);
        *slider_field(s, r->action) = mn + (int)(f * (float)(mx - mn) + 0.5f);
        return VRMENU_NONE;
    }
    return r->action;   /* VRMENU_MOVEMENT or VRMENU_EXIT */
}

void vrmenu_drag(VrMenu *m, Settings *s, float u, float v) {
    (void)v;
    if (m->drag < 0.f) return;
    int action = (int)m->drag;
    int mn, mx;
    slider_range(action, &mn, &mx);
    float f = clampf((u * PAGE_W - SLIDER_X0) / (SLIDER_X1 - SLIDER_X0), 0.f, 1.f);
    *slider_field(s, action) = mn + (int)(f * (float)(mx - mn) + 0.5f);
}

void vrmenu_release(VrMenu *m) { m->drag = -1.f; }

void vrmenu_paint(VrMenu *m, const Settings *s, int movement_on) {
    Row rows[8];
    int n = rows_of(s, movement_on, m->page, rows, 8);
    const float bg[4]    = { 0.03f, 0.05f, 0.07f, 0.88f };
    const float edge[4]  = { 0.45f, 0.85f, 0.6f,  0.85f };
    const float head[4]  = { 1.0f,  0.82f, 0.25f, 1.f };
    const float txt[4]   = { 0.88f, 0.92f, 0.95f, 1.f };
    const float dim[4]   = { 0.55f, 0.62f, 0.68f, 1.f };
    const float sel[4]   = { 0.12f, 0.35f, 0.25f, 0.9f };
    const float bar[4]   = { 0.25f, 0.45f, 0.35f, 0.9f };
    const float barbg[4] = { 0.12f, 0.15f, 0.18f, 0.9f };

    glBindFramebuffer(GL_FRAMEBUFFER, m->fbo);
    glViewport(0, 0, PAGE_W, PAGE_H);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    hud_ui_begin(PAGE_W, PAGE_H, 1.f);
    hud_ui_rect(0.f, 0.f, (float)PAGE_W, (float)PAGE_H, bg);
    hud_ui_frame(4.f, 4.f, (float)PAGE_W - 4.f, (float)PAGE_H - 4.f, 3.f, edge);
    hud_ui_text((float)PAGE_W * 0.5f, 46.f, m->page == 0 ? "THE BLACK WALL" : "SETTINGS", 5.f, 0, head, 1);
    for (int i = 0; i < n; ++i) {
        const Row *r = &rows[i];
        float y = row_y(i);
        if (i == m->hover)
            hud_ui_rect(PAD, y - 4.f, (float)PAGE_W - PAD, y + ROW_H - 14.f, sel);
        const float *lc = r->kind == 4 ? dim : txt;
        char label[64];
        snprintf(label, sizeof label, "%s%s", r->kind == 4 ? "< " : "", r->label);
        hud_ui_text(PAD + 16.f, y + 12.f, label, 3.5f, -1, lc, 1);
        if (r->checked == 1) hud_ui_text((float)PAGE_W - PAD - 24.f, y + 12.f, "ON", 3.5f, 1, head, 1);
        else if (r->checked == 0) hud_ui_text((float)PAGE_W - PAD - 24.f, y + 12.f, "OFF", 3.5f, 1, dim, 1);
        if (r->kind == 3) {
            hud_ui_text((float)PAGE_W - PAD - 24.f, y + 12.f, r->right, 3.f, 1, dim, 1);
            float yy = y + 22.f;
            hud_ui_rect(SLIDER_X0, yy - 6.f, SLIDER_X1, yy + 6.f, barbg);
            hud_ui_rect(SLIDER_X0, yy - 6.f, SLIDER_X0 + (SLIDER_X1 - SLIDER_X0) * clampf(r->frac, 0.f, 1.f), yy + 6.f, bar);
        }
    }
    hud_ui_flush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void vrmenu_draw(const VrMenu *m, mat4 view_proj) {
    if (!m->open || !g_quad_prog) return;
    vec3 right = v3_scale(m->basis.x, m->width_m * 0.5f);
    vec3 up    = v3_scale(m->basis.y, m->height_m * 0.5f);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_quad_prog);
    glUniformMatrix4fv(glGetUniformLocation(g_quad_prog, "uViewProj"), 1, GL_FALSE, view_proj.m);
    glUniform3f(glGetUniformLocation(g_quad_prog, "uPos"), m->pos.x, m->pos.y, m->pos.z);
    glUniform3f(glGetUniformLocation(g_quad_prog, "uRight"), right.x, right.y, right.z);
    glUniform3f(glGetUniformLocation(g_quad_prog, "uUp"), up.x, up.y, up.z);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m->tex);
    glUniform1i(glGetUniformLocation(g_quad_prog, "uTex"), 0);
    glBindVertexArray(g_quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}
