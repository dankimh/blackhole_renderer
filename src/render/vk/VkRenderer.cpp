#include "render/vk/VkRenderer.h"
#include "util/File.h"
#include "util/Log.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>
#if BH_ENABLE_IMGUI
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#endif

namespace bh {

static const VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
static const VkFormat kOutFormat = VK_FORMAT_R8G8B8A8_UNORM;

VkRenderer::~VkRenderer() { shutdown(); }

// ---------------------------------------------------------------------------
bool VkRenderer::init(const RendererConfig& cfg) {
    cfg_ = cfg;
    bool wantCuda = cfg.particleBackend != ParticleBackend::Compute;
#if !BH_ENABLE_CUDA
    wantCuda = false;
#endif
    if (!ctx_.init(cfg.window, cfg.validation, cfg.gpuIndex, wantCuda)) return false;
    spvDir_ = file::resource("shaders") / "spv";
    winW_ = cfg.windowWidth; winH_ = cfg.windowHeight;
    inW_ = cfg.internalWidth; inH_ = cfg.internalHeight;

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 1.f;
    VK_CHECK(vkCreateSampler(ctx_.device, &sci, nullptr, &sampler_));

    const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uboRender_ = ctx_.createBuffer(sizeof(RenderParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host);
    uboParticle_ = ctx_.createBuffer(sizeof(ParticleParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host);
    for (auto& u : uboBloom_) u = ctx_.createBuffer(sizeof(BloomParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host);

    if (!particles_.init(ctx_, cfg.particleCount, cfg.particleBackend)) return false;
    if (!createDescriptors()) return false;

    // HDR render pass (particles blended over the ray-marched image, layout stays GENERAL)
    {
        VkAttachmentDescription att{};
        att.format = kHdrFormat;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.initialLayout = att.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_GENERAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo rci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rci.attachmentCount = 1; rci.pAttachments = &att;
        rci.subpassCount = 1; rci.pSubpasses = &sub;
        rci.dependencyCount = 2; rci.pDependencies = deps;
        VK_CHECK(vkCreateRenderPass(ctx_.device, &rci, nullptr, &rpHdr_));
    }
    if (!createPresentTargets()) return false;   // swapchain / offscreen + present render pass
    if (!createPipelines()) return false;
    if (!createTargets()) return false;

    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = ctx_.cmdPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(ctx_.device, &cai, &cmd_));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(ctx_.device, &fci, nullptr, &fence_));
    semAcquire_ = ctx_.createSemaphore(false);
    semRender_ = ctx_.createSemaphore(false);

#if BH_ENABLE_IMGUI
    if (cfg.debugUi && cfg.window) {
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8};
        VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pci.maxSets = 8; pci.poolSizeCount = 1; pci.pPoolSizes = &ps;
        VK_CHECK(vkCreateDescriptorPool(ctx_.device, &pci, nullptr, &imguiPool_));
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForVulkan(cfg.window, true);
        ImGui_ImplVulkan_InitInfo ii{};
        ii.Instance = ctx_.instance;
        ii.PhysicalDevice = ctx_.physical;
        ii.Device = ctx_.device;
        ii.QueueFamily = ctx_.queueFamily;
        ii.Queue = ctx_.queue;
        ii.DescriptorPool = imguiPool_;
        ii.RenderPass = rpPresent_;
        ii.MinImageCount = 2;
        ii.ImageCount = (uint32_t)std::max<size_t>(2, ctx_.swapImages.size());
        ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        if (ImGui_ImplVulkan_Init(&ii)) imgui_ = true;
        else LOG_WARN("ImGui Vulkan init failed");
    }
#endif
    LOG_INFO("Vulkan targets: internal %dx%d, window %dx%d", inW_, inH_, winW_, winH_);
    return true;
}

// ---------------------------------------------------------------------------
bool VkRenderer::createDescriptors() {
    VkDescriptorSetLayoutBinding b[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {9, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = (uint32_t)std::size(b);
    lci.pBindings = b;
    VK_CHECK(vkCreateDescriptorSetLayout(ctx_.device, &lci, nullptr, &setLayout_));
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &setLayout_;
    VK_CHECK(vkCreatePipelineLayout(ctx_.device, &pli, nullptr, &pipeLayout_));

    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 9}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 6},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6}};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 3;
    pci.poolSizeCount = (uint32_t)std::size(sizes);
    pci.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(ctx_.device, &pci, nullptr, &pool_));
    VkDescriptorSetLayout layouts[3] = {setLayout_, setLayout_, setLayout_};
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 3;
    ai.pSetLayouts = layouts;
    VK_CHECK(vkAllocateDescriptorSets(ctx_.device, &ai, sets_));
    return true;
}

