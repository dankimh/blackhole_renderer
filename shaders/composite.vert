#version 460
// Fullscreen triangle. uv row 0 == top of image for both GL and Vulkan.
#ifdef VULKAN
#extension GL_GOOGLE_include_directive : enable
#endif
#include "common/params.glsl"
layout(location = 0) out vec2 vUv;
void main() {
    vec2 ndc = vec2((VERTEX_INDEX == 1) ? 3.0 : -1.0, (VERTEX_INDEX == 2) ? 3.0 : -1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
#ifdef VULKAN
    vUv = vec2((ndc.x + 1.0) * 0.5, (ndc.y + 1.0) * 0.5);
#else
    vUv = vec2((ndc.x + 1.0) * 0.5, (1.0 - ndc.y) * 0.5);
#endif
}
