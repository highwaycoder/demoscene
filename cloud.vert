#version 330 core
/* ===========================================================================
   cloud.vert -- places a glowing piece for 2D/3D competitive placement.

   uTangent picks the piece style:
     0 = camera-facing billboard  -- cheap, rotationally symmetric, never
         foreshortens (the default).
     1 = surface-tangent tile     -- the quad lies flat on the sphere, so it
         turns with the surface and foreshortens toward the silhouette.

   Reveal is by SHELL: a piece appears once uReveal reaches its shell index.
   Hot-reloads on save.
   =========================================================================== */

layout(location = 0) in vec2  aCorner;   // shared unit quad: x in {0,1}, y in {-1,1}
layout(location = 1) in vec3  aPos;      // piece position (z = 0 in 2D scenes)
layout(location = 2) in vec2  aMeta;     // x = army index, y = shell index

uniform vec3  uCenter;
uniform float uAz;
uniform float uEl;
uniform float uScale;
uniform vec2  uAspect;
uniform float uPointR;
uniform float uReveal;
uniform float uRadius;
uniform float uTangent;   // 0 = billboard, 1 = surface-tangent tile

out  vec2  vLocal;
flat out float vArmy;
out  float vFront;
out  float vDepth;

void main()
{
    vec2 c = vec2(aCorner.x * 2.0 - 1.0, aCorner.y);   /* centred quad */

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

    vec3 rel = aPos - uCenter;
    vec3 q   = rel;
    if (uTangent > 0.5) {
        /* lay the quad flat on the sphere surface (its tangent plane) */
        vec3 nrm = (length(rel) > 1e-4) ? normalize(rel) : vec3(0.0, 0.0, 1.0);
        vec3 ref = (abs(nrm.z) < 0.9) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
        vec3 t1  = normalize(cross(ref, nrm));
        vec3 t2  = cross(nrm, t1);
        q = rel + (c.x * t1 + c.y * t2) * uPointR;
    }

    vec2  screen = vec2(dot(q, rt), dot(q, up));
    float depth  = dot(q, dir);
    if (uTangent <= 0.5) screen += c * uPointR;  /* camera-facing billboard */

    vLocal = c;
    vArmy  = aMeta.x;
    vFront = uReveal - shell;
    vDepth = clamp(depth / max(uRadius, 1.0) * 0.5 + 0.5, 0.0, 1.0);

    gl_Position = vec4(screen * uScale * uAspect, 0.0, 1.0);
}
