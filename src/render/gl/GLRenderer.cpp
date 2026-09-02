#include "render/gl/GLRenderer.h"
#include "render/gl/GLShader.h"
#include "util/File.h"
#include "util/Image.h"
#include "util/Log.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#if BH_ENABLE_HEADLESS && !defined(_WIN32)
#include "render/gl/EglContext.h"
#endif
#if BH_ENABLE_IMGUI
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#endif

namespace bh {

GLRenderer::GLRenderer() = default;
GLRenderer::~GLRenderer() { shutdown(); }

bool GLRenderer::init(const RendererConfig& cfg) {
    cfg_ = cfg;
    if (cfg.window) {
        glfwMakeContextCurrent(cfg.window);
        glfwSwapInterval(cfg.vsync ? 1 : 0);
        if (!gl::load((gl::GetProcFn)glfwGetProcAddress)) return false;
    } else {
#if BH_ENABLE_HEADLESS && !defined(_WIN32)
        egl_ = std::make_unique<EglContext>();
        if (!egl_->create(0)) return false;
        if (!gl::load(EglContext::getProcAddress)) return false;
#else
        LOG_ERROR("Headless GL is not available in this build");
        return false;
#endif
    }
    device_ = std::string((const char*)glGetString(GL_RENDERER)) + " / GL " + (const char*)glGetString(GL_VERSION);
    LOG_INFO("OpenGL: %s", device_.c_str());
    if (cfg.validation) gl::enableDebugOutput();

    shaderDir_ = file::resource("shaders");
    progBlackhole_ = gl::buildProgram(shaderDir_, {{GL_COMPUTE_SHADER, "blackhole.comp"}});
    progBloom_ = gl::buildProgram(shaderDir_, {{GL_COMPUTE_SHADER, "bloom.comp"}});
    progComposite_ = gl::buildProgram(shaderDir_, {{GL_VERTEX_SHADER, "composite.vert"}, {GL_FRAGMENT_SHADER, "composite.frag"}});
    progParticles_ = gl::buildProgram(shaderDir_, {{GL_VERTEX_SHADER, "particles.vert"}, {GL_FRAGMENT_SHADER, "particles.frag"}});
    if (!progBlackhole_ || !progBloom_ || !progComposite_ || !progParticles_) return false;

    glCreateBuffers(1, &uboRender_);
    glNamedBufferData(uboRender_, sizeof(RenderParams), nullptr, GL_DYNAMIC_DRAW);
    glCreateBuffers(1, &uboParticle_);
    glNamedBufferData(uboParticle_, sizeof(ParticleParams), nullptr, GL_DYNAMIC_DRAW);
    glCreateBuffers(1, &uboBloom_);
    glNamedBufferData(uboBloom_, sizeof(BloomParams), nullptr, GL_DYNAMIC_DRAW);
    glCreateVertexArrays(1, &vao_);

    if (!particles_.init(cfg.particleCount, cfg.particleBackend, shaderDir_)) return false;

    winW_ = cfg.windowWidth; winH_ = cfg.windowHeight;
    inW_ = cfg.internalWidth; inH_ = cfg.internalHeight;
    if (!createTargets()) return false;

#if BH_ENABLE_IMGUI
    if (cfg.debugUi && cfg.window) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(cfg.window, true);
        ImGui_ImplOpenGL3_Init("#version 460");
        imgui_ = true;
    }
#endif
    gl::checkErrors("GLRenderer::init");
    return true;
}

