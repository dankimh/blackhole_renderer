#pragma once
#include "render/IRenderer.h"
#include "render/vk/VkContext.h"
#include "render/vk/VkParticles.h"
#include <filesystem>

namespace bh {

/// Vulkan 1.1 backend: compute ray marcher + particle points + bloom + composite.
/// One frame in flight (the ray march dominates; CPU is never the bottleneck).
class VkRenderer : public IRenderer {
public:
    ~VkRenderer() override;
    bool init(const RendererConfig& cfg) override;
    void shutdown() override;
    void resize(int windowW, int windowH, int internalW, int internalH) override;
    void setParticleCount(uint32_t count) override;
    void render(const FrameInput& frame, const std::function<void()>& debugUi) override;
    bool readback(std::vector<uint8_t>& rgba, int& w, int& h) override;
    const char* name() const override { return "Vulkan 1.1 compute"; }
    const char* particleBackendName() const override { return particles_.backendName(); }
    std::string deviceName() const override { return ctx_.deviceName; }

private:
    bool createDescriptors();
    bool createPipelines();
    bool createTargets();
    void destroyTargets();
    bool createPresentTargets();
    void destroyPresentTargets();
    void updateDescriptors();
    void recordFrame(VkCommandBuffer cmd, const FrameInput& frame, uint32_t imageIndex, const std::function<void()>& debugUi);
    void barrierImage(VkCommandBuffer cmd, VkImage img, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                      VkAccessFlags srcAccess, VkAccessFlags dstAccess);
    VkPipeline makeCompute(const char* spv);
    VkPipeline makeGraphics(const char* vert, const char* frag, VkRenderPass rp, bool points, bool additive);

    RendererConfig cfg_;
    VkContext ctx_;
    VkParticles particles_;
    std::filesystem::path spvDir_;

    ImageRes hdr_, bloomA_, bloomB_, out_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    BufferRes uboRender_, uboParticle_, uboBloom_[3];
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE, imguiPool_ = VK_NULL_HANDLE;
    VkDescriptorSet sets_[3]{};
    VkPipeline pipeBlackhole_ = VK_NULL_HANDLE, pipeParticleSim_ = VK_NULL_HANDLE, pipeBloom_ = VK_NULL_HANDLE;
    VkPipeline pipeParticles_ = VK_NULL_HANDLE, pipeComposite_ = VK_NULL_HANDLE;
    VkRenderPass rpHdr_ = VK_NULL_HANDLE, rpPresent_ = VK_NULL_HANDLE;
    VkFramebuffer fbHdr_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> fbPresent_;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkSemaphore semAcquire_ = VK_NULL_HANDLE, semRender_ = VK_NULL_HANDLE;

    int winW_ = 0, winH_ = 0, inW_ = 0, inH_ = 0, bloomW_ = 0, bloomH_ = 0;
    uint32_t lastImage_ = 0, particleGen_ = 0;
    bool accumValid_ = false, imgui_ = false, needSwapchain_ = false;
};

}  // namespace bh
