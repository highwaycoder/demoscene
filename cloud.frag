#version 330 core
/* ===========================================================================
   cloud.frag -- glowing disc for one placed piece.

   2D scenes blend additively; 3D scenes use GL_MAX so the sphere cannot blow
   out.  In 3D a piece fades by how far it sits BELOW the current growth
   surface (vFront): the outer rind stays bright, the deep interior empties
   out, so the middle never builds into a fuzzy white haze as the ball grows.
   Hot-reloads on save.
   =========================================================================== */

in  vec2  vLocal;
flat in float vArmy;
in  float vFront;         // uReveal - shell: 0 at the surface, large = deep in
in  float vDepth;         // view depth, 0 (far side) .. 1 (near side)

out vec4 fragColor;

uniform vec3  uTint[8];
uniform float uDim3D;     // 1 in 3D scenes, 0 in 2D

void main()
{
    float d = length(vLocal);
    if (d > 1.0) discard;                              /* circular piece */
    float disc = smoothstep(1.0, 0.45, d);
    float glow = exp(-d * d * 3.0);

    int  idx  = clamp(int(vArmy + 0.5), 0, 7);
    vec3 tint = uTint[idx];

    /* 3D: the outer ~one shell stays full bright, deeper shells fall off
       fast toward nothing -- the interior fades as the surface passes it.
       vFront is capped so the fade SATURATES: stepping the reveal cannot
       slide the whole sphere's brightness, only the outer band changes. */
    float vf        = min(vFront, 12.0);
    float surf      = mix(1.0, exp(-0.14 * max(vf - 1.0, 0.0)), uDim3D);
    /* 3D: the far hemisphere is dimmer, so the sphere reads with depth */
    float depthFade = mix(1.0, mix(0.35, 1.0, vDepth), uDim3D);
    /* a subtle, tinted lift on the freshly revealed shell-front */
    float front = clamp(1.0 - vFront, 0.0, 1.0);

    vec3 col = tint * glow * 0.50;
    col += mix(tint, vec3(1.0), 0.10) * disc * 1.35;
    col *= surf * depthFade;
    col += tint * front * disc * 0.30;

    fragColor = vec4(col, 1.0);
}
