#pragma once
// std140 uniform blocks shared with shaders/common/params.glsl and the CUDA kernel.
#include <cstdint>

namespace bh {

struct alignas(16) RenderParams {
    float camToWorld[16];   // column-major: right, up, forward, unused
    float camPos[4];        // xyz, w = aspect
    float resolution[4];    // w, h, 1/w, 1/h
    float mouse[4];         // nx, ny (y down), active, seconds since move
    float bhScreen[4];      // black hole ndc xy, camera distance, tanHalfFov

    float time = 0.f;
    float dt = 0.f;
    uint32_t frame = 0;
    uint32_t samples = 1;

    float tanHalfFov = 0.5f;
    float rs = 1.f;
    float diskInner = 3.f;
    float diskOuter = 14.f;

    float diskBrightness = 1.f;
    float diskTemperature = 6500.f;
    float diskSpeed = 1.f;
    float diskThickness = 0.f;

    int32_t maxSteps = 400;
    float stepSize = 0.35f;
    float farRadius = 60.f;
    float diskOpacity = 0.85f;

    float starDensity = 0.5f;
    float starBrightness = 1.f;
    float nebulaBrightness = 1.f;
    float exposure = 1.2f;

    float dopplerStrength = 1.f;
    float redshiftStrength = 1.f;
    float seed = 0.f;
    float diskNoiseScale = 1.f;

    uint32_t accumulate = 0;
    float accumWeight = 1.f;
    float diskRotation = 1.f;
    float particleSize = 2.f;

    float particleBrightness = 1.f;
    float lensStrength = 1.f;
    float bloomStrength = 0.6f;
    float bloomThreshold = 0.9f;
};
static_assert(sizeof(RenderParams) == 16 * 4 + 4 * 16 + 4 * 4 * 8, "RenderParams layout must match std140");

struct alignas(16) ParticleParams {
    float spawnPos[4];     // xyz, w = active
    float spawnU[4];       // xyz basis, w = spawn radius
    float spawnV[4];       // xyz basis, w = spread
    float planeNormal[4];  // xyz camera forward

    float dt = 0.f;
    float mass = 0.5f;
    float rs = 1.f;
    float drag = 0.05f;

    uint32_t frame = 0;
    uint32_t count = 0;
    float spawnProb = 0.f;
    float maxLife = 12.f;

    float orbitBias = 1.f;
    float pad0 = 0.f, pad1 = 0.f, pad2 = 0.f;
};
static_assert(sizeof(ParticleParams) == 4 * 16 + 3 * 16, "ParticleParams layout must match std140");

struct alignas(16) BloomParams {
    float texel[4];   // src texel size xy, dst size zw
    float dir[4];     // blur dir xy, mode z, threshold w
};

struct Particle {
    float posLife[4];
    float velSeed[4];
};
static_assert(sizeof(Particle) == 32, "Particle must be 32 bytes");

/// Everything a backend needs to draw one frame.
struct FrameInput {
    RenderParams render{};
    ParticleParams particles{};
    bool simulateParticles = true;
    bool drawParticles = true;
    bool clearParticles = false;   // reset the particle pool (count changed / settings reset)
};

}  // namespace bh
