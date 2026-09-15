#version 330 core
// The last of a figure: tiny sparks left where it stood. Each starts falling
// a fifth of a second after the retreating wall has passed its place, drifts
// down slowly for about five seconds, and shimmers the whole way. Half of
// them go out in mid-air somewhere between one and four seconds; the rest
// reach the floor and die there over three more.

uniform float uT;            // seconds since the wall passed this place
uniform vec3  uPos;          // x_rel, 0, z of the figure
uniform float uHeight;       // how tall the figure was
uniform float uPointWorld;
uniform float uProjScale;
uniform float uMaxPointPx;
uniform mat4  uViewProj;
uniform vec3  uColor;
uniform float uAlpha;

flat out float vGlyph;
out float vAlpha;
out float vBright;
out vec3  vColor;
out float vScar;       // never a scar here; wall.frag is shared
flat out float vSize;  // sprite size in pixels

float hash11(float p) { return fract(sin(p * 12.9898) * 43758.5453); }

const float DELAY   = 0.2;   // after the wall has gone past
const float FALL_S  = 5.0;   // down to the floor
const float FLOOR_S = 3.0;   // lying there, going out

void main() {
    vScar = 0.0;
    vSize = 2.0;
    float id = float(gl_VertexID);
    float h1 = hash11(id + 1.0), h2 = hash11(id + 7.0);
    float h3 = hash11(id + 13.0), h4 = hash11(id + 23.0);
    vColor = uColor;
    vGlyph = floor(h2 * 16.0);

    float t = uT - (DELAY + 0.15 * h4);
    float y0 = 0.10 + h3 * uHeight * 0.9;
    float fallS = FALL_S * (0.85 + 0.3 * h1);
    float u = clamp(t / fallS, 0.0, 1.0);
    float y = y0 * (1.0 - u * u);                 // a slow, accelerating drift down

    float life;
    if (h2 < 0.5) {
        // goes out in mid-air, somewhere between one and four seconds
        float tDie = 1.0 + 3.0 * h4;
        life = 1.0 - smoothstep(tDie - 0.35, tDie, t);
    } else {
        y = max(y, 0.015);                        // settles on the floor
        life = 1.0 - smoothstep(0.0, FLOOR_S, max(0.0, t - fallS));
    }

    // A true sparkle: mostly dark, snapping on and off at its own fast rate
    // rather than pulsing smoothly.
    float rate = 14.0 + 22.0 * h2;
    float tick = floor(t * rate) + id * 3.0;
    float on = step(0.45, hash11(tick));
    float shimmer = on * (0.55 + 0.45 * hash11(tick + 5.0));

    vec3 pos = vec3(uPos.x + (h1 - 0.5) * 0.7 + 0.05 * sin(t * 1.3 + h1 * 6.2831),
                    y,
                    uPos.z + (h2 - 0.5) * 0.5);

    vAlpha = t < 0.0 ? 0.0 : uAlpha * life * shimmer;
    vBright = 3.6;   // sparks read as bright points, not dots
    if (vAlpha < 0.004) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 1.0;
        return;
    }
    vec4 clip = uViewProj * vec4(pos, 1.0);
    if (clip.w <= 0.05) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); gl_PointSize = 1.0; vAlpha = 0.0; return; }
    gl_Position = clip;
    // tiny: never more than a few pixels across
    vSize = clamp(uPointWorld * uProjScale / max(clip.w, 0.05), 1.5, min(3.5, uMaxPointPx));
    gl_PointSize = vSize;
}
