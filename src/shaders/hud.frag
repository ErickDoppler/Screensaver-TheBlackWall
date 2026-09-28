#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 frag;
uniform sampler2D uFont;
void main() {
    float a = vUV.x < 0.0 ? 1.0 : texture(uFont, vUV).r;
    frag = vec4(vColor.rgb, vColor.a * a);
}
