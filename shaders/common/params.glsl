// Shared uniform layouts. Must match src/render/RenderParams.h byte-for-byte (std140).
// GL: layout(binding=N)   Vulkan: layout(set=0, binding=N)
#ifndef BH_PARAMS_GLSL
#define BH_PARAMS_GLSL

#ifdef VULKAN
  #define SET0 set = 0,
  #define VERTEX_INDEX gl_VertexIndex
#else
  #define SET0
  #define VERTEX_INDEX gl_VertexID
#endif

// Binding map (shared by every pipeline; GL image units 0..7, texture units 0..15)
#define BIND_RENDER_PARAMS   0   // UBO  RenderParams
#define BIND_HDR_IMAGE       1   // image2D rgba16f  (raymarch output / accumulation)
#define BIND_PARTICLES       3   // SSBO Particle[]
#define BIND_PARTICLE_PARAMS 4   // UBO  ParticleParams
#define BIND_SRC_SAMPLER     5   // sampler2D (composite: hdr, bloom: source)
#define BIND_BLOOM_SAMPLER   6   // sampler2D bloom (composite)
#define BIND_DST_IMAGE       2   // image2D bloom destination (GL image units are 0..7)
#define BIND_BLOOM_PARAMS    9   // UBO BloomParams

layout(std140, SET0 binding = BIND_RENDER_PARAMS) uniform RenderParamsBlock {
    mat4  camToWorld;      // columns: right, up, forward, (unused)
    vec4  camPos;          // xyz = camera position (rs units), w = aspect
    vec4  resolution;      // xy = internal render size, zw = 1/size
    vec4  mouse;           // xy = normalized cursor (0..1, y down), z = active(0/1), w = seconds since move
    vec4  bhScreen;        // xy = black hole ndc position, z = camera distance, w = tanHalfFov

    float time;            // seconds (resets every 6h like rain)
    float dt;
    uint  frame;
    uint  samples;         // sub-samples per pixel per frame

    float tanHalfFov;
    float rs;              // Schwarzschild radius (world units)
    float diskInner;       // rs units
    float diskOuter;       // rs units

    float diskBrightness;
    float diskTemperature; // Kelvin at reference radius
    float diskSpeed;
    float diskThickness;

    int   maxSteps;
    float stepSize;
    float farRadius;
    float diskOpacity;

    float starDensity;
    float starBrightness;
    float nebulaBrightness;
    float exposure;

    float dopplerStrength;
    float redshiftStrength;
    float seed;
    float diskNoiseScale;

    uint  accumulate;      // 1 = temporal blend with previous frame
    float accumWeight;     // weight of the new frame
    float diskRotation;    // +1 / -1
    float particleSize;

    float particleBrightness;
    float lensStrength;    // particle pseudo-lensing amount
    float bloomStrength;
    float bloomThreshold;
} P;

struct Particle {
    vec4 posLife;   // xyz position, w life (0 = dead)
    vec4 velSeed;   // xyz velocity, w random seed (0..1)
};

layout(std140, SET0 binding = BIND_PARTICLE_PARAMS) uniform ParticleParamsBlock {
    vec4  spawnPos;     // xyz world position of cursor projected onto the lens plane, w = active
    vec4  spawnU;       // spawn-plane basis u (xyz), w = spawn radius
    vec4  spawnV;       // spawn-plane basis v (xyz), w = spread
    vec4  planeNormal;  // xyz = camera forward (orbit plane normal), w = unused

    float dt;           // simulation dt (already scaled by speed)
    float mass;         // M = rs/2
    float rs;
    float drag;

    uint  frame;
    uint  count;
    float spawnProb;    // per-dead-particle respawn probability this frame
    float maxLife;

    float orbitBias;    // 0 = radial fall, 1 = circular orbit
    float pad0, pad1, pad2;
} PP;

layout(std140, SET0 binding = BIND_BLOOM_PARAMS) uniform BloomParamsBlock {
    vec4 texel;   // xy = source texel size, zw = destination size
    vec4 dir;     // xy = blur direction (texels), z = mode (0 downsample, 1 blur), w = threshold
} BP;
#endif
