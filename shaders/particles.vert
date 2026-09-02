#version 460
#ifdef VULKAN
#extension GL_GOOGLE_include_directive : enable
#endif
#include "common/params.glsl"
#include "common/blackbody.glsl"

layout(std430, SET0 binding = BIND_PARTICLES) readonly buffer ParticleBuffer { Particle particles[]; };
layout(location = 0) out vec4 vColor;

void main() {
    Particle p = particles[VERTEX_INDEX];
    if (p.posLife.w <= 0.0) { gl_Position = vec4(0.0, 0.0, -3.0, 1.0); gl_PointSize = 0.0; vColor = vec4(0.0); return; }

    vec3 right = P.camToWorld[0].xyz, up = P.camToWorld[1].xyz, fwd = P.camToWorld[2].xyz;
    vec3 rel = p.posLife.xyz - P.camPos.xyz;
    vec3 cs = vec3(dot(rel, right), dot(rel, up), dot(rel, fwd));
    if (cs.z < 0.2) { gl_Position = vec4(0.0, 0.0, -3.0, 1.0); gl_PointSize = 0.0; vColor = vec4(0.0); return; }

    // Pseudo gravitational lensing: deflect the apparent position away from the hole
    // by alpha ~ 2 rs / b, weighted by how far behind the lens plane the particle is.
    vec3 bh = -P.camPos.xyz;
    vec3 bcs = vec3(dot(bh, right), dot(bh, up), dot(bh, fwd));
    vec2 ang = cs.xy / cs.z;
    vec2 bAng = bcs.xy / bcs.z;
    vec2 dAng = ang - bAng;
    float b = max(length(dAng) * bcs.z, P.rs * 1.2);
    float behind = clamp((cs.z - bcs.z) / cs.z, 0.0, 1.0);
    float alpha = 2.0 * P.rs / b;
    ang += normalize(dAng + vec2(1e-5, 0.0)) * alpha * behind * P.lensStrength;

    float aspect = P.camPos.w;
    vec2 ndc = vec2(ang.x / (P.tanHalfFov * aspect), ang.y / P.tanHalfFov);
#ifdef VULKAN
    ndc.y = -ndc.y;
#endif
    gl_Position = vec4(ndc, 0.5, 1.0);

    float r = length(p.posLife.xyz) / P.rs;
    float heat = 1.0 - smoothstep(1.3, 8.0, r);      // 0 far away .. 1 near the horizon
    // ~1-4 px at 720p for particleSize = 2, scaling with resolution and proximity.
    float size = P.particleSize * (P.resolution.y / 720.0) * (8.0 / cs.z) * (0.5 + 0.9 * p.velSeed.w) * (1.0 + 0.8 * heat);
    gl_PointSize = clamp(size, 1.0, 24.0);

    vec3 cool = mix(vec3(0.35, 0.55, 1.0), vec3(0.6, 0.9, 1.0), p.velSeed.w);
    vec3 hot  = blackbody(mix(4500.0, 12000.0, heat));
    float life = p.posLife.w;
    float fade = smoothstep(0.0, 0.08, life) * smoothstep(0.0, 0.05, 1.0 - life);   // fade in at spawn, out at death
    float speed = length(p.velSeed.xyz);
    float bright = P.particleBrightness * fade * (0.08 + heat * 0.9 + min(speed, 0.6) * 0.5);
    vColor = vec4(mix(cool, hot, heat) * bright, 1.0);
}
