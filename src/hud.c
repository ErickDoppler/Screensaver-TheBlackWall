#include "hud.h"
#include "gl_loader.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

extern const char shader_hud_vert[], shader_hud_frag[];

/* --- the font: 5 x 7, drawn by hand, ASCII 32..95 plus a degree sign ------ */
static const char *const glyphs[65][7] = {
    { ".....", ".....", ".....", ".....", ".....", ".....", "....." }, /* space */
    { "..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.." }, /* ! */
    { ".#.#.", ".#.#.", ".....", ".....", ".....", ".....", "....." }, /* " */
    { ".#.#.", "#####", ".#.#.", ".#.#.", "#####", ".#.#.", "....." }, /* # */
    { "..#..", ".####", "#.#..", ".###.", "..#.#", "####.", "..#.." }, /* $ */
    { "##...", "##..#", "...#.", "..#..", ".#...", "#..##", "...##" }, /* % */
    { ".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#" }, /* & */
    { "..#..", "..#..", ".....", ".....", ".....", ".....", "....." }, /* ' */
    { "...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#." }, /* ( */
    { ".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..." }, /* ) */
    { ".....", "..#..", "#.#.#", ".###.", "#.#.#", "..#..", "....." }, /* * */
    { ".....", "..#..", "..#..", "#####", "..#..", "..#..", "....." }, /* + */
    { ".....", ".....", ".....", ".....", ".##..", "..#..", ".#..." }, /* , */
    { ".....", ".....", ".....", "#####", ".....", ".....", "....." }, /* - */
    { ".....", ".....", ".....", ".....", ".....", ".##..", ".##.." }, /* . */
    { "....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#...." }, /* / */
    { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." }, /* 0 */
    { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." }, /* 1 */
    { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" }, /* 2 */
    { "#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###." }, /* 3 */
    { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." }, /* 4 */
    { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." }, /* 5 */
    { "..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###." }, /* 6 */
    { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." }, /* 7 */
    { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." }, /* 8 */
    { ".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.." }, /* 9 */
    { ".....", ".##..", ".##..", ".....", ".##..", ".##..", "....." }, /* : */
    { ".....", ".##..", ".##..", ".....", ".##..", "..#..", ".#..." }, /* ; */
    { "...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#." }, /* < */
    { ".....", ".....", "#####", ".....", "#####", ".....", "....." }, /* = */
    { ".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..." }, /* > */
    { ".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.." }, /* ? */
    { ".###.", "#...#", "....#", ".##.#", "#.#.#", "#.#.#", ".###." }, /* @ */
    { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" }, /* A */
    { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." }, /* B */
    { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." }, /* C */
    { "###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.." }, /* D */
    { "#####", "#....", "#....", "####.", "#....", "#....", "#####" }, /* E */
    { "#####", "#....", "#....", "####.", "#....", "#....", "#...." }, /* F */
    { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####" }, /* G */
    { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" }, /* H */
    { ".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###." }, /* I */
    { "..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.." }, /* J */
    { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" }, /* K */
    { "#....", "#....", "#....", "#....", "#....", "#....", "#####" }, /* L */
    { "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#" }, /* M */
    { "#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#" }, /* N */
    { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." }, /* O */
    { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." }, /* P */
    { ".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#" }, /* Q */
    { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" }, /* R */
    { ".####", "#....", "#....", ".###.", "....#", "....#", "####." }, /* S */
    { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." }, /* T */
    { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." }, /* U */
    { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." }, /* V */
    { "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#." }, /* W */
    { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" }, /* X */
    { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." }, /* Y */
    { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" }, /* Z */
    { ".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###." }, /* [ */
    { "#....", ".#...", ".#...", "..#..", "...#.", "...#.", "....#" }, /* \ */
    { ".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###." }, /* ] */
    { "..#..", ".#.#.", "#...#", ".....", ".....", ".....", "....." }, /* ^ */
    { ".....", ".....", ".....", ".....", ".....", ".....", "#####" }, /* _ */
    { ".##..", "#..#.", "#..#.", ".##..", ".....", ".....", "....." }, /* degree, as 0x7F */
};

#define CELL_W 6
#define CELL_H 8
#define ATLAS_COLS 16
#define ATLAS_ROWS 5

typedef struct { float x, y, u, v, r, g, b, a; } HVert;

static unsigned g_prog, g_vao, g_vbo, g_font;
static HVert *g_v;
static int g_n, g_cap;
static int g_ui_w, g_ui_h;

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

int hud_init(void) {
    g_prog = link_program(shader_hud_vert, shader_hud_frag, "hud");
    if (!g_prog) return 0;
    unsigned char px[ATLAS_ROWS * CELL_H][ATLAS_COLS * CELL_W];
    memset(px, 0, sizeof px);
    for (int gi = 0; gi < 65; ++gi) {
        int cx = (gi % ATLAS_COLS) * CELL_W, cy = (gi / ATLAS_COLS) * CELL_H;
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 5; ++x)
                if (glyphs[gi][y][x] == '#') px[cy + y][cx + x] = 255;
    }
    glGenTextures(1, &g_font);
    glBindTexture(GL_TEXTURE_2D, g_font);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_COLS * CELL_W, ATLAS_ROWS * CELL_H, 0, GL_RED, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(HVert), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(HVert), (void *)8);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(HVert), (void *)16);
    glBindVertexArray(0);
    return 1;
}

void hud_shutdown(void) {
    if (g_prog) glDeleteProgram(g_prog);
    if (g_vbo) glDeleteBuffers(1, &g_vbo);
    if (g_vao) glDeleteVertexArrays(1, &g_vao);
    if (g_font) glDeleteTextures(1, &g_font);
    free(g_v);
    g_v = NULL;
    g_cap = g_n = 0;
}

/* --- primitives ------------------------------------------------------------ */
static void push(float x, float y, float u, float v, const float c[4]) {
    if (g_n >= g_cap) {
        g_cap = g_cap ? g_cap * 2 : 4096;
        g_v = realloc(g_v, sizeof *g_v * (size_t)g_cap);
    }
    HVert *h = &g_v[g_n++];
    h->x = x; h->y = y; h->u = u; h->v = v;
    h->r = c[0]; h->g = c[1]; h->b = c[2]; h->a = c[3];
}
static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const float c[4]) {
    push(x0, y0, u0, v0, c); push(x1, y0, u1, v0, c); push(x1, y1, u1, v1, c);
    push(x0, y0, u0, v0, c); push(x1, y1, u1, v1, c); push(x0, y1, u0, v1, c);
}
static void line(float x0, float y0, float x1, float y1, float w, const float c[4]) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-3f) return;
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    push(x0 + nx, y0 + ny, -1, 0, c); push(x1 + nx, y1 + ny, -1, 0, c); push(x1 - nx, y1 - ny, -1, 0, c);
    push(x0 + nx, y0 + ny, -1, 0, c); push(x1 - nx, y1 - ny, -1, 0, c); push(x0 - nx, y0 - ny, -1, 0, c);
}
static void rect_outline(float x0, float y0, float x1, float y1, float w, const float c[4]) {
    line(x0, y0, x1, y0, w, c); line(x1, y0, x1, y1, w, c);
    line(x1, y1, x0, y1, w, c); line(x0, y1, x0, y0, w, c);
}

static int glyph_index(unsigned char ch) {
    if (ch == 0x7F) return 64;
    if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32);
    if (ch < 32 || ch > 95) return 0;
    return ch - 32;
}

/* Text at pixel scale s; align -1 left, 0 centre, 1 right. */
static float text_width(const char *t, float s) { return (float)strlen(t) * CELL_W * s - s; }
static void text(float x, float y, const char *t, float s, int align, const float c[4], int shadow) {
    float w = text_width(t, s);
    if (align == 0) x -= w * 0.5f;
    else if (align > 0) x -= w;
    x = floorf(x); y = floorf(y);
    float aw = ATLAS_COLS * CELL_W, ah = ATLAS_ROWS * CELL_H;
    float sc[4] = { 0.f, 0.f, 0.f, c[3] * 0.6f };
    for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
        float ox = pass == 0 ? s : 0.f, oy = pass == 0 ? s : 0.f;
        const float *col = pass == 0 ? sc : c;
        float cx = x;
        for (const char *p = t; *p; ++p) {
            int gi = glyph_index((unsigned char)*p);
            float u0 = (float)((gi % ATLAS_COLS) * CELL_W) / aw, v0 = (float)((gi / ATLAS_COLS) * CELL_H) / ah;
            float u1 = u0 + 5.f / aw, v1 = v0 + 7.f / ah;
            quad(cx + ox, y + oy, cx + ox + 5.f * s, y + oy + 7.f * s, u0, v0, u1, v1, col);
            cx += CELL_W * s;
        }
    }
}

/* ---- the public UI engine ------------------------------------------------ */
void hud_ui_begin(int w, int h, float scale) {
    (void)scale;
    g_n = 0;
    g_ui_w = w; g_ui_h = h;
}
void hud_ui_rect(float x0, float y0, float x1, float y1, const float col[4]) {
    quad(x0, y0, x1, y1, -1, 0, -1, 0, col);
}
void hud_ui_frame(float x0, float y0, float x1, float y1, float lw, const float col[4]) {
    rect_outline(x0, y0, x1, y1, lw, col);
}
void hud_ui_text(float x, float y, const char *s, float scale, int align, const float col[4], int shadow) {
    text(x, y, s, scale, align, col, shadow);
}
float hud_ui_text_width(const char *s, float scale) { return text_width(s, scale); }

void hud_ui_flush(void) {
    if (g_n == 0) return;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_prog);
    glUniform2f(glGetUniformLocation(g_prog, "uRes"), (float)g_ui_w, (float)g_ui_h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_font);
    glUniform1i(glGetUniformLocation(g_prog, "uFont"), 0);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(HVert) * (size_t)g_n), g_v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, g_n);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    g_n = 0;
}
