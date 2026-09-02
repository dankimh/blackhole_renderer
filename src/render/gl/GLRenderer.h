#pragma once
#include "render/IRenderer.h"
#include "render/gl/GLLoader.h"
#include "render/gl/GLParticles.h"
#include <filesystem>
#include <memory>

namespace bh {
class EglContext;

/// OpenGL 4.6 backend: compute-shader ray marcher + particle points + bloom + composite.
class GLRenderer : public IRenderer {
public:
    GLRenderer();
    ~GLRenderer() override;
    bool init(const RendererConfig& cfg) override;
    void shutdown() override;
    void resize(int windowW, int windowH, int internalW, int internalH) override;
    void setParticleCount(uint32_t count) override;
    void render(const FrameInput& frame, const std::function<void()>& debugUi) override;
    bool readback(std::vector<uint8_t>& rgba, int& w, int& h) override;
    const char* name() const override { return "OpenGL 4.6 compute"; }
    const char* particleBackendName() const override { return particles_.backendName(); }
    std::string deviceName() const override { return device_; }

private:
    bool createTargets();
    void destroyTargets();
    void bloomPass(const FrameInput& frame);

    RendererConfig cfg_;
    std::unique_ptr<EglContext> egl_;
    std::filesystem::path shaderDir_;
    std::string device_;
    GLuint progBlackhole_ = 0, progBloom_ = 0, progComposite_ = 0, progParticles_ = 0;
    GLuint uboRender_ = 0, uboParticle_ = 0, uboBloom_ = 0, vao_ = 0;
    GLuint hdrTex_ = 0, bloomA_ = 0, bloomB_ = 0, hdrFbo_ = 0, outTex_ = 0, outFbo_ = 0;
    int winW_ = 0, winH_ = 0, inW_ = 0, inH_ = 0, bloomW_ = 0, bloomH_ = 0;
    bool accumValid_ = false;
    bool imgui_ = false;
    GLParticles particles_;
};

}  // namespace bh
