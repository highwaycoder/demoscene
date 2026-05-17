#version 330 core
/* ===========================================================================
   path.frag -- neon glow for a path segment or a placed piece.

   uMode 0: trapped path  -- colour is a cyclic palette running along the path,
            with an optional comet (uTrail) endlessly retracing the route.
   uMode 1: competitive   -- colour is the placing army's tint (uTint[army]).

   Rendered with additive blending, so crossings and dense regions bloom.
   Hot-reloads on save.
   =========================================================================== */

in vec2       vWorld;
flat in vec2  vA;
flat in vec2  vB;
flat in float vParam;
flat in float vHot;

out vec4 fragColor;

uniform float uTime;
uniform float uGlowR;     // glow radius   (board units)
uniform float uCoreW;     // core halfwidth (board units)
uniform int   uPalette;
uniform int   uMode;      // 0 = trapped path, 1 = competitive armies
uniform vec3  uTint[8];   // per-army colours
uniform float uTrail;     // <0 = off; else 0..1 comet position along the path

const float TAU = 6.28318530718;

float sdSegment(vec2 p, vec2 a, vec2 b)
{
    vec2  pa = p - a, ba = b - a;
    float h  = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-9), 0.0, 1.0);
    return length(pa - ba * h);
}

vec3 palette(float t, int idx)
{
    vec3 a = vec3(0.5), b = vec3(0.5), c = vec3(1.0), d = vec3(0.0);
    if      (idx == 0) d = vec3(0.00, 0.33, 0.67);
    else if (idx == 1) d = vec3(0.00, 0.10, 0.20);
    else if (idx == 2){ c = vec3(1.0, 1.0, 0.5); d = vec3(0.80, 0.90, 0.30); }
    else if (idx == 3){ a = vec3(0.20, 0.20, 0.30); b = vec3(0.70, 0.50, 0.40);
                        d = vec3(0.00, 0.15, 0.30); }
    else              { c = vec3(2.0, 1.0, 0.0); d = vec3(0.50, 0.20, 0.25); }
    return a + b * cos(TAU * (c * t + d));
}

void main()
{
    float dist = sdSegment(vWorld, vA, vB);
    float core = smoothstep(uCoreW, uCoreW * 0.35, dist);
    float glow = exp(-dist * dist / (uGlowR * uGlowR) * 9.0);

    vec3 hue;
    if (uMode == 1) {
        int idx = clamp(int(vParam + 0.5), 0, 7);
        hue = uTint[idx];
    } else {
        hue = palette(vParam * 2.0 - uTime * 0.08, uPalette);
    }

    /* trapped paths get a white-hot core; army pieces stay saturated so the
       territory colours read clearly even where glows pile up */
    float whiteMix = (uMode == 1) ? 0.12 : 0.55;
    float coreAmp  = (uMode == 1) ? 1.70 : 1.30;
    vec3 col  = hue * glow * 0.60;
    col      += mix(hue, vec3(1.0), whiteMix) * core * coreAmp;
    col      += vec3(1.0) * vHot * (core + glow * 0.4) * 0.9;

    /* comet: a bright pulse endlessly retracing the trapped-knight path */
    if (uMode == 0 && uTrail >= 0.0) {
        float td = abs(vParam - uTrail);
        td = min(td, 1.0 - td);                       /* wrap around the loop */
        float tr = exp(-td * td / (0.013 * 0.013));
        col += mix(hue, vec3(1.0), 0.5) * tr * (core * 2.0 + glow * 0.8);
    }

    fragColor = vec4(col, 1.0);                        /* additive: GL_ONE,ONE */
}
