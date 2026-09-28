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
    // The menu texture is painted top-down (hud.c's y-down pixel convention),
    // which ends up stored with texel v=0 at the BOTTOM of the image (OpenGL's
    // usual row order) - so the quad's top (p.y=1) must sample v=1, not v=0.
    vUV = vec2(p.x * 0.5 + 0.5, 0.5 + p.y * 0.5);
}
