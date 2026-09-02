#pragma once
#include "render/IRenderer.h"
#include "render/vk/VkContext.h"
#include <memory>

namespace bh {
namespace cuda { class VkExternalBuffer; class VkExternalSemaphore; }

/// Particle SSBO for the Vulkan backend. With CUDA: memory is exported through
/// VK_KHR_external_memory and simulated by the CUDA kernel, synchronised with
/// exported binary semaphores (cudaDone / vkDone).
class VkParticles {
public:
    VkParticles();
    ~VkParticles();
    bool init(VkContext& ctx, uint32_t count, ParticleBackend pref);
    void shutdown();
    void resize(uint32_t count);

    VkBuffer buffer() const { return ssbo_.buffer; }
    VkDeviceSize size() const { return ssbo_.size; }
    uint32_t count() const { return count_; }
    uint32_t generation() const { return generation_; }
    bool useCuda() const { return useCuda_; }
    const char* backendName() const { return useCuda_ ? "CUDA (Vulkan interop)" : "GLSL compute"; }

    /// CUDA path: wait for last frame's Vulkan work, simulate, signal cudaDone.
    void cudaSimulate(const ParticleParams& pp, bool clear);
    VkSemaphore semCudaDone() const { return semCudaDone_; }
    VkSemaphore semVkDone() const { return semVkDone_; }
    bool vkDonePending() const { return vkDonePending_; }
    void markVkDoneSignaled() { vkDonePending_ = true; }

private:
    void createBuffer(uint32_t count);
    VkContext* ctx_ = nullptr;
    BufferRes ssbo_;
    uint32_t count_ = 0, generation_ = 0;
    bool useCuda_ = false, vkDonePending_ = false;
    VkSemaphore semCudaDone_ = VK_NULL_HANDLE, semVkDone_ = VK_NULL_HANDLE;
    std::unique_ptr<cuda::VkExternalBuffer> cudaMem_;
    std::unique_ptr<cuda::VkExternalSemaphore> cudaSemDone_, cudaSemVkDone_;
};

}  // namespace bh
