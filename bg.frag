#version 330 core
/* ===========================================================================
   bg.frag -- a dark, slowly drifting backdrop so the neon path has somewhere
   to glow against. Deliberately dim. Hot-reloads on save.
   =========================================================================== */

out vec4 fragColor;

uniform vec2  uRes;
uniform float uTime;

void main()
{
    vec2  uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    float r  = length(uv);

    /* lazy interference of a few sine waves -- a faint nebula */
    float n = 0.0;
    n += sin(uv.x * 3.1 + uTime * 0.15);
    n += sin(uv.y * 2.7 - uTime * 0.11);
    n += sin((uv.x + uv.y) * 2.3 + uTime * 0.09);
    n = n / 3.0;

    vec3 col = vec3(0.025, 0.030, 0.055);
    col += vec3(0.045, 0.025, 0.075) * (0.5 + 0.5 * n);
    col *= 1.0 - 0.55 * r;                       // vignette toward the edges

    fragColor = vec4(max(col, 0.0), 1.0);
}
