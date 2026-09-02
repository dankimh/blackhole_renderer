#include "render/cuda/CudaParticles.h"
#include "util/Log.h"
#include <cuda_runtime.h>
#include <cmath>
#include <cstring>

namespace bh::cuda {

// ---- hashing (same as shaders/common/hash.glsl) ---------------------------
__device__ __forceinline__ unsigned hashU(unsigned x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16; return x;
}
__device__ __forceinline__ unsigned hashU3(unsigned a, unsigned b, unsigned c) { return hashU(a ^ hashU(b ^ hashU(c))); }
__device__ __forceinline__ float hashF(unsigned x) { return (float)(hashU(x) & 0x00ffffffu) / 16777216.0f; }
__device__ __forceinline__ float3 hash33(unsigned a, unsigned b, unsigned c) {
    unsigned h = hashU3(a, b, c);
    return make_float3(hashF(h), hashF(h ^ 0x9e3779b9u), hashF(h ^ 0x85ebca6bu));
}

__device__ __forceinline__ float3 operator+(float3 a, float3 b) { return make_float3(a.x + b.x, a.y + b.y, a.z + b.z); }
__device__ __forceinline__ float3 operator-(float3 a, float3 b) { return make_float3(a.x - b.x, a.y - b.y, a.z - b.z); }
__device__ __forceinline__ float3 operator*(float3 a, float s) { return make_float3(a.x * s, a.y * s, a.z * s); }
__device__ __forceinline__ float dot3(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
__device__ __forceinline__ float3 cross3(float3 a, float3 b) {
    return make_float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
__device__ __forceinline__ float len3(float3 a) { return sqrtf(dot3(a, a)); }
__device__ __forceinline__ float3 norm3(float3 a) { float l = len3(a); return l > 0.f ? a * (1.f / l) : a; }

struct DevParticle { float4 posLife; float4 velSeed; };

__global__ void simulateKernel(DevParticle* particles, ParticleParams pp) {
    unsigned idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= pp.count) return;
    DevParticle p = particles[idx];

    if (p.posLife.w <= 0.f) {
        if (pp.spawnPos[3] > 0.5f && hashF(hashU3(idx, pp.frame, 0x1234u)) < pp.spawnProb) {
            float3 h = hash33(idx, pp.frame, 0x5151u);
            float3 h2 = hash33(idx, pp.frame, 0x7777u);
            float ang = h.x * 6.2831853f;
            float rad = sqrtf(h.y) * pp.spawnU[3];
            float3 sp = make_float3(pp.spawnPos[0], pp.spawnPos[1], pp.spawnPos[2]);
            float3 U = make_float3(pp.spawnU[0], pp.spawnU[1], pp.spawnU[2]);
            float3 V = make_float3(pp.spawnV[0], pp.spawnV[1], pp.spawnV[2]);
            float3 pos = sp + (U * cosf(ang) + V * sinf(ang)) * rad;
            float r = fmaxf(len3(pos), pp.rs * 1.5f);
            float3 n = make_float3(pp.planeNormal[0], pp.planeNormal[1], pp.planeNormal[2]);
            float3 tangent = norm3(cross3(n, pos));
            float vcirc = sqrtf(pp.mass / r);
            float bias = pp.orbitBias * (0.75f + 0.5f * h2.x);
            float3 vel = tangent * (vcirc * bias) - norm3(pos) * (vcirc * (0.15f + 0.3f * h2.y));
            vel = vel + (h2 - make_float3(0.5f, 0.5f, 0.5f)) * (vcirc * pp.spawnV[3]);
            p.posLife = make_float4(pos.x, pos.y, pos.z, 1.f);
            p.velSeed = make_float4(vel.x, vel.y, vel.z, h.z);
        }
        particles[idx] = p;
        return;
    }

    float3 pos = make_float3(p.posLife.x, p.posLife.y, p.posLife.z);
    float3 vel = make_float3(p.velSeed.x, p.velSeed.y, p.velSeed.z);
    float r = len3(pos);
    if (r < pp.rs * 1.02f) { p.posLife.w = 0.f; particles[idx] = p; return; }
    float d = fmaxf(r - pp.rs, 0.05f);
    float3 acc = pos * (-pp.mass / (r * d * d));
    vel = vel + acc * pp.dt;
    vel = vel * fmaxf(0.f, 1.f - pp.drag * pp.dt);
    pos = pos + vel * pp.dt;
    p.posLife = make_float4(pos.x, pos.y, pos.z, p.posLife.w - pp.dt / pp.maxLife);
    p.velSeed.x = vel.x; p.velSeed.y = vel.y; p.velSeed.z = vel.z;
    particles[idx] = p;
}

bool available(std::string* deviceName) {
    int n = 0;
    if (cudaGetDeviceCount(&n) != cudaSuccess || n == 0) return false;
    int dev = 0;
    cudaGetDevice(&dev);
    cudaDeviceProp prop{};
    if (cudaGetDeviceProperties(&prop, dev) != cudaSuccess) return false;
    if (deviceName) *deviceName = std::string(prop.name) + " (sm_" + std::to_string(prop.major) + std::to_string(prop.minor) + ")";
    return true;
}

void simulateParticles(void* devParticles, const ParticleParams& pp) {
    if (!devParticles || pp.count == 0) return;
    unsigned blocks = (pp.count + 255) / 256;
    simulateKernel<<<blocks, 256>>>(static_cast<DevParticle*>(devParticles), pp);
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) LOG_ERROR("CUDA kernel launch failed: %s", cudaGetErrorString(err));
}

void syncDevice() { cudaDeviceSynchronize(); }

}  // namespace bh::cuda

namespace bh::cuda {
bool selectDeviceByUuid(const unsigned char* uuid16) {
    int n = 0;
    if (cudaGetDeviceCount(&n) != cudaSuccess) return false;
    for (int i = 0; i < n; ++i) {
        cudaDeviceProp prop{};
        if (cudaGetDeviceProperties(&prop, i) != cudaSuccess) continue;
        if (memcmp(prop.uuid.bytes, uuid16, 16) == 0) {
            cudaSetDevice(i);
            return true;
        }
    }
    return false;
}
}  // namespace bh::cuda
