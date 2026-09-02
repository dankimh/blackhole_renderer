#version 460
#ifdef VULKAN
#extension GL_GOOGLE_include_directive : enable
#endif
#include "common/params.glsl"
layout(SET0 binding = BIND_SRC_SAMPLER)   uniform sampler2D hdrTex;
layout(SET0 binding = BIND_BLOOM_SAMPLER) uniform sampler2D bloomTex;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
void main() {
    vec3 hdr = texture(hdrTex, vUv).rgb;
    vec3 bloom = texture(bloomTex, vUv).rgb;
    vec3 c = (hdr + bloom * P.bloomStrength) * P.exposure;
    c = aces(c);
    // subtle vignette
    vec2 q = vUv * 2.0 - 1.0;
    c *= 1.0 - 0.18 * dot(q, q);
    outColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}