void VkRenderer::updateDescriptors() {
    // set 0: bloom pass 0 (hdr -> A), also used by blackhole/particles/composite
    // set 1: bloom pass 1 (A -> B)      set 2: bloom pass 2 (B -> A)
    VkImageView srcViews[3] = {hdr_.view, bloomA_.view, bloomB_.view};
    VkImageView dstViews[3] = {bloomA_.view, bloomB_.view, bloomA_.view};
    std::vector<VkWriteDescriptorSet> writes;
    VkDescriptorBufferInfo bufRender{uboRender_.buffer, 0, sizeof(RenderParams)};
    VkDescriptorBufferInfo bufParticle{uboParticle_.buffer, 0, sizeof(ParticleParams)};
    VkDescriptorBufferInfo bufSsbo{particles_.buffer(), 0, particles_.size()};
    VkDescriptorBufferInfo bufBloom[3];
    VkDescriptorImageInfo imgHdr{VK_NULL_HANDLE, hdr_.view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo imgSrc[3], imgDst[3], imgBloomA{sampler_, bloomA_.view, VK_IMAGE_LAYOUT_GENERAL};
    for (int i = 0; i < 3; ++i) {
        bufBloom[i] = {uboBloom_[i].buffer, 0, sizeof(BloomParams)};
        imgSrc[i] = {sampler_, srcViews[i], VK_IMAGE_LAYOUT_GENERAL};
        imgDst[i] = {VK_NULL_HANDLE, dstViews[i], VK_IMAGE_LAYOUT_GENERAL};
        auto w = [&](uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo* b, const VkDescriptorImageInfo* im) {
            VkWriteDescriptorSet ws{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            ws.dstSet = sets_[i]; ws.dstBinding = binding; ws.descriptorCount = 1; ws.descriptorType = type;
            ws.pBufferInfo = b; ws.pImageInfo = im;
            writes.push_back(ws);
        };
        w(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufRender, nullptr);
        w(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &imgHdr);
        w(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bufSsbo, nullptr);
        w(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufParticle, nullptr);
        w(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &imgSrc[i]);
        w(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &imgBloomA);
        w(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &imgDst[i]);
        w(9, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufBloom[i], nullptr);
    }
    vkUpdateDescriptorSets(ctx_.device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    particleGen_ = particles_.generation();
}

// ---------------------------------------------------------------------------
VkPipeline VkRenderer::makeCompute(const char* spv) {
    VkShaderModule m = ctx_.loadShader(spvDir_ / spv);
    if (!m) return VK_NULL_HANDLE;
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, m, "main", nullptr};
    ci.layout = pipeLayout_;
    VkPipeline p = VK_NULL_HANDLE;
    VK_CHECK(vkCreateComputePipelines(ctx_.device, VK_NULL_HANDLE, 1, &ci, nullptr, &p));
    vkDestroyShaderModule(ctx_.device, m, nullptr);
    return p;
}

VkPipeline VkRenderer::makeGraphics(const char* vert, const char* frag, VkRenderPass rp, bool points, bool additive) {
    VkShaderModule vm = ctx_.loadShader(spvDir_ / vert), fm = ctx_.loadShader(spvDir_ / frag);
    if (!vm || !fm) return VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vm, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fm, "main", nullptr}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = points ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 0xF;
    ba.blendEnable = additive ? VK_TRUE : VK_FALSE;
    ba.srcColorBlendFactor = ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ba.dstColorBlendFactor = ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ba.colorBlendOp = ba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1; cb.pAttachments = &ba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.stageCount = 2; ci.pStages = stages;
    ci.pVertexInputState = &vi; ci.pInputAssemblyState = &ia; ci.pViewportState = &vp;
    ci.pRasterizationState = &rs; ci.pMultisampleState = &ms; ci.pColorBlendState = &cb; ci.pDynamicState = &ds;
    ci.layout = pipeLayout_;
    ci.renderPass = rp;
    VkPipeline p = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(ctx_.device, VK_NULL_HANDLE, 1, &ci, nullptr, &p));
    vkDestroyShaderModule(ctx_.device, vm, nullptr);
    vkDestroyShaderModule(ctx_.device, fm, nullptr);
    return p;
}

