#ifndef BH_SKY_GLSL
#define BH_SKY_GLSL
#include "common/hash.glsl"
#include "common/blackbody.glsl"

// Point stars on a cubic lattice intersected with the unit sphere.
vec3 starLayer(vec3 d, float scale, float density, float sizeK, uint salt) {
    vec3 p = d * scale;
    ivec3 base = ivec3(floor(p));
    vec3 col = vec3(0.0);
    for (int z = -1; z <= 1; ++z)
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
        ivec3 c = base + ivec3(x, y, z);
        uvec3 uc = uvec3(c + 0x20000) ^ uvec3(salt);
        vec4 h = hash43(uc);
        if (h.w > density) continue;
        vec3 starDir = normalize(vec3(c) + h.xyz);
        float cosA = dot(d, starDir);
        float ang2 = max(0.0, 2.0 - 2.0 * cosA);         // ~ angle^2
        float mag = pow(hash13(uc ^ uvec3(77u)), 5.0);    // few bright, many faint
        float size = sizeK * (0.35 + 1.2 * mag) / (scale * scale);
        float I = exp(-ang2 / size) * (0.05 + 3.0 * mag);
        float temp = mix(2800.0, 14000.0, pow(hash13(uc ^ uvec3(991u)), 2.0));
        col += blackbody(temp) * I;
    }
    return col;
}

vec3 sky(vec3 d) {
    vec3 col = vec3(0.0);
    float dens = P.starDensity;
    col += starLayer(d,  45.0, dens * 0.06, 0.0025, 11u) * 1.6;   // few bright stars
    col += starLayer(d, 120.0, dens * 0.16, 0.0030, 23u) * 0.7;   // mid field
    col += starLayer(d, 320.0, dens * 0.35, 0.0035, 37u) * 0.22;  // faint dust of stars
    col *= P.starBrightness;

    // Galactic band: tilted great circle with fbm dust lanes.
    vec3 g = normalize(vec3(d.x, d.y * 0.62 + d.z * 0.78, -d.y * 0.78 + d.z * 0.62));
    float band = exp(-g.y * g.y * 18.0);
    float dust = fbm(g * 6.0 + vec3(3.1, 0.0, 1.7), 5);
    float lanes = fbm(g * 14.0 + vec3(0.0, 5.3, 2.2), 4);
    float glow = band * (0.35 + 0.65 * dust) * smoothstep(0.25, 0.75, lanes);
    vec3 tint = mix(vec3(0.55, 0.62, 1.0), vec3(1.0, 0.85, 0.7), dust);
    col += tint * glow * P.nebulaBrightness * 0.35;

    // Faint large-scale nebulosity
    float neb = fbm(d * 3.0 + vec3(9.0), 4);
    col += vec3(0.35, 0.2, 0.6) * pow(neb, 3.0) * P.nebulaBrightness * 0.15;
    return col;
}
#endif
