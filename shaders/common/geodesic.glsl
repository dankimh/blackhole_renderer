#ifndef BH_GEODESIC_GLSL
#define BH_GEODESIC_GLSL
#include "common/sky.glsl"
#include "common/disk.glsl"

// Null geodesic in Schwarzschild spacetime, Cartesian form:
//   d2x/dl2 = -(3/2) rs h^2 x / r^5,  h = |x cross v| (conserved).
// Integrated with velocity Verlet and an adaptive step. Thin disk on y = 0.
vec3 traceGeodesic(vec3 ro, vec3 rd) {
    vec3 p = ro, v = rd;
    vec3 hv = cross(p, v);
    float h2 = dot(hv, hv);
    float rs = P.rs;
    vec3 col = vec3(0.0);
    float T = 1.0;                                   // transmittance

    float r2 = dot(p, p), r = sqrt(r2);
    vec3 a = -1.5 * rs * h2 * p / (r2 * r2 * r);

    for (int i = 0; i < P.maxSteps; ++i) {
        // Adaptive step: fine near the photon sphere / horizon, coarse far away.
        float dl = P.stepSize * clamp((r - rs) * 0.30, 0.03, 3.0);

        vec3 pPrev = p;
        v += a * (0.5 * dl);
        p += v * dl;
        r2 = dot(p, p); r = sqrt(r2);
        a = -1.5 * rs * h2 * p / (r2 * r2 * r);
        v += a * (0.5 * dl);

        // Disk crossing (y = 0 plane)
        if ((pPrev.y > 0.0) != (p.y > 0.0)) {
            float t = pPrev.y / (pPrev.y - p.y);
            vec3 hit = mix(pPrev, p, t);
            float alpha;
            vec3 e = diskEmission(hit, v, alpha);
            col += T * e;
            T *= (1.0 - alpha);
            if (T < 0.02) break;
        }

        if (r < rs * 1.01) { T = 0.0; break; }        // captured
        if (r2 > P.farRadius * P.farRadius && dot(p, v) > 0.0) {
            col += T * sky(normalize(v));
            T = 0.0; break;
        }
    }
    // Ray budget exhausted while still inside the domain: use its current direction.
    if (T > 0.0) col += T * sky(normalize(v)) * smoothstep(P.rs * 3.0, P.farRadius * 0.5, r);
    return col;
}
#endif
