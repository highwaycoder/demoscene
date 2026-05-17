#version 330 core
/* ===========================================================================
   cloud.vert -- billboards a placed piece (2D or 3D competitive placement)
   into a glowing quad, seen through an orbiting orthographic camera.

   The reveal is by SHELL: a piece appears the instant uReveal reaches its
   shell index, so whole concentric shells pop in at once and the pattern
   grows as a disc (2D) or sphere (3D).

   Billboarding is intentional (cheap, no per-piece mesh); because each piece
   is a full circle it is rotationally symmetric, so it reads cleanly as the
   sphere turns.  Hot-reloads on save.
   =========================================================================== */

layout(location = 0) in vec2  aCorner;   // shared unit quad: x in {0,1}, y in {-1,1}
layout(location = 1) in vec3  aPos;      // piece position (z = 0 in 2D scenes)
layout(location = 2) in vec2  aMeta;     // x = army index, y = shell index

uniform vec3  uCenter;    // orbit pivot, world coords
uniform float uAz;        // camera azimuth
uniform float uEl;        // camera elevation (pi/2 = straight down => 2D)
uniform float uScale;     // world -> NDC
uniform vec2  uAspect;    // (winH/winW, 1)
uniform float uPointR;    // piece disc radius, world units
uniform float uReveal;    // current revealed shell (fractional)
uniform float uRadius;    // scene bounding radius (depth-fade reference)

out  vec2  vLocal;        // billboard corner, centred in [-1,1]^2
flat out float vArmy;
out  float vFront;        // uReveal - shell  (0 = just appeared, big = deep in)
out  float vDepth;        // view depth, 0 (far side) .. 1 (near side)

void main()
{
    /* the shared quad spans x in {0,1}; recentre it so the piece is a full
       disc, not a half-disc */
    vec2 c = vec2(aCorner.x * 2.0 - 1.0, aCorner.y);

    float shell = aMeta.y;
    if (shell > uReveal) {                       /* not revealed yet -> cull */
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vLocal = c; vArmy = aMeta.x; vFront = 0.0; vDepth = 0.5;
        return;
    }

    float ce = cos(uEl), se = sin(uEl), ca = cos(uAz), sa = sin(uAz);
    vec3 dir = vec3(ce * ca, ce * sa, se);       /* origin -> camera */
    vec3 rt  = vec3(-sa, ca, 0.0);               /* screen right */
    vec3 up  = cross(dir, rt);                   /* screen up */

    vec3  q      = aPos - uCenter;
    vec2  screen = vec2(dot(q, rt), dot(q, up));
    float depth  = dot(q, dir);

    vec2 p = screen + c * uPointR;               /* billboard the quad */

    vLocal = c;
    vArmy  = aMeta.x;
    vFront = uReveal - shell;
    vDepth = clamp(depth / max(uRadius, 1.0) * 0.5 + 0.5, 0.0, 1.0);

    gl_Position = vec4(p * uScale * uAspect, 0.0, 1.0);
}
