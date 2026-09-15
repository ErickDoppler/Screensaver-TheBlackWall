#version 330 core
// Signal loss at the far end of the corridor. Applied to the finished frame.
//   uSparkle 0..1  random static sparkles (wall / city / space colors)
//   uDistort 0..1  broken-video distortion: torn rows, displaced blocks, channel split
//   uTvOff   0..1  TV-off collapse; at 1 the picture is gone and CCTV lines remain

uniform sampler2D uTex;
uniform vec2  uResolution;
uniform float uTime;
uniform float uSparkle;
uniform float uDistort;
uniform float uTvOff;
uniform vec3  uWallColor;
uniform vec3  uCityColor;
uniform vec3  uSpaceColor;

in vec2 vUV;
out vec4 frag;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec2 uv = vUV;
    float t = uTime;

    // ---- TV off: squeeze to a bright line, then the line shrinks to a dot ----
    if (uTvOff > 0.0) {
        float p = uTvOff;
        float sy = max(1.0 - smoothstep(0.0, 0.65, p) * 0.995, 0.005);   // vertical squeeze
        float sx = max(1.0 - smoothstep(0.6, 1.0, p), 0.0);              // then horizontal
        vec2 c = uv - 0.5;
        vec2 q = vec2(c.x / max(sx, 0.001), c.y / sy) + 0.5;
        bool inside = abs(c.y) < 0.5 * sy && abs(c.x) < 0.5 * sx;
        vec3 col = vec3(0.0);
        if (inside && p < 1.0) {
            col = texture(uTex, q).rgb;
            // the compressed picture brightens like a hot phosphor line
            col = col * (1.0 + 3.0 * smoothstep(0.3, 0.7, p)) + vec3(0.6) * smoothstep(0.5, 0.75, p);
        }
        // CCTV lines on the dead screen: 5-10 px, wall color, barely there, drifting
        float lineY = gl_FragCoord.y + t * 6.0;
        float period = 44.0;
        float thick = 5.0 + 5.0 * hash12(vec2(floor(lineY / period), 3.0));
        float onLine = step(mod(lineY, period), thick);
        vec3 cctv = uWallColor * 0.045 * onLine * (0.7 + 0.3 * sin(t * 0.8 + gl_FragCoord.y * 0.01));
        col += cctv * smoothstep(0.7, 1.0, p);
        frag = vec4(col, 1.0);
        return;
    }

    // ---- broken video ----
    if (uDistort > 0.0) {
        float d = uDistort;
        // torn rows: bands of 8-40 px shifted sideways for a few frames
        float band = floor(gl_FragCoord.y / (8.0 + 32.0 * hash12(vec2(floor(t * 7.0), 1.0))));
        float bh = hash12(vec2(band, floor(t * 9.0)));
        if (bh < 0.35 * d) uv.x += (hash12(vec2(band, floor(t * 9.0) + 7.0)) - 0.5) * 0.25 * d;
        // displaced macroblocks: a block shows the wrong part of the picture
        vec2 blk = floor(gl_FragCoord.xy / 28.0);
        float bk = hash12(blk + floor(t * 4.0));
        if (bk < 0.25 * d) uv += (vec2(hash12(blk + 11.0), hash12(blk + 23.0)) - 0.5) * 0.3 * d;
        uv = clamp(uv, 0.0, 1.0);
        // channel split
        float split = 0.006 * d * (0.5 + 0.5 * sin(t * 13.0));
        vec3 col;
        col.r = texture(uTex, uv + vec2(split, 0.0)).r;
        col.g = texture(uTex, uv).g;
        col.b = texture(uTex, uv - vec2(split, 0.0)).b;
        // frozen / posterised patches
        if (hash12(blk + floor(t * 2.0) + 41.0) < 0.15 * d) col = floor(col * 4.0) / 4.0 * 1.3;
        frag = vec4(col, 1.0);
    } else {
        frag = vec4(texture(uTex, uv).rgb, 1.0);
    }

    // ---- static sparkles ----
    if (uSparkle > 0.0) {
        vec2 cell = floor(gl_FragCoord.xy / 3.0);
        float h = hash12(cell + floor(t * 30.0) * 0.37);
        float density = 0.002 + 0.06 * uSparkle * uSparkle;
        if (h < density) {
            float which = hash12(cell * 1.7 + 9.0);
            vec3 c = which < 0.5 ? uWallColor : (which < 0.8 ? uCityColor : uSpaceColor * 3.0 + 0.3);
            float bright = 0.4 + 1.6 * uSparkle * hash12(cell + 5.0);
            frag.rgb = mix(frag.rgb, c * bright, 0.85);
        }
    }
}
