/* The figures that appear by the wall. Point tables are generated at build
 * time from the GLB files in res/models (see CMakeLists.txt, keep in sync).
 *
 *   name      color      who
 *   johny     crimson    engram
 *   alt       crimson    engram
 *   songbird  gold       half human
 *   brendan   yellow     AI (vending machine)
 *   skippy    yellow     AI (smart pistol)
 *   david     blue       human
 *   jackie    blue       human
 *
 * Heights are set in CMakeLists.txt: Johnny's 1.80 for most, Brendan 3 m,
 * Alt 15 m. All stand on the floor; elevation = height of the model's base
 * above the floor (0 for all). */
#ifndef BW_MODELS_H
#define BW_MODELS_H

#define BW_COLOR_ENGRAM 0xFF1408u   /* bright crimson (almost no blue: additive blending would turn it pink) */
#define BW_COLOR_HALF   0xFFC340u   /* gold */
#define BW_COLOR_AI     0xFFF03Au   /* bright yellow */
#define BW_COLOR_HUMAN  0x3FA0FFu   /* blue */

/* base_yaw: built-in turn in 45-degree steps (counter-clockwise from above),
 * applied on top of the facing the user saves in the pose tool. */
/*  X(name,     elevation, color,           base_yaw) */
#define BW_MODELS(X) \
    X(johny,    0.00f, BW_COLOR_ENGRAM, 0) \
    X(alt,      0.00f, BW_COLOR_ENGRAM, 0) \
    X(songbird, 0.00f, BW_COLOR_HALF,   0) \
    X(brendan,  0.00f, BW_COLOR_AI,     6) \
    X(skippy,   0.00f, BW_COLOR_AI,     0) \
    X(david,    0.00f, BW_COLOR_HUMAN,  0) \
    X(jackie,   0.00f, BW_COLOR_HUMAN,  0)

typedef struct ModelInfo {
    const char  *name;
    float        elevation;
    unsigned     color;         /* 0xRRGGBB */
    int          base_yaw;      /* 45-degree steps */
    const int   *count;         /* number of points */
    const short *points;        /* count * 3 quantised coordinates */
    const float *scale;         /* world units per quantum */
} ModelInfo;

extern const ModelInfo bw_models[];
extern const int       bw_model_count;
/* Height of a figure in world units (its base at 0), from the point data. */
float bw_model_height(int model);
/* Largest horizontal reach from the figure's axis, whatever its facing. */
float bw_model_radius(int model);
#define BW_MAX_MODELS 8
#endif
