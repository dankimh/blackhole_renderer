#ifndef BH_HASH_GLSL
#define BH_HASH_GLSL
// Integer hashing (lowbias32) - stable across GL/Vulkan/CUDA.
uint hashU(uint x) {
    x ^= x >> 16u; x *= 0x7feb352dU;
    x ^= x >> 15u; x *= 0x846ca68bU;
    x ^= x >> 16u; return x;
}
uint hashU3(uvec3 v) { return hashU(v.x ^ hashU(v.y ^ hashU(v.z))); }
float hashF(uint x) { return float(hashU(x) & 0x00ffffffu) / 16777216.0; }
float hash13(uvec3 v) { return hashF(hashU3(v)); }
vec2  hash23(uvec3 v) { uint h = hashU3(v); return vec2(hashF(h), hashF(h ^ 0x9e3779b9u)); }
vec3  hash33(uvec3 v) { uint h = hashU3(v); return vec3(hashF(h), hashF(h ^ 0x9e3779b9u), hashF(h ^ 0x85ebca6bu)); }
vec4  hash43(uvec3 v) { uint h = hashU3(v); return vec4(hashF(h), hashF(h ^ 0x9e3779b9u), hashF(h ^ 0x85ebca6bu), hashF(h ^ 0xc2b2ae35u)); }
uvec3 cellOf(vec3 p) { return uvec3(ivec3(floor(p)) + 0x20000); }

// 3D value noise + fbm
float vnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    uvec3 c = cellOf(i);
    float n000 = hash13(c), n100 = hash13(c + uvec3(1,0,0));
    float n010 = hash13(c + uvec3(0,1,0)), n110 = hash13(c + uvec3(1,1,0));
    float n001 = hash13(c + uvec3(0,0,1)), n101 = hash13(c + uvec3(1,0,1));
    float n011 = hash13(c + uvec3(0,1,1)), n111 = hash13(c + uvec3(1,1,1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float fbm(vec3 p, int octaves) {
    float a = 0.5, s = 0.0, n = 0.0;
    for (int i = 0; i < octaves; ++i) {
        s += a * vnoise(p); n += a; a *= 0.5; p = p * 2.03 + vec3(17.1, 9.7, 3.3);
    }
    return s / n;
}
#endif
