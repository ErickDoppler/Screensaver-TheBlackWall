#version 330 core
// Dark floor that fogs into space with distance and picks up a soft glow at
// the foot of the wall, rippling in step with the wall's wave.

uniform vec3  uCamPos;
uniform vec3  uFloorColor;
uniform vec3  uSpaceColor;
uniform vec3  uWallColor;
uniform float uFogDist;
uniform float uWaveK;
uniform float uWavePhase;
uniform float uWaveAmp;

in vec3 vPos;
out vec4 frag;

void main() {
    // fade along the wall only, like the wall itself: stepping back keeps the
    // whole floor between camera and wall visible
    float dist = abs(vPos.x - uCamPos.x);
    float fog = 1.0 - smoothstep(uFogDist * 0.15, uFogDist, dist);
    // behind the camera (toward the city) the floor sinks into space slowly,
    // so there is no line where the floor ends
    float behind = vPos.z - uCamPos.z;
    fog *= 1.0 - smoothstep(40.0, 500.0, behind);
    // beyond the wall (z < 0) the floor dissolves into space quickly: seen
    // through the wall's pixels, its far end would otherwise draw a line
    fog *= 1.0 - smoothstep(0.0, 25.0, -vPos.z);
    // Glow falls off with distance from the wall plane (z = 0).
    float nearWall = exp(-abs(vPos.z) / 1.8);
    float ripple = 0.75 + 0.25 * sin(uWaveK * vPos.x + uWavePhase) * clamp(uWaveAmp, 0.0, 1.0);
    vec3 col = uFloorColor + uWallColor * 0.28 * nearWall * ripple;
    frag = vec4(mix(uSpaceColor, col, fog), 1.0);
}
