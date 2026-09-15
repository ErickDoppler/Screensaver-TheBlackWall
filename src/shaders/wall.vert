#version 330 core
// The wall is a regular grid of glowing pixels generated entirely from
// gl_VertexID - no vertex buffers. Displacement (wave + spikes) is computed
// here per pixel, so the CPU only sends a handful of uniforms per frame.

uniform mat4  uViewProj;
uniform vec3  uCamPos;        // camera position in origin-relative space
uniform int   uCols;
uniform int   uCellBase;      // world column index of column 0
uniform float uSpacing;
uniform float uGridX0;        // x of column 0 (origin-relative)
uniform float uPointWorld;    // pixel size in world units
uniform float uProjScale;     // viewport_h * 0.5 * proj[1][1]
uniform vec2  uViewport;      // viewport size in pixels
uniform float uMaxPointPx;
uniform float uTime;
uniform float uWaveK;
uniform float uWaveAmp;
uniform float uWavePhase;
uniform float uWavePhase2;    // harmonic phase, kept continuous by the sim
uniform float uFadeHeight;    // e-folding height of the upward fade
uniform float uWallHeight;    // top of the generated geometry (fade reaches zero there)
uniform float uFogDist;
uniform float uMirror;        // 1 = wall, -1 = reflection in the floor
uniform float uAlphaMul;
uniform float uShimmer;       // 0 = calm twinkle only, 1 = full random flicker
uniform int   uSpikeCount;
uniform vec4  uSpikes[64];    // x_rel, y, amp*envelope, packed(width, pointy)
uniform vec3  uColor;
uniform vec4  uSurge;         // x_rel, amplitude, half_width, roll phase: the giant wave
uniform float uSkipHalf;      // far-detail pass: leave out columns this close to the camera
uniform float uDotComp;       // lift for the far field when the dots are sparse
// The ghost's imprint. Rather than pasting anything onto the wall, the wall's
// own pixels that were in contact with the figure take its colour.
uniform sampler2D uScarMask;  // the figure's silhouette, in the plane of the wall
uniform vec4  uScarRect;      // x_rel centre, half width, height, the figure's z
uniform vec2  uScarState;     // x: 1 whole .. 0 gone, y: 1 = every pixel already touched
uniform vec3  uScarColor;

