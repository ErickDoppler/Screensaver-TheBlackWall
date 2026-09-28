#version 330 core
// HUD: text and symbology, in pixels.
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;       // u < 0: solid colour
layout(location = 2) in vec4 aColor;
uniform vec2 uRes;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    vec2 p = aPos / uRes * 2.0 - 1.0;
    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
}
