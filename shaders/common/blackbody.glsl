#ifndef BH_BLACKBODY_GLSL
#define BH_BLACKBODY_GLSL
// Planckian locus approximation (Kang et al. 2002) -> CIE xy -> linear sRGB.
vec3 blackbody(float T) {
    T = clamp(T, 1000.0, 40000.0);
    float t = T, t2 = t * t, t3 = t2 * t;
    float x = (T <= 4000.0)
        ? -0.2661239e9 / t3 - 0.2343589e6 / t2 + 0.8776956e3 / t + 0.179910
        : -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390;
    float x2 = x * x, x3 = x2 * x;
    float y = (T <= 2222.0) ? -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683
            : (T <= 4000.0) ? -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867
                            :  3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
    float Y = 1.0;
    vec3 XYZ = vec3(Y / y * x, Y, Y / y * (1.0 - x - y));
    vec3 rgb = vec3(
         3.2404542 * XYZ.x - 1.5371385 * XYZ.y - 0.4985314 * XYZ.z,
        -0.9692660 * XYZ.x + 1.8760108 * XYZ.y + 0.0415560 * XYZ.z,
         0.0556434 * XYZ.x - 0.2040259 * XYZ.y + 1.0572252 * XYZ.z);
    return max(rgb, vec3(0.0));
}
#endif
