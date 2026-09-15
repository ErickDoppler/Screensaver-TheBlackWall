#version 330 core
// Renders a figure's points flat into a small silhouette mask, in the plane
// of the wall. The wall shader samples it to know which of its own pixels
// were in contact with the figure.

layout(location = 0) in vec3 aPos;

uniform mat4  uModel;     // the figure's yaw, no translation
uniform float uHalfW;     // mask covers x in [-uHalfW, uHalfW]
uniform float uHeight;    // ... and y in [0, uHeight]
uniform float uPointPx;

void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    gl_Position = vec4(w.x / uHalfW, (w.y / uHeight) * 2.0 - 1.0, 0.0, 1.0);
    gl_PointSize = uPointPx;
}
