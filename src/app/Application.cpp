#include "app/Application.h"
#include "app/DebugUI.h"
#include "platform/Wallpaper.h"
#include "render/gl/GLRenderer.h"
#include "util/File.h"
#include "util/Image.h"
#include "util/Log.h"
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#if BH_ENABLE_VULKAN
#include "render/vk/VkRenderer.h"
#endif

namespace bh {
using json = nlohmann::json;

static std::unique_ptr<IRenderer> makeRenderer(Backend b) {
#if BH_ENABLE_VULKAN
    if (b == Backend::Vulkan) return std::make_unique<VkRenderer>();
#else
    if (b == Backend::Vulkan) LOG_WARN("Vulkan backend not built - using OpenGL");
#endif
    return std::make_unique<GLRenderer>();
}

// ---------------------------------------------------------------------------
// init()  (rain: init())
// ---------------------------------------------------------------------------
bool Application::init(const AppOptions& opts) {
    opts_ = opts;

    std::filesystem::path props = opts.propertiesPath.empty()
        ? file::resource("LivelyProperties.json") : std::filesystem::path(opts.propertiesPath);
    store_.loadFile(props);
    Settings& s = store_.s;
    if (opts.scaleOverride > 0.f) s.displayScaling = opts.scaleOverride;
    if (opts.samplesOverride > 0) s.samples = opts.samplesOverride;
    if (opts.stepsOverride > 0) s.maxSteps = opts.stepsOverride;
    if (opts.globalMouseOverride >= 0) s.globalMouse = opts.globalMouseOverride != 0;
    if (opts.debugUi) s.debug = true;
    for (const auto& kv : opts.overrides) {
        json v;
        try { v = json::parse(kv.second); } catch (...) { v = kv.second; }
        store_.apply(kv.first, v);
    }
    Backend backend = opts.backendFromArgs ? opts.backend
        : (s.renderBackend == 1 ? Backend::Vulkan : Backend::OpenGL);
#if !BH_ENABLE_VULKAN
    backend = Backend::OpenGL;
#endif
    ParticleBackend pb = opts.particleBackendFromArgs ? opts.particleBackend
        : (s.particleBackend == 1 ? ParticleBackend::Cuda
           : s.particleBackend == 2 ? ParticleBackend::Compute : ParticleBackend::Auto);

    bool headless = opts.mode == WindowMode::Headless;
#ifdef _WIN32
    // Windows 11 24H2+ desktop: GPU swapchains of Progman children are never composed; push
    // frames through UpdateLayeredWindow instead (auto in wallpaper/lively modes).
    bool embedded = opts.mode == WindowMode::Wallpaper || opts.mode == WindowMode::Lively;
    useD3d_ = opts.presentMode == 3 || (opts.presentMode == 0 && embedded);
    presentGdi_ = useD3d_ || opts.presentMode == 2;
#endif
    if (!headless) {
        WindowOptions wo;
        wo.width = opts.width; wo.height = opts.height;
        wo.openGL = backend != Backend::Vulkan;
        wo.title = "Black Hole Wallpaper";
        wo.fullscreen = opts.mode == WindowMode::Fullscreen;
        wo.borderless = opts.mode == WindowMode::Wallpaper || opts.mode == WindowMode::Lively;
        if (!window_.create(wo)) return false;
        if (wo.borderless) {
            int mw, mh;
            if (wallpaper::primaryMonitorSize(mw, mh)) {
                glfwSetWindowPos(window_.handle(), 0, 0);
                glfwSetWindowSize(window_.handle(), mw, mh);
            }
            wallpaper::hideFromTaskbar(window_);
        }
        if (opts.mode == WindowMode::Wallpaper) { wallpaper::setEmbedMode(opts.embedMode); wallpaper::embed(window_); }
        window_.framebufferSize(fbW_, fbH_);
        window_.onResize = [this](int w, int h) { resize(w, h); };
        input_.attach(window_);
    } else {
        fbW_ = opts.width; fbH_ = opts.height;
    }
    inW_ = std::max(8, (int)std::lround(fbW_ * s.displayScaling));
    inH_ = std::max(8, (int)std::lround(fbH_ * s.displayScaling));

    renderer_ = makeRenderer(backend);
    RendererConfig rc;
    rc.window = headless ? nullptr : window_.handle();
    rc.windowWidth = fbW_; rc.windowHeight = fbH_;
    rc.internalWidth = inW_; rc.internalHeight = inH_;
    rc.particleCount = s.particlesChk ? (uint32_t)s.particleCount : 0u;
    rc.particleBackend = pb;
    rc.vsync = opts.vsync;
    rc.debugUi = !headless;
    rc.offscreenPresent = presentGdi_;
    rc.validation = opts.validation;
    rc.gpuIndex = opts.gpuIndex;
    if (!renderer_->init(rc)) {
        LOG_ERROR("Renderer init failed (%s)", renderer_->name());
        return false;
    }
    LOG_INFO("Renderer: %s | particles: %s | %s", renderer_->name(), renderer_->particleBackendName(),
             renderer_->deviceName().c_str());
    if (useD3d_) {
        if (d3d_.init(window_.nativeHandle(), fbW_, fbH_)) LOG_INFO("Presenting through a D3D11 blt swapchain");
        else { LOG_WARN("D3D11 presenter unavailable - falling back to UpdateLayeredWindow"); useD3d_ = false; }
    }
    if (presentGdi_ && !useD3d_) {
        presenter_.init(window_.nativeHandle());
        LOG_INFO("Presenting through UpdateLayeredWindow (GDI layered path)");
    }

    camera_.aspect = (float)fbW_ / (float)fbH_;
    applySettings(true);

    ipcMode_ = opts.ipc;
    if (ipcMode_) {
        log::setLivelyConsole(true);
        ipc_.start();
        if (!headless) {
            LivelyIpc::sendHwnd((unsigned long long)(uintptr_t)window_.nativeHandle());
            LivelyIpc::sendWallpaperLoaded(true);
        }
    }
    lastFrameStart_ = Clock::now();
    return true;
}

// ---------------------------------------------------------------------------
void Application::resize(int fbw, int fbh) {
    if (fbw <= 0 || fbh <= 0) return;
    fbW_ = fbw; fbH_ = fbh;
    camera_.aspect = (float)fbW_ / (float)fbH_;
    inW_ = std::max(8, (int)std::lround(fbW_ * store_.s.displayScaling));
    inH_ = std::max(8, (int)std::lround(fbH_ * store_.s.displayScaling));
    renderer_->resize(fbW_, fbH_, inW_, inH_);
}

void Application::livelyPropertyListener(const std::string& name, const json& value) {
    store_.apply(name, value);
}

void Application::livelyWallpaperPlaybackChanged(bool paused) {
    paused_ = paused;
    clock_.setPaused(paused);
    LOG_INFO("Playback %s", paused ? "paused" : "resumed");
}

/// Push setting changes that need renderer work (resolution, particle count).
void Application::applySettings(bool force) {
    const Settings& s = store_.s;
    if (force || s.displayScaling != applied_.displayScaling) {
        inW_ = std::max(8, (int)std::lround(fbW_ * s.displayScaling));
        inH_ = std::max(8, (int)std::lround(fbH_ * s.displayScaling));
        renderer_->resize(fbW_, fbH_, inW_, inH_);
    }
    uint32_t want = s.particlesChk ? (uint32_t)s.particleCount : 0u;
    uint32_t had = applied_.particlesChk ? (uint32_t)applied_.particleCount : 0u;
    if (force || want != had) renderer_->setParticleCount(want);
    applied_ = s;
}

// ---------------------------------------------------------------------------
void Application::processIpc() {
    IpcMessage m;
    while (ipc_.poll(m)) {
        switch (m.type) {
            case IpcType::lp_slider: case IpcType::lp_dropdown: case IpcType::lp_chekbox:
            case IpcType::lp_textbox: case IpcType::lp_cpicker: case IpcType::lp_fdropdown:
            case IpcType::lp_dropdown_scaler:
                if (!m.name.empty()) livelyPropertyListener(m.name, m.value);
                break;
            case IpcType::lp_button:
                if (m.name == "resetParticles") resetParticles_ = true;
                else if (m.name == "saveProperties") saveProperties();
                break;
            case IpcType::cmd_close: case IpcType::terminate:
                LOG_INFO("IPC: close requested");
                quit_ = true;
                break;
            case IpcType::cmd_suspend: livelyWallpaperPlaybackChanged(true); break;
            case IpcType::cmd_resume:  livelyWallpaperPlaybackChanged(false); break;
            case IpcType::cmd_reload:
                if (!store_.path().empty()) store_.loadFile(store_.path());
                break;
            case IpcType::cmd_screenshot:
                if (m.raw.is_object() && m.raw.contains("FilePath") && m.raw["FilePath"].is_string())
                    pendingScreenshot_ = m.raw["FilePath"].get<std::string>();
                else pendingScreenshot_ = "screenshot.png";
                break;
            default: break;
        }
    }
}

void Application::limitFrameRate() {
    int fps = store_.s.fps();
    if (fps <= 0) { lastFrameStart_ = Clock::now(); return; }
    double target = 1.0 / fps;
    double next = lastFrameStart_ + target;
    double now = Clock::now();
    if (next - now > 0.003) std::this_thread::sleep_for(std::chrono::duration<double>(next - now - 0.002));
    while (Clock::now() < next) std::this_thread::yield();
    lastFrameStart_ = std::max(next, Clock::now() - target);   // avoid spiral after stalls
}

bool Application::saveScreenshot(const std::string& path) {
    std::vector<uint8_t> rgba;
    int w, h;
    if (!renderer_->readback(rgba, w, h)) return false;
    bool ok = image::write(path, rgba, w, h);
    LOG_INFO("Screenshot %s: %s (%dx%d)", ok ? "saved" : "FAILED", path.c_str(), w, h);
    if (ipcMode_) LivelyIpc::sendScreenshotDone(path, ok);
    return ok;
}

bool Application::saveProperties() {
    if (store_.path().empty()) return false;
    std::string text;
    if (!file::readText(store_.path(), text)) return false;
    json doc;
    try { doc = json::parse(text, nullptr, true, true); } catch (...) { return false; }
    const Settings& s = store_.s;
    auto set = [&](const char* k, json v) { if (doc.contains(k) && doc[k].is_object()) doc[k]["value"] = v; };
#define FS(f) set(#f, s.f);
    FS(renderBackend) FS(particleBackend) FS(displayScaling) FS(samples) FS(maxSteps) FS(stepSize)
    FS(fpsLock) FS(temporalBlend) FS(bloomChk) FS(diskBrightness) FS(diskTemperature) FS(diskInner)
    FS(diskOuter) FS(diskSpeed) FS(diskOpacity) FS(diskRotation) FS(diskNoiseScale) FS(dopplerStrength)
    FS(redshiftStrength) FS(starDensity) FS(starBrightness) FS(nebulaBrightness) FS(cameraDistance)
    FS(cameraInclination) FS(cameraOrbitSpeed) FS(fov) FS(parallaxIntensity) FS(parallaxDamp)
    FS(exposure) FS(bloomStrength) FS(bloomThreshold) FS(particlesChk) FS(particleCount)
    FS(particleEmitRate) FS(particleSpeed) FS(particleSize) FS(particleBrightness) FS(particleLife)
    FS(particleSpawnRadius) FS(particleOrbitBias) FS(particleDrag) FS(particleSpread)
    FS(mouseIdleTimeout) FS(lensStrength) FS(globalMouse) FS(debug)
#undef FS
    bool ok = file::writeText(store_.path(), doc.dump(2) + "\n");
    LOG_INFO("Properties %s: %s", ok ? "saved" : "save FAILED", store_.path().string().c_str());
    if (ok) store_.loadFile(store_.path());   // refresh mtime so we don't reload again
    return ok;
}

void Application::updateStats(double frameSeconds) {
    statAccum_ += frameSeconds;
    ++statFrames_;
    if (statAccum_ >= 0.5) {
        fps_ = statFrames_ / statAccum_;
        frameMs_ = statAccum_ / statFrames_ * 1000.0;
        statAccum_ = 0; statFrames_ = 0;
    }
}

// ---------------------------------------------------------------------------
// Per-frame parameter assembly
// ---------------------------------------------------------------------------
void Application::buildFrame(FrameInput& frame, double dt) {
    const Settings& s = store_.s;
    const MouseState& m = input_.mouse();

    // Camera: slow orbit + mouse parallax (rain: parallax on the container).
    camera_.distance = s.cameraDistance;
    camera_.inclinationDeg = s.cameraInclination;
    camera_.fovDeg = s.fov;
    float yaw = 0.f, pitch = 0.f;
    if (s.parallaxIntensity > 0.f && m.inside) {
        yaw = -(float)(m.nx - 0.5) * 2.f * s.parallaxIntensity * 4.f;
        pitch = (float)(m.ny - 0.5) * 2.f * s.parallaxIntensity * 3.f;
    }
    camera_.setParallaxTarget(yaw, pitch);
    camera_.update((float)dt, s.cameraOrbitSpeed, s.parallaxDamp);

    RenderParams& rp = frame.render;
    glm::vec3 r = camera_.right(), u = camera_.up(), f = camera_.forward(), p = camera_.position();
    std::memset(rp.camToWorld, 0, sizeof(rp.camToWorld));
    rp.camToWorld[0] = r.x; rp.camToWorld[1] = r.y; rp.camToWorld[2] = r.z;
    rp.camToWorld[4] = u.x; rp.camToWorld[5] = u.y; rp.camToWorld[6] = u.z;
    rp.camToWorld[8] = f.x; rp.camToWorld[9] = f.y; rp.camToWorld[10] = f.z;
    rp.camToWorld[15] = 1.f;
    rp.camPos[0] = p.x; rp.camPos[1] = p.y; rp.camPos[2] = p.z; rp.camPos[3] = camera_.aspect;
    rp.resolution[0] = (float)inW_; rp.resolution[1] = (float)inH_;
    rp.resolution[2] = 1.f / inW_; rp.resolution[3] = 1.f / inH_;
    bool cursorActive = input_.cursorActive(s.mouseIdleTimeout);
    rp.mouse[0] = (float)m.nx; rp.mouse[1] = (float)m.ny;
    rp.mouse[2] = cursorActive ? 1.f : 0.f;
    rp.mouse[3] = (float)(Clock::now() - m.lastMoveTime);
    glm::vec2 bh(0.f);
    camera_.project(glm::vec3(0.f), bh);
    rp.bhScreen[0] = bh.x; rp.bhScreen[1] = bh.y; rp.bhScreen[2] = camera_.distance; rp.bhScreen[3] = camera_.tanHalfFov();

    rp.time = (float)clock_.elapsed();
    rp.dt = (float)dt;
    rp.frame = frame_;
    rp.samples = (uint32_t)s.samples;
    rp.tanHalfFov = camera_.tanHalfFov();
    rp.rs = 1.f;
    rp.diskInner = s.diskInner; rp.diskOuter = s.diskOuter;
    rp.diskBrightness = s.diskBrightness; rp.diskTemperature = s.diskTemperature;
    rp.diskSpeed = s.diskSpeed; rp.diskThickness = 0.f;
    rp.maxSteps = s.maxSteps; rp.stepSize = s.stepSize;
    rp.farRadius = std::max(60.f, s.cameraDistance * 2.5f);
    rp.diskOpacity = s.diskOpacity;
    rp.starDensity = s.starDensity; rp.starBrightness = s.starBrightness; rp.nebulaBrightness = s.nebulaBrightness;
    rp.exposure = s.exposure;
    rp.dopplerStrength = s.dopplerStrength; rp.redshiftStrength = s.redshiftStrength;
    rp.seed = 0.f; rp.diskNoiseScale = s.diskNoiseScale;
    rp.accumulate = s.temporalBlend > 0.f ? 1u : 0u;
    rp.accumWeight = 1.f - std::clamp(s.temporalBlend, 0.f, 0.98f);
    rp.diskRotation = s.diskRotation == 0 ? 1.f : -1.f;
    rp.particleSize = s.particleSize; rp.particleBrightness = s.particleBrightness;
    rp.lensStrength = s.lensStrength;
    rp.bloomStrength = s.bloomChk ? s.bloomStrength : 0.f;
    rp.bloomThreshold = s.bloomThreshold;

    // Particles: cursor ray intersected with the plane through the hole facing the camera.
    ParticleParams& pp = frame.particles;
    glm::vec3 dir = camera_.rayDir((float)m.nx, (float)m.ny);
    float denom = glm::dot(dir, f);
    float t = denom > 1e-4f ? -glm::dot(p, f) / denom : camera_.distance;
    glm::vec3 spawn = p + dir * t;
    bool emit = s.particlesChk && cursorActive && s.particleEmitRate > 0.f;
    pp.spawnPos[0] = spawn.x; pp.spawnPos[1] = spawn.y; pp.spawnPos[2] = spawn.z; pp.spawnPos[3] = emit ? 1.f : 0.f;
    pp.spawnU[0] = r.x; pp.spawnU[1] = r.y; pp.spawnU[2] = r.z; pp.spawnU[3] = s.particleSpawnRadius;
    pp.spawnV[0] = u.x; pp.spawnV[1] = u.y; pp.spawnV[2] = u.z; pp.spawnV[3] = s.particleSpread;
    pp.planeNormal[0] = f.x; pp.planeNormal[1] = f.y; pp.planeNormal[2] = f.z; pp.planeNormal[3] = 0.f;
    pp.dt = (float)dt * s.particleSpeed;
    pp.mass = 0.5f; pp.rs = 1.f; pp.drag = s.particleDrag;
    pp.frame = frame_;
    pp.count = s.particlesChk ? (uint32_t)s.particleCount : 0u;
    pp.spawnProb = pp.count ? std::min(1.f, (float)(s.particleEmitRate * dt) / (float)pp.count) : 0.f;
    pp.maxLife = std::max(0.5f, s.particleLife) * s.particleSpeed;   // life measured in sim time
    pp.orbitBias = s.particleOrbitBias;

    frame.simulateParticles = s.particlesChk && dt > 0.0;
    frame.drawParticles = s.particlesChk;
    frame.clearParticles = resetParticles_;
    resetParticles_ = false;
}

bool Application::renderFrame() {
    double now = Clock::now();
    double dt = opts_.fixedDt > 0.0 ? clock_.tickFixed(opts_.fixedDt) : clock_.tick();
    if (opts_.simCursor) {
        double t = clock_.elapsed() * 0.6;
        input_.setSynthetic(0.5 + 0.32 * std::cos(t), 0.5 + 0.28 * std::sin(t * 1.3));
    } else if (opts_.mode != WindowMode::Headless) {
        input_.update(store_.s.globalMouse);
    }

    FrameInput frame;
    buildFrame(frame, dt);

    std::function<void()> ui;
    if (store_.s.debug && opts_.mode != WindowMode::Headless) {
        ui = [this] {
            DebugStats st;
            st.fps = (float)fps_; st.frameMs = (float)frameMs_;
            st.internalW = inW_; st.internalH = inH_;
            st.backend = renderer_->name(); st.particleBackend = renderer_->particleBackendName();
            st.device = renderer_->deviceName();
            st.cursorActive = input_.cursorActive(store_.s.mouseIdleTimeout);
            st.mouseX = (float)input_.mouse().x; st.mouseY = (float)input_.mouse().y;
            bool save = false, reset = false;
            drawDebugUi(store_.s, st, save, reset);
            if (save) saveProperties();
            if (reset) resetParticles_ = true;
        };
    }
    renderer_->render(frame, ui);
    ++frame_;
    if (presentGdi_) {
        std::vector<uint8_t> bgra;
        int w, h;
        bool topDown;
        if (renderer_->readbackBGRA(bgra, w, h, topDown)) {
            if (useD3d_) d3d_.present(bgra.data(), w, h, topDown);
            else presenter_.present(bgra.data(), w, h, topDown);
        }
    }

    if (!pendingScreenshot_.empty()) {
        saveScreenshot(pendingScreenshot_);
        pendingScreenshot_.clear();
    }
    updateStats(Clock::now() - now);
    return true;
}

// ---------------------------------------------------------------------------
// run()  (rain: render() loop with fps lock)
// ---------------------------------------------------------------------------
int Application::run() {
    bool headless = opts_.mode == WindowMode::Headless;
    int rendered = 0;
    double headlessStart = Clock::now();
    while (!quit_) {
        if (!headless) {
            window_.pollEvents();
            if (window_.shouldClose()) { LOG_INFO("Window close requested"); break; }
        }
        if (ipcMode_) processIpc();
        applySettings(false);
        store_.pollReload();
        if (opts_.mode == WindowMode::Wallpaper && (frame_ % 120) == 0) wallpaper::maintain(window_);

        if (paused_ && !headless) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (!headless) limitFrameRate();

        renderFrame();

        if (headless) {
            ++rendered;
            if (rendered % 10 == 0 || rendered == opts_.headlessFrames)
                LOG_INFO("headless frame %d/%d (%.2f ms/frame avg)", rendered, opts_.headlessFrames,
                         (Clock::now() - headlessStart) * 1000.0 / rendered);
            if (rendered >= opts_.headlessFrames) {
                saveScreenshot(opts_.outputPath);
                break;
            }
        }
    }
    return 0;
}

void Application::shutdown() {
    ipc_.stop();
    if (renderer_) { renderer_->shutdown(); renderer_.reset(); }
    window_.destroy();
}

}  // namespace bh