bool VkRenderer::createPipelines() {
    pipeBlackhole_ = makeCompute("blackhole.comp.spv");
    pipeParticleSim_ = makeCompute("particles_sim.comp.spv");
    pipeBloom_ = makeCompute("bloom.comp.spv");
    pipeParticles_ = makeGraphics("particles.vert.spv", "particles.frag.spv", rpHdr_, true, true);
    pipeComposite_ = makeGraphics("composite.vert.spv", "composite.frag.spv", rpPresent_, false, false);
    return pipeBlackhole_ && pipeParticleSim_ && pipeBloom_ && pipeParticles_ && pipeComposite_;
}

// ---------------------------------------------------------------------------
bool VkRenderer::createTargets() {
    destroyTargets();
    inW_ = std::max(inW_, 8); inH_ = std::max(inH_, 8);
    bloomW_ = std::max(inW_ / 2, 1); bloomH_ = std::max(inH_ / 2, 1);
    const VkImageUsageFlags hdrUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    hdr_ = ctx_.createImage(inW_, inH_, kHdrFormat, hdrUsage);
    bloomA_ = ctx_.createImage(bloomW_, bloomH_, kHdrFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    bloomB_ = ctx_.createImage(bloomW_, bloomH_, kHdrFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    VkCommandBuffer cmd = ctx_.beginOneTime();
    VkClearColorValue zero{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (ImageRes* im : {&hdr_, &bloomA_, &bloomB_}) {
        ctx_.transition(cmd, im->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        vkCmdClearColorImage(cmd, im->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        ctx_.transition(cmd, im->image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    }
    ctx_.endOneTime(cmd);

    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = rpHdr_;
    fci.attachmentCount = 1;
    fci.pAttachments = &hdr_.view;
    fci.width = inW_; fci.height = inH_; fci.layers = 1;
    VK_CHECK(vkCreateFramebuffer(ctx_.device, &fci, nullptr, &fbHdr_));
    updateDescriptors();
    accumValid_ = false;
    return true;
}

void VkRenderer::destroyTargets() {
    if (fbHdr_) vkDestroyFramebuffer(ctx_.device, fbHdr_, nullptr);
    fbHdr_ = VK_NULL_HANDLE;
    ctx_.destroy(hdr_); ctx_.destroy(bloomA_); ctx_.destroy(bloomB_);
}

bool VkRenderer::createPresentTargets() {
    destroyPresentTargets();
    VkFormat fmt;
    if (cfg_.window) {
        if (!ctx_.createSwapchain(winW_, winH_, cfg_.vsync)) return false;
        winW_ = ctx_.swapExtent.width; winH_ = ctx_.swapExtent.height;
        fmt = ctx_.swapFormat;
    } else {
        out_ = ctx_.createImage(winW_, winH_, kOutFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        fmt = kOutFormat;
    }
    if (!rpPresent_) {
        VkAttachmentDescription att{};
        att.format = fmt;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = cfg_.window ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rci.attachmentCount = 1; rci.pAttachments = &att;
        rci.subpassCount = 1; rci.pSubpasses = &sub;
        rci.dependencyCount = 1; rci.pDependencies = &dep;
        VK_CHECK(vkCreateRenderPass(ctx_.device, &rci, nullptr, &rpPresent_));
    }
    size_t n = cfg_.window ? ctx_.swapViews.size() : 1;
    fbPresent_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        VkImageView view = cfg_.window ? ctx_.swapViews[i] : out_.view;
        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fci.renderPass = rpPresent_;
        fci.attachmentCount = 1;
        fci.pAttachments = &view;
        fci.width = winW_; fci.height = winH_; fci.layers = 1;
        VK_CHECK(vkCreateFramebuffer(ctx_.device, &fci, nullptr, &fbPresent_[i]));
    }
    needSwapchain_ = false;
    return true;
}

void VkRenderer::destroyPresentTargets() {
    for (auto fb : fbPresent_) vkDestroyFramebuffer(ctx_.device, fb, nullptr);
    fbPresent_.clear();
    ctx_.destroy(out_);
}

void VkRenderer::resize(int windowW, int windowH, int internalW, int internalH) {
    bool win = windowW != winW_ || windowH != winH_;
    bool in = internalW != inW_ || internalH != inH_;
    if (!win && !in && !needSwapchain_) return;
    vkDeviceWaitIdle(ctx_.device);
    if (win || needSwapchain_) {
        winW_ = std::max(windowW, 1); winH_ = std::max(windowH, 1);
        createPresentTargets();
    }
    if (in) {
        inW_ = internalW; inH_ = internalH;
        createTargets();
    }
    LOG_INFO("Vulkan targets: internal %dx%d, window %dx%d", inW_, inH_, winW_, winH_);
}

void VkRenderer::setParticleCount(uint32_t count) {
    if (count == particles_.count()) return;
    vkDeviceWaitIdle(ctx_.device);
    particles_.resize(count);
    updateDescriptors();
}

// ---------------------------------------------------------------------------
void VkRenderer::barrierImage(VkCommandBuffer cmd, VkImage img, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                              VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    ctx_.transition(cmd, img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, src, dst, srcAccess, dstAccess);
}

void VkRenderer::recordFrame(VkCommandBuffer cmd, const FrameInput& frame, uint32_t imageIndex,
                             const std::function<void()>& debugUi) {
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    // Make last frame's writes visible (single frame in flight, but be explicit).
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeLayout_, 0, 1, &sets_[0], 0, nullptr);

    // 1. particles (GLSL path; CUDA path ran before submission)
    if (particles_.count() > 0 && !particles_.useCuda()) {
        if (frame.clearParticles) {
            vkCmdFillBuffer(cmd, particles_.buffer(), 0, particles_.size(), 0);
            VkBufferMemoryBarrier bb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            bb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            bb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            bb.srcQueueFamilyIndex = bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bb.buffer = particles_.buffer(); bb.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &bb, 0, nullptr);
        }
        if (frame.simulateParticles) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeParticleSim_);
            vkCmdDispatch(cmd, (particles_.count() + 255) / 256, 1, 1);
            VkBufferMemoryBarrier bb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            bb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            bb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            bb.srcQueueFamilyIndex = bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bb.buffer = particles_.buffer(); bb.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 1, &bb, 0, nullptr);
        }
    }

    // 2. ray march
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeBlackhole_);
    vkCmdDispatch(cmd, (inW_ + 7) / 8, (inH_ + 7) / 8, 1);

    // 3. particle points over the HDR image (render pass dependencies handle compute<->attachment)
    {
        VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rbi.renderPass = rpHdr_;
        rbi.framebuffer = fbHdr_;
        rbi.renderArea = {{0, 0}, {(uint32_t)inW_, (uint32_t)inH_}};
        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        if (frame.drawParticles && particles_.count() > 0) {
            VkViewport vp{0.f, 0.f, (float)inW_, (float)inH_, 0.f, 1.f};
            VkRect2D sc{{0, 0}, {(uint32_t)inW_, (uint32_t)inH_}};
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeParticles_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &sets_[0], 0, nullptr);
            vkCmdDraw(cmd, particles_.count(), 1, 0, 0);
        }
        vkCmdEndRenderPass(cmd);
    }

    // 4. bloom (3 dispatches: threshold+downsample, blur H, blur V)
    if (frame.render.bloomStrength > 0.f) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeBloom_);
        VkImage dstImgs[3] = {bloomA_.image, bloomB_.image, bloomA_.image};
        for (int i = 0; i < 3; ++i) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeLayout_, 0, 1, &sets_[i], 0, nullptr);
            vkCmdDispatch(cmd, (bloomW_ + 7) / 8, (bloomH_ + 7) / 8, 1);
            barrierImage(cmd, dstImgs[i], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        }
    } else {
        VkClearColorValue zero{};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, bloomA_.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        barrierImage(cmd, bloomA_.image, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // 5. composite to swapchain / offscreen (+ ImGui)
    {
        VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rbi.renderPass = rpPresent_;
        rbi.framebuffer = fbPresent_[imageIndex];
        rbi.renderArea = {{0, 0}, {(uint32_t)winW_, (uint32_t)winH_}};
        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0.f, 0.f, (float)winW_, (float)winH_, 0.f, 1.f};
        VkRect2D sc{{0, 0}, {(uint32_t)winW_, (uint32_t)winH_}};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeComposite_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &sets_[0], 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
#if BH_ENABLE_IMGUI
        if (imgui_) {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            if (debugUi) debugUi();
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        }
#else
        (void)debugUi;
#endif
        vkCmdEndRenderPass(cmd);
    }
    vkEndCommandBuffer(cmd);
}

