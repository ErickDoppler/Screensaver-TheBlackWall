#version 330 core
// The figures: a model's vertices drawn as tiny pixels, nothing else. Each
// point flickers on its own and horizontal "scan" bands drop out and drift
// upward, so the figure reads as a cyberspace projection, not a mesh.
//
// Once the wave reaches a point it becomes dust; see the comment in main().
// The imprint left behind is not drawn here at all: the wall colours its own
// pixels, see the scar block in wall.vert.

layout(location = 0) in vec3 aPos;      // normalised: base at y = 0

uniform mat4  uViewProj;
uniform mat4  uModel;
uniform float uAlpha;
uniform float uTime;
uniform float uPointWorld;
uniform float uProjScale;
uniform float uMaxPointPx;
uniform vec3  uColor;
uniform float uSoften;       // blur slider 0..1: wider, dimmer dots (matches the wall)
// The dust clock. uWaveT counts from the moment the wave started, through the
// retreat; a point's own moment of being hit follows from where it stands
// between uWaveStart and uCrash.
uniform float uWaveT;
uniform float uWaveStart;
uniform float uCrash;
uniform float uWaveS;
uniform float uDustVisible;  // 1 only while the viewer is inside the wall
// wall displacement (same formulas as wall.vert) so the shadow rides the wall
uniform float uWaveK;
uniform float uWaveAmp;
uniform float uWavePhase;
uniform float uWavePhase2;
uniform vec4  uSurge;        // x_rel, amplitude, half_width, roll phase

flat out float vGlyph;
out float vAlpha;
out float vBright;
out vec3  vColor;
out float vScar;   // never a scar here; wall.frag is shared
flat out float vSize;  // sprite size in pixels; small ones get a soft edge

float hash11(float p) { return fract(sin(p * 12.9898) * 43758.5453); }

float wallDisp(float x, float y) {
    float d = uWaveAmp * sin(uWaveK * x + uWavePhase + 0.25 * y)
            + 0.3 * uWaveAmp * sin(2.3 * uWaveK * x + uWavePhase2 + 1.3);
    if (uSurge.y > 0.001) {
        float px = (x - uSurge.x) / uSurge.z;
        float profile = exp(-px * px * 0.7);
        float lean = min(0.35 * uSurge.y, 8.0) * pow(clamp(y / 9.0, 0.0, 1.0), 1.2);
        float rollAmp = 0.10 * min(1.0, 25.0 / uSurge.z);
        float roll = 1.0 + rollAmp * sin(0.35 * (x - uSurge.x) - uSurge.w) * clamp(y / 3.0, 0.0, 1.0);
        d += (uSurge.y + lean) * profile * roll;
    }
    return d;
}

void main() {
    vScar = 0.0;
    vSize = 8.0;   // these are drawn with their own shapes
    float id = float(gl_VertexID);
    float h = hash11(id);
    vec3 p = aPos;
    vColor = uColor;
    vGlyph = floor(h * 16.0);

    p.x += 0.004 * sin(uTime * 2.0 + h * 6.2831);
    float flicker = 0.55 + 0.45 * sin(uTime * 3.0 + h * 6.2831);
    // drifting scan bands: a thin slice of the body is missing at any moment
    float band = fract(p.y * 2.5 - uTime * 0.35);
    float scan = step(0.07, band);
    float soft = 1.0 + 1.5 * uSoften;

    // Once the wave has reached this point it is no longer body but dust: it
    // puffs, is thrown off in whatever direction it happens to take, drifts
    // and slows, and simply hangs there. It is not faded out. It disappears
    // when the wall, drawing back in, passes it again and scoops it up.
    vec4 wp = uModel * vec4(p, 1.0);
    float origZ = wp.z + (hash11(id * 5.7) - 0.5) * 0.3;
    float takeT = uWaveS * clamp((origZ - uWaveStart) / max(uCrash - uWaveStart, 0.001), 0.0, 1.0);
    float age = uWaveT - takeT;
    float alive = 1.0;
    float puff = 0.0;
    if (uWaveT > 0.0 && age > 0.0) {
        float a = hash11(id * 7.3) * 6.2831;
        float ct = hash11(id * 13.1) * 2.0 - 1.0;          // uniform on a sphere
        float st = sqrt(max(0.0, 1.0 - ct * ct));
        vec3 dir = vec3(st * cos(a), ct, st * sin(a));
        float v = 0.3 + 0.9 * hash11(id * 17.9);
        const float TAU = 2.5;                              // drag
        wp.xyz += dir * (v * TAU * (1.0 - exp(-age / TAU)));
        float drift = min(age, 3.0);
        wp.x += sin(uTime * 1.7 + h * 6.2831) * 0.12 * drift;
        wp.y += cos(uTime * 1.3 + h * 4.1) * 0.08 * drift;
        // The receding wall takes the cloud with it, outermost motes first.
        // Each mote is drawn into the wall over the last stretch rather than
        // blinking out, and its own threshold is offset at random so they do
        // not all go together.
        float surf = wallDisp(wp.x, wp.y);
        float grab = surf - wp.z + (hash11(id * 23.3) - 0.5) * 2.4;
        float caught = smoothstep(-0.2, 1.2, grab);
        wp.z = mix(surf, wp.z, caught);          // pulled in as it is taken
        // Only someone the wall has swallowed sees the dust at all; from
        // outside, the figure is simply gone and its imprint remains.
        alive = caught * uDustVisible;
        puff = exp(-age * 2.0);
    }
    vAlpha = uAlpha * flicker * scan * 0.16 / (soft * soft) * alive;
    vBright = 1.4 + 2.0 * puff;
    float dust = clamp(age * 0.3, 0.0, 1.0);
    if (vAlpha < 0.004) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 1.0;
        return;
    }
    vec4 clip = uViewProj * wp;
    if (clip.w <= 0.05) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); gl_PointSize = 1.0; vAlpha = 0.0; return; }
    gl_Position = clip;
    // dust motes swell a little as they disperse
    gl_PointSize = clamp(uPointWorld * soft * (1.0 + dust) * uProjScale / max(clip.w, 0.05), 1.0, uMaxPointPx);
    vBright *= 1.0 + 0.6 * max(0.0, 3.0 - gl_PointSize);
}
