#version 330 core
// Shapes each point sprite: square, soft round, or a glyph from the atlas.
// Shared by the wall, floor debris, city horizon, figures and embers.
// Output is premultiplied for additive blending (GL_ONE, GL_ONE).

uniform int       uPixelType;   // 0 square, 1 round, 2 matrix glyph
uniform sampler2D uGlyphs;      // 16 glyphs of 8x8, laid out horizontally
uniform float     uLightBg;     // 1 on a light space color: no over-brightening (it would whiten)
uniform vec3      uScarColor;   // the colour a touched wall pixel takes
uniform float     uTime;

flat in float vGlyph;
in float vAlpha;
in float vBright;
in vec3  vColor;
in float vScar;                 // 1 where this wall pixel was in contact with a figure
flat in float vSize;            // sprite size in pixels

out vec4 frag;

float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec2 uv = gl_PointCoord;
    // One shaping rule for every size. The edge is softened by about one
    // screen pixel, so a big sprite keeps a crisp edge and a small one melts
    // into a smooth blob, continuously in between: no size at which the wall
    // changes character, and no hard edge small enough to snap to whole
    // pixels and beat the grid into moire.
    float aa = clamp(1.1 / max(vSize, 1.0), 0.02, 0.5);
    float m;
    if (uPixelType == 1) {
        float r = length(uv - 0.5) * 2.0;
        m = 1.0 - smoothstep(1.0 - 2.0 * aa, 1.0, r);
    } else {
        vec2 d = abs(uv - 0.5);
        m = (1.0 - smoothstep(0.5 - aa, 0.5, d.x)) * (1.0 - smoothstep(0.5 - aa, 0.5, d.y));
        if (uPixelType == 2) {
            // the glyph only shows once the sprite is big enough to read it
            float g = step(0.5, texture(uGlyphs, vec2((vGlyph + uv.x) / 16.0, uv.y)).r);
            m *= mix(1.0, g, smoothstep(3.0, 7.0, vSize));
        }
    }
    if (m < 0.004) discard;
    // softening shrinks the lit area; give the light back so brightness does
    // not drift with distance
    float a = vAlpha * m / max((1.0 - aa) * (1.0 - aa), 0.05);

    vec3 col = vColor;
    float b = uLightBg > 0.5 ? min(vBright, 1.0) : vBright;
    if (vScar > 0.5) {
        // A touched pixel does not take a flat colour: it shows snow, the way
        // a television shows a dead channel. The grain is one screen pixel,
        // the finest the display can give, re-rolled 45 times a second, and it
        // runs from near black to far brighter than the wall around it.
        float n = hash13(vec3(floor(gl_FragCoord.xy), floor(uTime * 45.0)));
        col = uScarColor;
        b *= 0.08 + 3.2 * n * n;
    }
    frag = vec4(col * b * a, a);
}
