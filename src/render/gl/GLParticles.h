#pragma once
#include "render/gl/GLLoader.h"
#include "render/IRenderer.h"
#include <filesystem>
#include <memory>

namespace bh {
namespace cuda { class GLBuffer; }

/// Particle pool living in a GL SSBO, simulated either by CUDA (GL interop) or
/// by the GLSL compute shader. Drawing is done by GLRenderer.
class GLParticles {
public:
    GLParticles();
    ~GLParticles();
    bool init(uint32_t count, ParticleBackend pref, const std::filesystem::path& shaderDir);
    void shutdown();
    void resize(uint32_t count);
    void reset();
    void simulate(const ParticleParams& pp, GLuint uboParticleParams);
    GLuint ssbo() const { return ssbo_; }
    uint32_t count() const { return count_; }
    const char* backendName() const { return useCuda_ ? "CUDA (GL interop)" : "GLSL compute"; }

private:
    void createBuffer(uint32_t count);
    GLuint ssbo_ = 0, prog_ = 0;
    uint32_t count_ = 0;
    bool useCuda_ = false;
    std::unique_ptr<cuda::GLBuffer> cuda_;
};

}  // namespace bh
