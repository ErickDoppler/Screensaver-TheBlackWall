/* User settings. One X-macro table drives defaults, clamping, persistence
 * (registry on Windows) and the command-line parser, so a setting is added in
 * exactly one place. Sliders are stored as 0..100 integers. */
#ifndef BW_SETTINGS_H
#define BW_SETTINGS_H

enum { PIXEL_SQUARE = 0, PIXEL_ROUND = 1, PIXEL_MATRIX = 2 };

/*  field               key                   default min  max */
#define BW_SETTINGS_INT(X) \
    X(side_movement,      "side-movement",      1,      0,   1)   \
    X(movement_speed,     "movement-speed",     30,     0,   100) \
    X(mouse_rotation,     "mouse-rotation",     1,      0,   1)   \
    X(exit_on_mouse_move, "exit-on-mouse-move", 1,      0,   1)   \
    X(mouse_sensitivity,  "mouse-sensitivity",  50,     0,   100) \
    X(click_resets_view,  "click-resets-view",  0,      0,   1)   \
    X(exit_on_any_key,    "exit-on-any-key",    0,      0,   1)   \
    X(density,            "density",            100,    0,   100) \
    X(pixel_size,         "pixel-size",         42,     0,   100) \
    X(blur,               "blur",               20,     0,   100) \
    X(ghost_tail,         "ghost-tail",         30,     0,   100) \
    X(shimmer,            "shimmer",            0,      0,   100) \
    X(pixel_type,         "pixel-type",         0,      0,   2)   \
    X(horizon,            "horizon",            1,      0,   1)   \
    X(show_ghosts,        "show-ghosts",        1,      0,   1)   \
    X(ghost_yaws,         "ghost-yaws",         0,      0,   0x1FFFFF) \
    X(target_fps,         "fps",                60,     10,  120)

/*  field         key            default (0xRRGGBB) */
#define BW_SETTINGS_COLOR(X) \
    X(wall_color,  "wall-color",  0xFF3B1Fu) \
    X(floor_color, "floor-color", 0x140507u) \
    X(space_color, "space-color", 0x04000Au) \
    X(horizon_color, "horizon-color", 0x3FA0FFu)

typedef struct Settings {
#define X(field, key, def, mn, mx) int field;
    BW_SETTINGS_INT(X)
#undef X
#define X(field, key, def) unsigned field;
    BW_SETTINGS_COLOR(X)
#undef X
} Settings;

void settings_defaults(Settings *s);
void settings_clamp(Settings *s);
void settings_load(Settings *s);        /* defaults, then platform store */
void settings_save(const Settings *s);
/* Tries to consume argv[*i] (and possibly argv[*i+1]) as a setting override
 * such as "--density 40", "--density=40", "--no-side-movement",
 * "--pixel-type round", "--wall-color #ff3b1f". Returns 1 if consumed. */
int settings_parse_arg(Settings *s, int argc, char **argv, int *i);
/* Parses "#RRGGBB", "RRGGBB" or "0xRRGGBB". Returns 1 on success. */
int settings_parse_color(const char *text, unsigned *out);
const char *settings_pixel_type_name(int t);
/* Facing of figure `model` in 45-degree steps (0..7), packed 3 bits each
 * into the ghost_yaws setting. */
int  settings_figure_yaw(const Settings *s, int model);
void settings_set_figure_yaw(Settings *s, int model, int yaw);
#endif