void VkRenderer::render(const FrameInput& frame, const std::function<void()>& debugUi) {
    vkWaitForFences(ctx_.device, 1, &fence_, VK_TRUE, UINT64_MAX);
    if (particleGen_ != particles_.generation()) updateDescriptors();

    uint32_t imageIndex = 0;
    if (cfg_.window) {
        if (needSwapchain_) { resize(winW_, winH_, inW_, inH_); }
        VkResult r = vkAcquireNextImageKHR(ctx_.device, ctx_.swapchain, UINT64_MAX, semAcquire_, VK_NULL_HANDLE, &imageIndex);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) { needSwapchain_ = true; return; }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { LOG_ERROR("vkAcquireNextImageKHR: %s", vkResultString(r)); return; }
    }
    vkResetFences(ctx_.device, 1, &fence_);

    RenderParams rp = frame.render;
    if (!accumValid_) rp.accumulate = 0;
    accumValid_ = true;
    std::memcpy(uboRender_.mapped, &rp, sizeof(rp));
    std::memcpy(uboParticle_.mapped, &frame.particles, sizeof(frame.particles));
    BloomParams bp[3]{};
    float sx[3] = {1.f / inW_, 1.f / bloomW_, 1.f / bloomW_}, sy[3] = {1.f / inH_, 1.f / bloomH_, 1.f / bloomH_};
    float dx[3] = {0.f, 1.f, 0.f}, dy[3] = {0.f, 0.f, 1.f}, mode[3] = {0.f, 1.f, 1.f};
    for (int i = 0; i < 3; ++i) {
        bp[i].texel[0] = sx[i]; bp[i].texel[1] = sy[i]; bp[i].texel[2] = (float)bloomW_; bp[i].texel[3] = (float)bloomH_;
        bp[i].dir[0] = dx[i]; bp[i].dir[1] = dy[i]; bp[i].dir[2] = mode[i]; bp[i].dir[3] = rp.bloomThreshold;
        std::memcpy(uboBloom_[i].mapped, &bp[i], sizeof(BloomParams));
    }

    // CUDA path: simulate on the CUDA stream, hand off through external semaphores.
    bool cudaSync = particles_.useCuda() && particles_.count() > 0 && frame.simulateParticles;
    if (cudaSync) particles_.cudaSimulate(frame.particles, frame.clearParticles);

    vkResetCommandBuffer(cmd_, 0);
    recordFrame(cmd_, frame, imageIndex, debugUi);

    std::vector<VkSemaphore> waits, signals;
    std::vector<VkPipelineStageFlags> waitStages;
    if (cfg_.window) { waits.push_back(semAcquire_); waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT); signals.push_back(semRender_); }
    if (cudaSync) {
        waits.push_back(particles_.semCudaDone());
        waitStages.push_back(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        signals.push_back(particles_.semVkDone());
        particles_.markVkDoneSignaled();
    }
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = (uint32_t)waits.size();
    si.pWaitSemaphores = waits.data();
    si.pWaitDstStageMask = waitStages.data();
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    si.signalSemaphoreCount = (uint32_t)signals.size();
    si.pSignalSemaphores = signals.data();
    VK_CHECK(vkQueueSubmit(ctx_.queue, 1, &si, fence_));

    if (cfg_.window) {
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &semRender_;
        pi.swapchainCount = 1;
        pi.pSwapchains = &ctx_.swapchain;
        pi.pImageIndices = &imageIndex;
        VkResult r = vkQueuePresentKHR(ctx_.queue, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) needSwapchain_ = true;
        else if (r != VK_SUCCESS) LOG_ERROR("vkQueuePresentKHR: %s", vkResultString(r));
    }
    lastImage_ = imageIndex;
}

