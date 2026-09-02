#pragma once
#include "render/RenderParams.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace bh {

enum class Backend { OpenGL, Vulkan };
enum class ParticleBackend { Auto, Cuda, Compute };

struct RendererConfig {
    GLFWwindow* window = nullptr;    // null => headless (EGL surfaceless / VK no swapchain)
    GLFWwindow* glContextWindow = nullptr;   // GL: window owning the context (defaults to `window`)
    int windowWidth = 1280;
    int windowHeight = 720;
    int internalWidth = 1280;        // ray-march resolution (windowSize * renderScale)
    int internalHeight = 720;
    uint32_t particleCount = 200000;
    ParticleBackend particleBackend = ParticleBackend::Auto;
    bool vsync = false;
    bool debugUi = false;            // Dear ImGui overlay
    bool validation = false;         // Vulkan validation layers / GL debug output
    bool offscreenPresent = false;   // render to an offscreen target, never swap; caller reads back & presents
    int gpuIndex = 0;                // headless EGL device / Vulkan physical device index
};

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool init(const RendererConfig& cfg) = 0;
    virtual void shutdown() = 0;
    /// Window size and/or internal resolution changed.
    virtual void resize(int windowW, int windowH, int internalW, int internalH) = 0;
    virtual void setParticleCount(uint32_t count) = 0;
    /// Simulate + ray-march + particles + bloom + composite + present.
    /// `debugUi` (may be empty) is called between ImGui NewFrame and Render.
    virtual void render(const FrameInput& frame, const std::function<void()>& debugUi) = 0;
    /// Copy the last presented image (RGBA8, row 0 = top).
    virtual bool readback(std::vector<uint8_t>& rgba, int& w, int& h) = 0;
    /// Fast path for LayeredPresenter: BGRA8, `topDown` reports the row order (no flip performed).
    virtual bool readbackBGRA(std::vector<uint8_t>& bgra, int& w, int& h, bool& topDown) = 0;
    virtual const char* name() const = 0;
    virtual const char* particleBackendName() const = 0;
    virtual std::string deviceName() const = 0;
};

}  // namespace bh
