#version 330 core
/* ===========================================================================
   path.vert -- expands one instance into a glowing quad.

   An instance is either a path SEGMENT (trapped mode: A != B) or a placed
   PIECE (competitive mode: A == B, which the fragment shader draws as a disc).
   The quad is padded by uGlowR so the fragment shader has room for the glow.
   The whole scene can be slowly rotated by uRot for the cinematic camera.
   Hot-reloads on save.
   =========================================================================== */

layout(location = 0) in vec2  aCorner;   // u in {0,1} along, v in {-1,1} across
layout(location = 1) in vec2  aA;         // segment start / piece position
layout(location = 2) in vec2  aB;         // segment end   / piece position
layout(location = 3) in float aParam;     // path: 0..1 position;  comp: army #

uniform vec2  uCam;       // camera centre, board coords
uniform float uScale;     // board units -> NDC (2 / visible height)
uniform vec2  uAspect;    // (winH/winW, 1) -- keeps the board isotropic
uniform float uRot;       // scene rotation (radians)
uniform float uGlowR;     // glow radius, board units
uniform float uHead;      // revealed instance count (fractional)

out vec2       vWorld;
flat out vec2  vA;
flat out vec2  vB;
flat out float vParam;
flat out float vHot;      // ~1 on the freshly-revealed head, 0 behind

void main()
{
    float id = float(gl_InstanceID);
    vec2  A  = aA, B = aB;

    /* progressive reveal */
    float reveal = uHead - id;
    if (reveal <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);   // cull: outside clip volume
        vWorld = A; vA = A; vB = B; vParam = aParam; vHot = 0.0;
        return;
    }
    if (reveal < 1.0) B = mix(A, B, reveal);      // no-op for pieces (A == B)

    vec2  d   = B - A;
    float len = length(d);
    d = (len > 1e-6) ? d / len : vec2(1.0, 0.0);
    vec2  n = vec2(-d.y, d.x);

    vec2 base  = mix(A - d * uGlowR, B + d * uGlowR, aCorner.x);
    vec2 world = base + n * aCorner.y * uGlowR;

    vWorld = world;
    vA     = A;
    vB     = B;
    vParam = aParam;
    vHot   = clamp(2.0 - reveal, 0.0, 1.0);

    /* world -> rotated -> NDC */
    vec2  rel = world - uCam;
    float cs = cos(uRot), sn = sin(uRot);
    rel = vec2(cs * rel.x - sn * rel.y, sn * rel.x + cs * rel.y);
    gl_Position = vec4(rel * uScale * uAspect, 0.0, 1.0);
}
