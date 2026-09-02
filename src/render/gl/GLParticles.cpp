#include "render/gl/GLParticles.h"
#include "render/gl/GLShader.h"
#include "util/Log.h"
#include <vector>
#if BH_ENABLE_CUDA
#include "render/cuda/CudaInterop.h"
#include "render/cuda/CudaParticles.h"
#endif

namespace bh {

GLParticles::GLParticles() = default;
GLParticles::~GLParticles() { shutdown(); }

bool GLParticles::init(uint32_t count, ParticleBackend pref, const std::filesystem::path& shaderDir) {
    prog_ = gl::buildProgram(shaderDir, {{GL_COMPUTE_SHADER, "particles_sim.comp"}});
    if (!prog_) return false;
#if BH_ENABLE_CUDA
    if (pref != ParticleBackend::Compute) {
        std::string dev;
        if (cuda::available(&dev)) {
            useCuda_ = true;
            LOG_INFO("CUDA particle backend: %s", dev.c_str());
        } else if (pref == ParticleBackend::Cuda) {
            LOG_WARN("CUDA requested but unavailable - falling back to GLSL compute");
        }
    }
#else
    (void)pref;
#endif
    createBuffer(count);
    return true;
}

void GLParticles::createBuffer(uint32_t count) {
#if BH_ENABLE_CUDA
    cuda_.reset();
#endif
    if (ssbo_) glDeleteBuffers(1, &ssbo_);
    ssbo_ = 0;
    count_ = count;
    if (count == 0) return;
    glCreateBuffers(1, &ssbo_);
    std::vector<Particle> zero(count);   // life = 0 -> all dead
    glNamedBufferData(ssbo_, (GLsizeiptr)(sizeof(Particle) * count), zero.data(), GL_DYNAMIC_COPY);
#if BH_ENABLE_CUDA
    if (useCuda_) {
        cuda_ = std::make_unique<cuda::GLBuffer>();
        if (!cuda_->registerBuffer(ssbo_, sizeof(Particle) * count)) {
            LOG_WARN("cudaGraphicsGLRegisterBuffer failed - using GLSL compute");
            cuda_.reset();
            useCuda_ = false;
        }
    }
#endif
}

void GLParticles::resize(uint32_t count) {
    if (count == count_) return;
    createBuffer(count);
}

void GLParticles::reset() {
    if (!ssbo_) return;
    GLuint zero = 0;
    glClearNamedBufferData(ssbo_, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
}

void GLParticles::simulate(const ParticleParams& pp, GLuint uboParticleParams) {
    if (!ssbo_ || count_ == 0) return;
#if BH_ENABLE_CUDA
    if (useCuda_ && cuda_) {
        void* dev = cuda_->map();
        if (dev) {
            cuda::simulateParticles(dev, pp);
            cuda_->unmap();
            return;
        }
    }
#endif
    glUseProgram(prog_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 4, uboParticleParams);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo_);
    glDispatchCompute((count_ + 255) / 256, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void GLParticles::shutdown() {
#if BH_ENABLE_CUDA
    cuda_.reset();
#endif
    if (ssbo_) { glDeleteBuffers(1, &ssbo_); ssbo_ = 0; }
    if (prog_) { glDeleteProgram(prog_); prog_ = 0; }
}

}  // namespace bh