flat out float vGlyph;
out float vAlpha;
out float vBright;
out vec3  vColor;
out float vScar;   // 1 where the fragment should fill this pixel with static
flat out float vSize;  // sprite size in pixels; small ones get a soft edge

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    int i = gl_VertexID % uCols;
    int j = gl_VertexID / uCols;
    float x = uGridX0 + float(i) * uSpacing;
    float y = (float(j) + 0.5) * uSpacing;
    if (uSkipHalf > 0.0 && abs(x - uCamPos.x) < uSkipHalf) {
        // covered by the fine near grid
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 1.0;
        vGlyph = 0.0; vAlpha = 0.0; vBright = 0.0; vColor = uColor; vScar = 0.0; vSize = 1.0;
        return;
    }
    // Per-pixel identity must follow the *world* cell, not the column slot:
    // as the camera slides the grid window shifts by whole columns, and
    // hashing on i would make every pixel change its twinkle/glyph each time.
    vec2 cell = vec2(float((uCellBase + i) & 0xFFFF), float(j));

    // Visibility first, before any expensive displacement work. Distance fade
    // is measured along the wall, so stepping back from it does not dim it;
    // the wall is fully dark uFogDist units along from the camera.
    float fog = 1.0 - smoothstep(uFogDist * 0.15, uFogDist, abs(x - uCamPos.x));
    // Upward fade with a faint long tail, so the glow thins into the void
    // rather than ending. The roll-off below takes it smoothly to exactly
    // zero: a hard cull threshold would draw a visible line where far points
    // stack several per screen pixel.
    float hfade = exp(-y / uFadeHeight) + 0.08 * exp(-y / (3.0 * uFadeHeight));
    // whatever the tail, the geometry's top is reached at exactly zero
    hfade *= 1.0 - smoothstep(0.55 * uWallHeight, uWallHeight, y);
    if (uMirror < 0.0) hfade *= exp(-y / 1.5);   // the reflection only shows the foot
    float vis = hfade * fog * uAlphaMul;
    hfade *= smoothstep(0.00015, 0.0015, vis);
    if (vis < 0.00015) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 1.0;
        vGlyph = 0.0; vAlpha = 0.0; vBright = 0.0; vColor = uColor; vScar = 0.0; vSize = 1.0;
        return;
    }

    // Calm breathing wave; a faint second harmonic keeps it organic.
    float disp = uWaveAmp * sin(uWaveK * x + uWavePhase + 0.25 * y)
               + 0.3 * uWaveAmp * sin(2.3 * uWaveK * x + uWavePhase2 + 1.3);

    float spikeGlow = 0.0;
    for (int s = 0; s < uSpikeCount; ++s) {
        vec4 sp = uSpikes[s];
        float packedW = sp.w;
        float pointy = floor(packedW / 100.0) / 99.0;
        float width  = packedW - floor(packedW / 100.0) * 100.0;
        vec2 d = vec2(x - sp.x, y - sp.y);
        float r = length(d) / width;
        float shape = mix(2.0, 1.0, pointy);        // 2 = gaussian bulge, 1 = pointed cusp
        float g = exp(-pow(r, shape));
        disp += sp.z * g;
        spikeGlow += abs(sp.z) * g;
    }

    // The giant wave: a very broad swell of the whole visible wall toward the
    // figure. Its top leans further out than its base (a breaking-wave curl)
    // and a slow ripple rolls across it, so it reads as inevitable motion
    // rather than a targeted strike.
    float surgeGlow = 0.0;
    if (uSurge.y > 0.001) {
        float px = (x - uSurge.x) / uSurge.z;
        float profile = exp(-px * px * 0.7);
        // The crest leans out past the foot by a fixed distance, not by a
        // fraction of the reach: a distant, huge surge would otherwise leave
        // its foot tens of metres behind its top and take figures head first.
        float lean = min(0.35 * uSurge.y, 8.0) * pow(clamp(y / 9.0, 0.0, 1.0), 1.2);
        // the rolling ripple is relative to a 25-unit-wide swell; a far, huge
        // surge would otherwise pleat into pillars
        float rollAmp = 0.10 * min(1.0, 25.0 / uSurge.z);
        float roll = 1.0 + rollAmp * sin(0.35 * (x - uSurge.x) - uSurge.w) * clamp(y / 3.0, 0.0, 1.0);
        float s = (uSurge.y + lean) * profile * roll;
        disp += s;
        surgeGlow = s / max(uSurge.y, 0.1) * 0.6;
    }

    vec3 pos = vec3(x, y * uMirror, disp);
    float ph = hash12(cell) * 6.2831;
    float twinkle = 0.82 + 0.18 * sin(uTime * 1.5 + ph);
    // Optional shimmer: each pixel re-rolls its brightness ~8 times a second,
    // the "tearing off" look. Blended in by the Pixel shimmer slider.
    if (uShimmer > 0.0) {
        float flick = hash12(cell + floor(uTime * 8.0 + hash12(cell.yx) * 7.0));
        twinkle = mix(twinkle, 0.35 + 0.75 * flick, uShimmer);
    }

    // Points at arm's length fade out. Without this, the moment the wall
    // sweeps over the camera every nearby dot stacks and the screen turns to
    // a flat white sheet instead of a wall passing through us.
    vAlpha = hfade * fog * twinkle * uAlphaMul * smoothstep(0.3, 3.0, distance(pos, uCamPos));
    vColor = uColor;
    vScar = 0.0;
    if (vAlpha < 0.00005) {
        // Invisible: push out of the clip volume so the rasterizer skips it.
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 1.0;
        vGlyph = 0.0;
        vBright = 0.0;
        vScar = 0.0;
        vSize = 1.0;
        return;
    }

    vec4 clip = uViewProj * vec4(pos, 1.0);
    if (clip.w <= 0.05) {
        // behind the camera: some drivers do not clip points there and would
        // draw them mirrored
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 1.0;
        vAlpha = 0.0; vBright = 0.0; vGlyph = 0.0; vScar = 0.0; vSize = 1.0;
        return;
    }
    gl_Position = clip;
    // Far along the wall the grid cells shrink below a screen pixel and many
    // points stack into one pixel: additive blending would burn them white
    // (a hot streak toward the vanishing point, and a bright tip where the
    // grid ends). Scale each point's weight by its cell's area in pixels so a
    // pixel never receives more than one point's worth.
    // The cell's true on-screen footprint (foreshortening included): project
    // the neighbours one column and one row over.
    vec4 cx = uViewProj * vec4(pos + vec3(uSpacing, 0.0, 0.0), 1.0);
    vec4 cy = uViewProj * vec4(pos + vec3(0.0, uSpacing, 0.0), 1.0);
    vec2 ndc0 = clip.xy / clip.w;
    vec2 px1 = (cx.xy / max(cx.w, 0.05) - ndc0) * 0.5 * uViewport;
    vec2 px2 = (cy.xy / max(cy.w, 0.05) - ndc0) * 0.5 * uViewport;
    float len1 = length(px1), len2 = length(px2);
    float minDim = min(len1, len2), maxDim = max(len1, len2);
    float wantedPx = uPointWorld * uProjScale / max(clip.w, 0.05);

    // Anti-moire. Once a cell is too small for the screen to resolve, a grid
    // of hard little sprites beats against the pixel grid and the wall breaks
    // into interference patterns, worst at grazing angles where the columns
    // compress. The cure is not to dither but to stop drawing a grid at all
    // out there: grow each sprite until it covers its own cell, so neighbours
    // merge into one continuous sheet of light. Near the camera the cells are
    // resolvable, nothing changes, and the panel stays crisp.
    float merge = 1.0 - smoothstep(1.5, 4.5, minDim);
    // A hard-edged sprite smaller than about 2 px snaps to whole pixels as it
    // moves, and a regular grid of those beats into arcs. Never go below 2 px;
    // the fragment shader gives anything that small a soft edge so its
    // brightness varies smoothly with its sub-pixel position instead.
    float sizePx = max(wantedPx, mix(wantedPx, clamp(maxDim, 1.0, 6.0), merge));
    sizePx = clamp(sizePx, 2.0, uMaxPointPx);
    gl_PointSize = sizePx;
    vSize = sizePx;
    // Spreading the same light over a bigger sprite has to dim it by exactly
    // the area ratio, so the wall's brightness does not change with distance.
    // Where the dots are sparse that leaves the far wall nearly black, since
    // it averages what the eye reads near the camera as bright points on
    // black; uDotComp lifts it back, and only there, so a resolved dot up
    // close is never pushed past full.
    float unresolved = 1.0 - smoothstep(0.7, 2.0, wantedPx);
    vAlpha *= (wantedPx * wantedPx) / (sizePx * sizePx) * mix(1.0, uDotComp, unresolved);
    vBright = 1.0 + 0.5 * min(spikeGlow + abs(disp) * 0.5, 3.0) + surgeGlow;

    // --- the ghost's imprint --------------------------------------------
    // A pixel counts as touched once its own displacement has carried it out
    // to where the figure stood. Touched pixels take the figure's colour and
    // then go out one by one, each with a spark of its own.
    if (uMirror > 0.0 && uScarRect.z > 0.0) {
        vec2 uv = vec2((x - uScarRect.x) / (2.0 * uScarRect.y) + 0.5, y / uScarRect.z);
        if (uv.x > 0.0 && uv.x < 1.0 && uv.y > 0.0 && uv.y < 1.0
            && textureLod(uScarMask, uv, 0.0).r > 0.4) {
            float touched = max(step(uScarRect.w, disp), uScarState.y);
            float death = hash12(cell + 31.7);
            float progress = 1.0 - uScarState.x;
            float spark = (progress > 0.0 && progress < death && death - progress < 0.03) ? 1.0 : 0.0;
            float on = touched * step(progress, death);
            vScar = on;                       // the fragment fills it with static
            vBright += on * (0.4 + 5.0 * spark);
            vAlpha = min(vAlpha * (1.0 + on * 2.5), 1.0);
        }
    }

    // Matrix mode: each cell flips through glyphs at its own random cadence.
    vGlyph = floor(hash12(cell + floor(uTime * 3.0 + hash12(cell.yx) * 10.0)) * 16.0);
}
