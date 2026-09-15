#version 330 core
// One big quad on y = 0, centred under the camera, drawn as a triangle strip
// from gl_VertexID. Extends behind the wall so the horizon never shows a gap.

uniform mat4  uViewProj;
uniform float uCamX;       // camera x, origin-relative
uniform float uHalfWidth;  // half extent along x
uniform float uZNear;      // toward the camera (positive z)
uniform float uZFar;       // behind the wall (negative z)

out vec3 vPos;

void main() {
    // 0:(-1,near) 1:(1,near) 2:(-1,far) 3:(1,far)
    float sx = (gl_VertexID & 1) == 0 ? -1.0 : 1.0;
    float z  = (gl_VertexID & 2) == 0 ? uZNear : uZFar;
    vPos = vec3(uCamX + sx * uHalfWidth, 0.0, z);
    gl_Position = uViewProj * vec4(vPos, 1.0);
}
