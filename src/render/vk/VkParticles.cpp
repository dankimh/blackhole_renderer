#include "render/vk/VkParticles.h"
#include "util/Log.h"
#include <vector>
#if BH_ENABLE_CUDA
#include "render/cuda/CudaInterop.h"
#include "render/cuda/CudaParticles.h"
#include <cuda_runtime.h>
#endif

namespace bh {

VkParticles::VkParticles() = default;
VkParticles::~VkParticles() { shutdown(); }

bool VkParticles::init(VkContext& ctx, uint32_t count, ParticleBackend pref) {
    ctx_ = &ctx;
#if BH_ENABLE_CUDA
    if (pref != ParticleBackend::Compute && ctx.externalSupported) {
        std::string dev;
        if (cuda::selectDeviceByUuid(ctx.deviceUuid) && cuda::available(&dev)) {
            useCuda_ = true;
            LOG_INFO("CUDA particle backend (Vulkan interop): %s", dev.c_str());
        } else if (pref == ParticleBackend::Cuda) {
            LOG_WARN("CUDA requested but no CUDA device matches the Vulkan device - using GLSL compute");
        }
    } else if (pref == ParticleBackend::Cuda) {
        LOG_WARN("CUDA requested but Vulkan external memory unsupported - using GLSL compute");
    }
    if (useCuda_) {
        semCudaDone_ = ctx.createSemaphore(true);
        semVkDone_ = ctx.createSemaphore(true);
        OsHandle h1, h2;
        cudaSemDone_ = std::make_unique<cuda::VkExternalSemaphore>();
        cudaSemVkDone_ = std::make_unique<cuda::VkExternalSemaphore>();
        bool ok = ctx.exportSemaphore(semCudaDone_, h1) && ctx.exportSemaphore(semVkDone_, h2);
        cuda::ExternalHandle e1{h1.fd, h1.win32}, e2{h2.fd, h2.win32};
        if (!ok || !cudaSemDone_->import(e1) || !cudaSemVkDone_->import(e2)) {
            LOG_WARN("Semaphore export/import failed - using GLSL compute");
            cudaSemDone_.reset(); cudaSemVkDone_.reset();
            vkDestroySemaphore(ctx.device, semCudaDone_, nullptr);
            vkDestroySemaphore(ctx.device, semVkDone_, nullptr);
            semCudaDone_ = semVkDone_ = VK_NULL_HANDLE;
            useCuda_ = false;
        }
    }
#else
    (void)pref;
#endif
    createBuffer(count);
    return true;
}

void VkParticles::createBuffer(uint32_t count) {
#if BH_ENABLE_CUDA
    if (cudaMem_) { cuda::syncDevice(); cudaMem_.reset(); }
#endif
    if (ssbo_.buffer) ctx_->destroy(ssbo_);
    count_ = count;
    ++generation_;
    VkDeviceSize bytes = VkDeviceSize(sizeof(Particle)) * std::max(count, 1u);
    ssbo_ = ctx_->createBuffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, useCuda_);
    // zero-fill => every particle dead
    VkCommandBuffer cmd = ctx_->beginOneTime();
    vkCmdFillBuffer(cmd, ssbo_.buffer, 0, bytes, 0);
    ctx_->endOneTime(cmd);
#if BH_ENABLE_CUDA
    if (useCuda_) {
        OsHandle h;
        cudaMem_ = std::make_unique<cuda::VkExternalBuffer>();
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(ctx_->device, ssbo_.buffer, &req);
        if (!ctx_->exportMemory(ssbo_.memory, h) || !cudaMem_->import(cuda::ExternalHandle{h.fd, h.win32}, (size_t)req.size)) {
            LOG_WARN("Vulkan memory export to CUDA failed - using GLSL compute");
            cudaMem_.reset();
            useCuda_ = false;
        }
    }
#endif
}

void VkParticles::resize(uint32_t count) {
    if (count == count_) return;
    vkDeviceWaitIdle(ctx_->device);
    createBuffer(count);
}

void VkParticles::cudaSimulate(const ParticleParams& pp, bool clear) {
#if BH_ENABLE_CUDA
    if (!useCuda_ || !cudaMem_) return;
    if (vkDonePending_) { cudaSemVkDone_->wait(); vkDonePending_ = false; }
    if (clear) cudaMemsetAsync(cudaMem_->ptr(), 0, sizeof(Particle) * count_, 0);
    cuda::simulateParticles(cudaMem_->ptr(), pp);
    cudaSemDone_->signal();
#else
    (void)pp; (void)clear;
#endif
}

void VkParticles::shutdown() {
    if (!ctx_) return;
#if BH_ENABLE_CUDA
    cuda::syncDevice();
    cudaMem_.reset();
    cudaSemDone_.reset();
    cudaSemVkDone_.reset();
#endif
    if (ssbo_.buffer) ctx_->destroy(ssbo_);
    if (semCudaDone_) vkDestroySemaphore(ctx_->device, semCudaDone_, nullptr);
    if (semVkDone_) vkDestroySemaphore(ctx_->device, semVkDone_, nullptr);
    semCudaDone_ = semVkDone_ = VK_NULL_HANDLE;
    ctx_ = nullptr;
}

}  // namespace bh
