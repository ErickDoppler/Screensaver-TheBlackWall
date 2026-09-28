#version 330 core
in vec2 vUV;
out vec4 frag;
uniform sampler2D uTex;
void main() { frag = texture(uTex, vUV); }