bool GLRenderer::createTargets() {
    destroyTargets();
    inW_ = std::max(inW_, 8); inH_ = std::max(inH_, 8);
    bloomW_ = std::max(inW_ / 2, 1); bloomH_ = std::max(inH_ / 2, 1);

    auto makeTex = [](GLuint& tex, GLenum fmt, int w, int h) {
        glCreateTextures(GL_TEXTURE_2D, 1, &tex);
        glTextureStorage2D(tex, 1, fmt, w, h);
        glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    makeTex(hdrTex_, GL_RGBA16F, inW_, inH_);
    makeTex(bloomA_, GL_RGBA16F, bloomW_, bloomH_);
    makeTex(bloomB_, GL_RGBA16F, bloomW_, bloomH_);
    glClearTexImage(hdrTex_, 0, GL_RGBA, GL_FLOAT, nullptr);

    glCreateFramebuffers(1, &hdrFbo_);
    glNamedFramebufferTexture(hdrFbo_, GL_COLOR_ATTACHMENT0, hdrTex_, 0);
    glNamedFramebufferDrawBuffer(hdrFbo_, GL_COLOR_ATTACHMENT0);
    if (glCheckNamedFramebufferStatus(hdrFbo_, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("HDR framebuffer incomplete");
        return false;
    }
    if (!cfg_.window) {
        makeTex(outTex_, GL_RGBA8, winW_, winH_);
        glCreateFramebuffers(1, &outFbo_);
        glNamedFramebufferTexture(outFbo_, GL_COLOR_ATTACHMENT0, outTex_, 0);
        glNamedFramebufferDrawBuffer(outFbo_, GL_COLOR_ATTACHMENT0);
    }
    accumValid_ = false;
    LOG_INFO("GL targets: internal %dx%d, window %dx%d", inW_, inH_, winW_, winH_);
    return true;
}

void GLRenderer::destroyTargets() {
    GLuint texs[] = {hdrTex_, bloomA_, bloomB_, outTex_};
    for (GLuint t : texs) if (t) glDeleteTextures(1, &t);
    if (hdrFbo_) glDeleteFramebuffers(1, &hdrFbo_);
    if (outFbo_) glDeleteFramebuffers(1, &outFbo_);
    hdrTex_ = bloomA_ = bloomB_ = outTex_ = hdrFbo_ = outFbo_ = 0;
}

void GLRenderer::resize(int windowW, int windowH, int internalW, int internalH) {
    if (windowW == winW_ && windowH == winH_ && internalW == inW_ && internalH == inH_) return;
    winW_ = std::max(windowW, 1); winH_ = std::max(windowH, 1);
    inW_ = internalW; inH_ = internalH;
    createTargets();
}

void GLRenderer::setParticleCount(uint32_t count) { particles_.resize(count); }

void GLRenderer::bloomPass(const FrameInput& frame) {
    glUseProgram(progBloom_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 9, uboBloom_);
    auto pass = [&](GLuint src, GLuint dst, float sx, float sy, float dirx, float diry, float mode) {
        BloomParams bp{};
        bp.texel[0] = sx; bp.texel[1] = sy; bp.texel[2] = (float)bloomW_; bp.texel[3] = (float)bloomH_;
        bp.dir[0] = dirx; bp.dir[1] = diry; bp.dir[2] = mode; bp.dir[3] = frame.render.bloomThreshold;
        glNamedBufferSubData(uboBloom_, 0, sizeof(bp), &bp);
        glBindTextureUnit(5, src);
        glBindImageTexture(2, dst, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glDispatchCompute((bloomW_ + 7) / 8, (bloomH_ + 7) / 8, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    };
    pass(hdrTex_, bloomA_, 1.f / inW_, 1.f / inH_, 0, 0, 0.f);
    pass(bloomA_, bloomB_, 1.f / bloomW_, 1.f / bloomH_, 1.f, 0.f, 1.f);
    pass(bloomB_, bloomA_, 1.f / bloomW_, 1.f / bloomH_, 0.f, 1.f, 1.f);
}

void GLRenderer::render(const FrameInput& frame, const std::function<void()>& debugUi) {
    RenderParams rp = frame.render;
    if (!accumValid_) rp.accumulate = 0;
    accumValid_ = true;
    glNamedBufferSubData(uboRender_, 0, sizeof(rp), &rp);
    glNamedBufferSubData(uboParticle_, 0, sizeof(frame.particles), &frame.particles);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboRender_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 4, uboParticle_);

#if BH_ENABLE_IMGUI
    if (imgui_) {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (debugUi) debugUi();
        ImGui::Render();
    }
#endif

    // 1. particles simulation (CUDA or GLSL)
    if (frame.clearParticles) particles_.reset();
    if (frame.simulateParticles) particles_.simulate(frame.particles, uboParticle_);

    // 2. ray march into the HDR image
    glUseProgram(progBlackhole_);
    glBindImageTexture(1, hdrTex_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
    glDispatchCompute((inW_ + 7) / 8, (inH_ + 7) / 8, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);

    // 3. particles, additively blended over the HDR image
    if (frame.drawParticles && particles_.count() > 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo_);
        glViewport(0, 0, inW_, inH_);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glUseProgram(progParticles_);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, particles_.ssbo());
        glBindVertexArray(vao_);
        glDrawArrays(GL_POINTS, 0, (GLsizei)particles_.count());
        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
    }

    // 4. bloom
    if (rp.bloomStrength > 0.f) bloomPass(frame);
    else glClearTexImage(bloomA_, 0, GL_RGBA, GL_FLOAT, nullptr);

    // 5. composite / tonemap to the window (or offscreen target)
    glBindFramebuffer(GL_FRAMEBUFFER, cfg_.window ? 0 : outFbo_);
    glViewport(0, 0, winW_, winH_);
    glUseProgram(progComposite_);
    glBindTextureUnit(5, hdrTex_);
    glBindTextureUnit(6, bloomA_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

#if BH_ENABLE_IMGUI
    if (imgui_) ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
    if (cfg_.window) glfwSwapBuffers(cfg_.window);
    else glFinish();
}

bool GLRenderer::readback(std::vector<uint8_t>& rgba, int& w, int& h) {
    w = winW_; h = winH_;
    rgba.resize((size_t)w * h * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, cfg_.window ? 0 : outFbo_);
    glReadBuffer(cfg_.window ? GL_FRONT : GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    image::flipVertical(rgba, w, h);
    return !gl::checkErrors("readback");
}

void GLRenderer::shutdown() {
#if BH_ENABLE_IMGUI
    if (imgui_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        imgui_ = false;
    }
#endif
    if (!glDeleteProgram) return;   // never initialised
    particles_.shutdown();
    destroyTargets();
    GLuint progs[] = {progBlackhole_, progBloom_, progComposite_, progParticles_};
    for (GLuint p : progs) if (p) glDeleteProgram(p);
    progBlackhole_ = progBloom_ = progComposite_ = progParticles_ = 0;
    GLuint bufs[] = {uboRender_, uboParticle_, uboBloom_};
    for (GLuint b : bufs) if (b) glDeleteBuffers(1, &b);
    uboRender_ = uboParticle_ = uboBloom_ = 0;
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
#if BH_ENABLE_HEADLESS && !defined(_WIN32)
    egl_.reset();
#endif
}

}  // namespace bh
