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
#include <math.h>

#define PAGE_W 640
#define PAGE_H 360
#define ROW_H  70.f
#define TOP    130.f
#define PAD    40.f

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
    int action;
    int checked;
} Row;

static int rows_of(int auto_move_on, Row *out, int cap) {
    int n = 0;
    if (n < cap) { out[n++] = (Row){ "Auto move", VRMENU_AUTO_MOVE, auto_move_on }; }
    if (n < cap) { out[n++] = (Row){ "Exit", VRMENU_EXIT, 0 }; }
    return n;
}

static float row_y(int i) { return TOP + (float)i * ROW_H; }

static int row_at(float u, float v, int count) {
    float x = u * PAGE_W, y = v * PAGE_H;
    if (x < PAD || x > PAGE_W - PAD) return -1;
    for (int i = 0; i < count; ++i) {
        float ry = row_y(i);
        if (y >= ry && y < ry + ROW_H - 8.f) return i;
    }
    return -1;
}

int vrmenu_init(VrMenu *m) {
    memset(m, 0, sizeof *m);
    m->w = PAGE_W; m->h = PAGE_H;
    m->hover = -1;
    m->width_m = 0.6f; m->height_m = 0.6f * (float)PAGE_H / (float)PAGE_W;
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
    if (open) m->place_pending = 1;
    else m->hover = -1;
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

void vrmenu_hover(VrMenu *m, float u, float v) {
    Row rows[4];
    int n = rows_of(0, rows, 4);
    m->hover = row_at(u, v, n);
}

int vrmenu_click(VrMenu *m, float u, float v) {
    Row rows[4];
    int n = rows_of(0, rows, 4);
    int idx = row_at(u, v, n);
    m->hover = idx;
    if (idx < 0) return VRMENU_NONE;
    return rows[idx].action;
}

void vrmenu_paint(VrMenu *m, int auto_move_on) {
    Row rows[4];
    int n = rows_of(auto_move_on, rows, 4);
    const float bg[4]    = { 0.03f, 0.05f, 0.07f, 0.88f };
    const float edge[4]  = { 0.45f, 0.85f, 0.6f,  0.85f };
    const float head[4]  = { 1.0f,  0.82f, 0.25f, 1.f };
    const float txt[4]   = { 0.88f, 0.92f, 0.95f, 1.f };
    const float sel[4]   = { 0.12f, 0.35f, 0.25f, 0.9f };

    glBindFramebuffer(GL_FRAMEBUFFER, m->fbo);
    glViewport(0, 0, PAGE_W, PAGE_H);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    hud_ui_begin(PAGE_W, PAGE_H, 1.f);
    hud_ui_rect(0.f, 0.f, (float)PAGE_W, (float)PAGE_H, bg);
    hud_ui_frame(4.f, 4.f, (float)PAGE_W - 4.f, (float)PAGE_H - 4.f, 3.f, edge);
    hud_ui_text((float)PAGE_W * 0.5f, 46.f, "THE BLACK WALL", 5.f, 0, head, 1);
    for (int i = 0; i < n; ++i) {
        float y = row_y(i);
        if (i == m->hover)
            hud_ui_rect(PAD, y - 4.f, (float)PAGE_W - PAD, y + ROW_H - 14.f, sel);
        hud_ui_text(PAD + 16.f, y + 12.f, rows[i].label, 3.5f, -1, txt, 1);
        if (rows[i].action == VRMENU_AUTO_MOVE)
            hud_ui_text((float)PAGE_W - PAD - 24.f, y + 12.f, rows[i].checked ? "ON" : "OFF", 3.5f, 1, head, 1);
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
