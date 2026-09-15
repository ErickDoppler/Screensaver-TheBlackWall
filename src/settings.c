#include "settings.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void settings_defaults(Settings *s) {
#define X(field, key, def, mn, mx) s->field = def;
    BW_SETTINGS_INT(X)
#undef X
#define X(field, key, def) s->field = def;
    BW_SETTINGS_COLOR(X)
#undef X
}

void settings_clamp(Settings *s) {
#define X(field, key, def, mn, mx) s->field = clampi(s->field, mn, mx);
    BW_SETTINGS_INT(X)
#undef X
#define X(field, key, def) s->field &= 0xFFFFFFu;
    BW_SETTINGS_COLOR(X)
#undef X
}

void settings_load(Settings *s) {
    int v;
    settings_defaults(s);
#define X(field, key, def, mn, mx) if (plat_store_read_int(key, &v)) s->field = v;
    BW_SETTINGS_INT(X)
#undef X
#define X(field, key, def) if (plat_store_read_int(key, &v)) s->field = (unsigned)v;
    BW_SETTINGS_COLOR(X)
#undef X
    settings_clamp(s);
}

void settings_save(const Settings *s) {
#define X(field, key, def, mn, mx) plat_store_write_int(key, s->field);
    BW_SETTINGS_INT(X)
#undef X
#define X(field, key, def) plat_store_write_int(key, (int)s->field);
    BW_SETTINGS_COLOR(X)
#undef X
}

int settings_parse_color(const char *text, unsigned *out) {
    if (!text) return 0;
    if (text[0] == '#') text++;
    else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    if (strlen(text) != 6) return 0;
    for (int k = 0; k < 6; ++k) if (!isxdigit((unsigned char)text[k])) return 0;
    *out = (unsigned)strtoul(text, NULL, 16);
    return 1;
}

int settings_figure_yaw(const Settings *s, int model) {
    if (model < 0 || model > 6) return 0;
    return (s->ghost_yaws >> (model * 3)) & 7;
}

void settings_set_figure_yaw(Settings *s, int model, int yaw) {
    if (model < 0 || model > 6) return;
    int shift = model * 3;
    s->ghost_yaws = (s->ghost_yaws & ~(7 << shift)) | ((yaw & 7) << shift);
}

const char *settings_pixel_type_name(int t) {
    switch (t) {
    case PIXEL_ROUND: return "round";
    case PIXEL_MATRIX: return "matrix";
    default: return "square";
    }
}

static int parse_int_value(const char *key, const char *val, int *out) {
    if (!val) return 0;
    if (strcmp(key, "pixel-type") == 0) {
        if (!strcmp(val, "square")) { *out = PIXEL_SQUARE; return 1; }
        if (!strcmp(val, "round"))  { *out = PIXEL_ROUND;  return 1; }
        if (!strcmp(val, "matrix")) { *out = PIXEL_MATRIX; return 1; }
    }
    if (!strcmp(val, "on") || !strcmp(val, "true") || !strcmp(val, "yes")) { *out = 1; return 1; }
    if (!strcmp(val, "off") || !strcmp(val, "false") || !strcmp(val, "no")) { *out = 0; return 1; }
    char *end;
    long v = strtol(val, &end, 10);
    if (end == val || *end) return 0;
    *out = (int)v;
    return 1;
}

/* Splits "--name=value" / "-name value" into name and value pointers. */
static const char *split_arg(const char *arg, char *name, size_t name_cap, int *has_inline_value) {
    while (*arg == '-') arg++;
    const char *eq = strchr(arg, '=');
    size_t n = eq ? (size_t)(eq - arg) : strlen(arg);
    name[0] = 0;
    if (n == 0 || n >= name_cap) return NULL;
    memcpy(name, arg, n);
    name[n] = 0;
    *has_inline_value = eq != NULL;
    return eq ? eq + 1 : NULL;
}

int settings_parse_arg(Settings *s, int argc, char **argv, int *i) {
    const char *arg = argv[*i];
    if (arg[0] != '-') return 0;
    char name[64];
    int inline_val = 0;
    const char *val = split_arg(arg, name, sizeof name, &inline_val);
    if (name[0] == 0) return 0;

    /* "--no-<bool-key>" form */
    int negate = 0;
    const char *key = name;
    if (strncmp(name, "no-", 3) == 0) { negate = 1; key = name + 3; }

#define X(field, key_str, def, mn, mx)                                         \
    if (strcmp(key, key_str) == 0) {                                           \
        if (negate) { s->field = 0; return 1; }                                \
        if ((mx) == 1 && !inline_val &&                                        \
            (*i + 1 >= argc || argv[*i + 1][0] == '-')) { s->field = 1; return 1; } \
        if (!inline_val) { if (*i + 1 >= argc) return 0; val = argv[++*i]; }   \
        int v; if (!parse_int_value(key_str, val, &v)) return 0;               \
        s->field = clampi(v, mn, mx); return 1;                                \
    }
    BW_SETTINGS_INT(X)
#undef X
#define X(field, key_str, def)                                                 \
    if (strcmp(key, key_str) == 0) {                                           \
        if (!inline_val) { if (*i + 1 >= argc) return 0; val = argv[++*i]; }   \
        unsigned c; if (!settings_parse_color(val, &c)) return 0;              \
        s->field = c; return 1;                                                \
    }
    BW_SETTINGS_COLOR(X)
#undef X
    return 0;
}