// ---------------------------------------------------------------------------
bool VkRenderer::readback(std::vector<uint8_t>& rgba, int& w, int& h) {
    vkWaitForFences(ctx_.device, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkQueueWaitIdle(ctx_.queue);
    w = winW_; h = winH_;
    VkImage src = cfg_.window ? ctx_.swapImages[lastImage_] : out_.image;
    VkFormat fmt = cfg_.window ? ctx_.swapFormat : kOutFormat;
    VkImageLayout layout = cfg_.window ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    BufferRes staging = ctx_.createBuffer((VkDeviceSize)w * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkCommandBuffer cmd = ctx_.beginOneTime();
    if (cfg_.window)
        ctx_.transition(cmd, src, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
    vkCmdCopyImageToBuffer(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);
    if (cfg_.window)
        ctx_.transition(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT);
    ctx_.endOneTime(cmd);
    rgba.resize((size_t)w * h * 4);
    std::memcpy(rgba.data(), staging.mapped, rgba.size());
    ctx_.destroy(staging);
    if (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB)
        for (size_t i = 0; i < rgba.size(); i += 4) std::swap(rgba[i], rgba[i + 2]);
    return true;
}

void VkRenderer::shutdown() {
    if (!ctx_.device) return;
    vkDeviceWaitIdle(ctx_.device);
#if BH_ENABLE_IMGUI
    if (imgui_) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        imgui_ = false;
    }
    if (imguiPool_) vkDestroyDescriptorPool(ctx_.device, imguiPool_, nullptr);
#endif
    particles_.shutdown();
    destroyTargets();
    destroyPresentTargets();
    for (VkPipeline p : {pipeBlackhole_, pipeParticleSim_, pipeBloom_, pipeParticles_, pipeComposite_})
        if (p) vkDestroyPipeline(ctx_.device, p, nullptr);
    if (rpHdr_) vkDestroyRenderPass(ctx_.device, rpHdr_, nullptr);
    if (rpPresent_) vkDestroyRenderPass(ctx_.device, rpPresent_, nullptr);
    if (pool_) vkDestroyDescriptorPool(ctx_.device, pool_, nullptr);
    if (pipeLayout_) vkDestroyPipelineLayout(ctx_.device, pipeLayout_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(ctx_.device, setLayout_, nullptr);
    if (sampler_) vkDestroySampler(ctx_.device, sampler_, nullptr);
    ctx_.destroy(uboRender_); ctx_.destroy(uboParticle_);
    for (auto& u : uboBloom_) ctx_.destroy(u);
    if (fence_) vkDestroyFence(ctx_.device, fence_, nullptr);
    if (semAcquire_) vkDestroySemaphore(ctx_.device, semAcquire_, nullptr);
    if (semRender_) vkDestroySemaphore(ctx_.device, semRender_, nullptr);
    ctx_.shutdown();
}

}  // namespace bh
