#version 330 core
// A cyber-city skyline far behind the camera, opposite the wall: columns of
// lit windows on a hashed skyline, wobbling like a mirage. Unreachable by
// design (the camera cannot walk that far). Drawn twice: upright and as a
// faint reflection below the horizon. The generated strip is much wider than
// the view and fades out with distance from the camera along x, so its ends
// are never seen as a hard edge.

uniform mat4  uViewProj;
uniform int   uCols;
uniform int   uCellBase;
uniform float uSpacing;
uniform float uRowStep;
uniform float uX0;            // x of column 0 (origin-relative)
uniform float uZ;             // horizon distance
uniform float uScale;         // geometry scale (1 = the original 75-unit design)
uniform float uCamX;          // camera x (origin-relative)
uniform float uHalfX;         // half width of the generated strip
uniform float uPointWorld;
uniform float uProjScale;
uniform float uMaxPointPx;
uniform float uTime;
uniform float uMirror;        // 1 upright, -1 reflection
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
    int cellIdx = (uCellBase + i) & 0xFFFF;
    int bld = cellIdx / 4;                                  // 4 columns per building
    float hb = hash12(vec2(float(bld), 3.0));
    float height = mix(0.8, 9.0, pow(hb, 1.7)) * uScale;
    // occasional gap between buildings
    float gap = step(0.12, hash12(vec2(float(bld), 9.0)));
    float y = (float(j) + 0.5) * uRowStep;

    float lit = hash12(vec2(float(cellIdx), float(j)));
    float win = step(0.42, lit) * gap * step(y, height);
    float x = uX0 + float(i) * uSpacing;
    // mirage: slow vertical wobble plus a faster fine ripple
    float xs = x / uScale, ys = y / uScale;
    float wob = (0.15 * sin(uTime * 0.6 + xs * 0.25) + 0.06 * sin(uTime * 1.7 + xs * 0.9 + ys)) * uScale;
    vec3 pos = vec3(x, (y + wob) * uMirror, uZ);

    float flick = 0.7 + 0.3 * sin(uTime * 2.0 + lit * 20.0);
    float topFade = 1.0 - 0.5 * y / max(height, 0.1);
    // gradual fade toward the ends of the strip (gone by ~70 % of half width)
    float dxn = (x - uCamX) / (uHalfX * 0.4);
    float xFade = exp(-dxn * dxn);
    vAlpha = win * flick * topFade * xFade * (uMirror > 0.0 ? 0.95 : 0.3);
    vColor = uColor;
    vBright = 1.0;
    vGlyph = floor(lit * 16.0);
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
    // small far sprites: brighten so they do not vanish
    vBright *= 1.0 + 0.9 * max(0.0, 4.0 - gl_PointSize);
}
