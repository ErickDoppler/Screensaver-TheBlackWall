/* Minimal vector/matrix helpers. Column-major 4x4 matrices, OpenGL style. */
#ifndef BW_MATHX_H
#define BW_MATHX_H
#include <math.h>

#define BW_PI 3.14159265358979323846f

typedef struct { float x, y, z; } vec3;
typedef struct { float m[16]; } mat4;
/* An orthonormal frame: x right, y up, z backward (so -z is forward, the
 * OpenXR/OpenGL convention). Used only for a VR controller/headset pose. */
typedef struct { vec3 x, y, z; } basis3;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float smoothstepf(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
/* Exponential approach used for frame-rate independent smoothing. */
static inline float approachf(float cur, float target, float tau, float dt) {
    if (tau <= 0.f) return target;
    float a = 1.f - expf(-dt / tau);
    return cur + (target - cur) * a;
}

static inline vec3 v3(float x, float y, float z) { vec3 r = { x, y, z }; return r; }
static inline vec3 v3_add(vec3 a, vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline vec3 v3_sub(vec3 a, vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline vec3 v3_scale(vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline vec3 v3_cross(vec3 a, vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline vec3 v3_norm(vec3 a) {
    float l = sqrtf(v3_dot(a, a));
    return l > 1e-8f ? v3_scale(a, 1.f / l) : a;
}

static inline vec3 basis_apply(basis3 b, vec3 v) {
    return v3(b.x.x * v.x + b.y.x * v.y + b.z.x * v.z,
              b.x.y * v.x + b.y.y * v.y + b.z.y * v.z,
              b.x.z * v.x + b.y.z * v.y + b.z.z * v.z);
}
/* a's rotation applied to b's: b's local directions, turned into a's frame. */
static inline basis3 basis_mul(basis3 a, basis3 b) {
    basis3 r = { basis_apply(a, b.x), basis_apply(a, b.y), basis_apply(a, b.z) };
    return r;
}
/* Yaw about +y only, matching the desktop camera's own convention (yaw 0
 * looks along -z: see the view direction built in render.c). Used to turn a
 * VR headset's play-space pose into a world direction, since the runtime's
 * own -z is wherever the player was facing when the session started, not
 * necessarily at the wall. */
static inline basis3 basis_yaw(float yaw) {
    float s = sinf(yaw), c = cosf(yaw);
    basis3 b;
    b.x = v3(c, 0.f, s);
    b.y = v3(0.f, 1.f, 0.f);
    b.z = v3(-s, 0.f, c);
    return b;
}
/* Yaw about +y, then pitch about the new +x - the same convention as the
 * desktop camera's own forward vector (see render.c/app.c), so a VR "cockpit
 * tilt" control can pitch the play space exactly as far as the keyboard's I/K
 * pitches the desktop view. */
static inline basis3 basis_yaw_pitch(float yaw, float pitch) {
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    vec3 fwd = v3(sy * cp, sp, -cy * cp);
    vec3 right = v3(cy, 0.f, sy);
    vec3 up = v3_cross(right, fwd);
    basis3 b = { right, up, v3_scale(fwd, -1.f) };
    return b;
}

static inline mat4 m4_identity(void) {
    mat4 r = {{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }};
    return r;
}
static inline mat4 m4_mul(mat4 a, mat4 b) {
    mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
static inline mat4 m4_perspective(float fovy_rad, float aspect, float znear, float zfar) {
    mat4 r = {{0}};
    float f = 1.f / tanf(fovy_rad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.f;
    r.m[14] = (2.f * zfar * znear) / (znear - zfar);
    return r;
}
static inline mat4 m4_look_at(vec3 eye, vec3 center, vec3 up) {
    vec3 f = v3_norm(v3_sub(center, eye));
    vec3 s = v3_norm(v3_cross(f, up));
    vec3 u = v3_cross(s, f);
    mat4 r = m4_identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -v3_dot(s, eye);
    r.m[13] = -v3_dot(u, eye);
    r.m[14] = v3_dot(f, eye);
    return r;
}
/* View matrix from a camera basis (x right, y up, z back) at `eye` - the
 * VR headset/eye pose OpenXR hands back is exactly this shape. */
static inline mat4 m4_view(basis3 b, vec3 eye) {
    mat4 r = m4_identity();
    r.m[0] = b.x.x; r.m[4] = b.x.y; r.m[8]  = b.x.z;
    r.m[1] = b.y.x; r.m[5] = b.y.y; r.m[9]  = b.y.z;
    r.m[2] = b.z.x; r.m[6] = b.z.y; r.m[10] = b.z.z;
    r.m[12] = -v3_dot(b.x, eye);
    r.m[13] = -v3_dot(b.y, eye);
    r.m[14] = -v3_dot(b.z, eye);
    return r;
}
/* A frustum given the tangents of its four half-angles, which need not be
 * symmetric: a headset eye looks off to one side of its own screen. Same
 * near/far mapping as m4_perspective, just with an off-axis projection centre. */
static inline mat4 m4_frustum(float tl, float tr, float td, float tu, float znear, float zfar) {
    mat4 r = {{0}};
    float w = tr - tl, h = tu - td;
    r.m[0] = 2.f / w;
    r.m[5] = 2.f / h;
    r.m[8] = (tr + tl) / w;
    r.m[9] = (tu + td) / h;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.f;
    r.m[14] = (2.f * zfar * znear) / (znear - zfar);
    return r;
}
/* Cheap deterministic PRNG (xorshift32) so the simulation is reproducible. */
typedef struct { unsigned s; } rng_t;
static inline unsigned rng_u32(rng_t *r) {
    unsigned x = r->s ? r->s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->s = x;
    return x;
}
static inline float rng_f(rng_t *r) { return (rng_u32(r) >> 8) * (1.f / 16777216.f); }
static inline float rng_range(rng_t *r, float a, float b) { return a + (b - a) * rng_f(r); }
#endif
