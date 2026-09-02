#ifndef BH_DISK_GLSL
#define BH_DISK_GLSL
#include "common/hash.glsl"
#include "common/blackbody.glsl"

// Emission of a thin, differentially rotating accretion disk at world point `hit`
// (y ~ 0 plane).  `rayDir` is the marching direction (camera -> scene); the photon
// travels in -rayDir.  Returns rgb radiance and writes opacity to `alpha`.
vec3 diskEmission(vec3 hit, vec3 rayDir, out float alpha) {
    float rw = length(hit.xz);
    float r  = rw / P.rs;                             // rs units
    float rin = P.diskInner, rout = P.diskOuter;
    if (r < rin || r > rout) { alpha = 0.0; return vec3(0.0); }

    float phi = atan(hit.z, hit.x);
    float omega = P.diskSpeed * pow(r, -1.5) * P.diskRotation;   // Keplerian
    float u = phi - omega * P.time;

    // Streaky turbulent texture in (log r, phi) space, advected with the flow.
    float lr = log(r) * P.diskNoiseScale;
    vec3 q = vec3(lr * 4.0, u * 3.0 + lr * 1.5, P.time * 0.05 + P.seed);
    float n1 = fbm(q, 5);
    float n2 = fbm(vec3(lr * 12.0, u * 1.2, 7.0 + P.seed), 4);
    float streaks = smoothstep(0.30, 0.80, n1) * (0.55 + 0.45 * n2);

    // Radial profile: edges fade, Shakura-Sunyaev like temperature.
    float innerFade = smoothstep(rin, rin * 1.25, r);
    float outerFade = 1.0 - smoothstep(rout * 0.55, rout, r);
    float rel = max(1.0 - sqrt(rin / r), 0.0);
    float rr = r / (rin * 1.36);                      // T peaks near 1.36 r_in
    float T = P.diskTemperature * pow(rr, -0.75) * pow(rel, 0.25) * 1.7;

    // Relativistic factors. Disk velocity: beta = sqrt(M/r), M = rs/2 -> sqrt(0.5/r)
    float beta = min(sqrt(0.5 / r), 0.6);
    vec3 tangent = normalize(vec3(-hit.z, 0.0, hit.x)) * P.diskRotation;
    vec3 photonDir = -normalize(rayDir);
    float gamma = inversesqrt(1.0 - beta * beta);
    float gD = 1.0 / (gamma * (1.0 - beta * dot(tangent, photonDir)));
    float gG = sqrt(max(1.0 - 1.0 / r, 0.0));
    float g = mix(1.0, gD, P.dopplerStrength) * mix(1.0, gG, P.redshiftStrength);

    float Tobs = T * g;
    float intensity = pow(max(Tobs / P.diskTemperature, 0.0), 2.0) * g * g;   // ~ g^3 beaming
    vec3 color = blackbody(Tobs) * intensity;

    float density = streaks * innerFade * outerFade;
    alpha = clamp(density * P.diskOpacity, 0.0, 1.0);
    return color * density * P.diskBrightness * 0.28;
}
#endif
