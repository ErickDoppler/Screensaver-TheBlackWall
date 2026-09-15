#version 330 core
// Sparse pixels lying on the floor so the eye can feel the camera moving.
// A coarse grid of cells around the camera; each cell may hold one pixel at
// a hashed offset. Hashing is on the world cell, so debris stays put.

uniform mat4  uViewProj;
uniform vec3  uCamPos;
uniform int   uCols;
uniform int   uCellBase;      // world index of column 0
uniform int   uZBase;         // world index of row 0
uniform float uCell;          // cell size in world units
uniform float uX0;            // x of column 0 (origin-relative)
uniform float uZ0;            // z of row 0
uniform float uPointWorld;
uniform float uProjScale;
uniform float uMaxPointPx;
uniform float uTime;
uniform float uFogDist;
uniform vec3  uColor;

flat out float vGlyph;
out float vAlpha;
out float vBright;
out vec3  vColor;
out float vScar;   // never a scar here; wall.frag is shared
flat out float vSize;  // sprite size in pixels; small ones get a soft edge

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vScar = 0.0;
    vSize = 8.0;   // these are drawn with their own shapes
    int i = gl_VertexID % uCols;
    int j = gl_VertexID / uCols;
    vec2 cell = vec2(float((uCellBase + i) & 0xFFFF), float((uZBase + j) & 0xFFFF));
    float h1 = hash12(cell), h2 = hash12(cell.yx + 7.0), h3 = hash12(cell + 13.0);
    vec3 pos = vec3(uX0 + (float(i) + h1) * uCell, 0.02, uZ0 + (float(j) + h2) * uCell);

    float keep = step(0.55, h3);                       // ~45 % of cells populated
    float dist = distance(pos, uCamPos);
    float fog = 1.0 - smoothstep(uFogDist * 0.15, uFogDist, dist);
    float tw = 0.6 + 0.4 * sin(uTime * 1.1 + h1 * 6.2831);
    vAlpha = keep * fog * tw * 0.55;
    vColor = uColor;
    vBright = 1.0;
    vGlyph = floor(h2 * 16.0);
    if (vAlpha < 0.004) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 1.0;
        return;
    }
    vec4 clip = uViewProj * vec4(pos, 1.0);
    if (clip.w <= 0.05) {   // behind the camera: never let a driver mirror it
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 1.0;
        vAlpha = 0.0;
        return;
    }
    gl_Position = clip;
    gl_PointSize = clamp(uPointWorld * uProjScale / max(clip.w, 0.05), 1.0, uMaxPointPx);
}
