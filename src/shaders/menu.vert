#version 330 core
// The VR menu panel: a single textured quad hanging in the world, its four
// corners generated from gl_VertexID so it needs no vertex buffer.
uniform mat4 uViewProj;
uniform vec3 uPos, uRight, uUp;
out vec2 vUV;
void main() {
    vec2 c[6] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
    vec2 p = c[gl_VertexID];
    vec3 world = uPos + uRight * p.x + uUp * p.y;
    gl_Position = uViewProj * vec4(world, 1.0);
    vUV = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
}
