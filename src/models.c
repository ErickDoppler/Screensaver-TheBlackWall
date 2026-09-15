#include "models.h"

/* Symbols emitted by tools/gltf2points.c for each model. */
#define X(name, elev, color, yaw) \
    extern const int   name##_point_count; \
    extern const short name##_points[];    \
    extern const float name##_point_scale;
BW_MODELS(X)
#undef X

const ModelInfo bw_models[] = {
#define X(name, elev, color, yaw) { #name, elev, color, yaw, &name##_point_count, name##_points, &name##_point_scale },
    BW_MODELS(X)
#undef X
};
const int bw_model_count = (int)(sizeof bw_models / sizeof bw_models[0]);

float bw_model_height(int model) {
    static float cache[BW_MAX_MODELS];
    if (model < 0 || model >= bw_model_count || model >= BW_MAX_MODELS) return 1.8f;
    if (cache[model] > 0.f) return cache[model];
    const ModelInfo *m = &bw_models[model];
    short top = 0;
    for (int i = 0; i < *m->count; ++i)
        if (m->points[i * 3 + 1] > top) top = m->points[i * 3 + 1];
    cache[model] = top * *m->scale;
    return cache[model] > 0.f ? cache[model] : 1.8f;
}

float bw_model_radius(int model) {
    static float cache[BW_MAX_MODELS];
    if (model < 0 || model >= bw_model_count || model >= BW_MAX_MODELS) return 1.f;
    if (cache[model] > 0.f) return cache[model];
    const ModelInfo *m = &bw_models[model];
    short far = 1;
    for (int i = 0; i < *m->count; ++i) {
        short ax = (short)(m->points[i * 3 + 0] < 0 ? -m->points[i * 3 + 0] : m->points[i * 3 + 0]);
        short az = (short)(m->points[i * 3 + 2] < 0 ? -m->points[i * 3 + 2] : m->points[i * 3 + 2]);
        if (ax > far) far = ax;
        if (az > far) far = az;
    }
    cache[model] = far * *m->scale;
    return cache[model];
}
