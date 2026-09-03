#pragma once
// Application == rain's script.js: init() / resize() / render() loop,
// livelyPropertyListener(), livelyWallpaperPlaybackChanged(), datUI().
#include "app/LivelyIpc.h"
#include "app/Settings.h"
#include "platform/Clock.h"
#include "platform/Input.h"
#include "platform/LayeredPresenter.h"
#include "platform/D3DPresenter.h"
#include "platform/DCompPresenter.h"
#include "platform/Window.h"
#include "render/Camera.h"
#include "render/IRenderer.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bh {

enum class WindowMode { Windowed, Fullscreen, Wallpaper, Lively, Headless };

struct AppOptions {
    WindowMode mode = WindowMode::Windowed;
    Backend backend = Backend::OpenGL;
    bool backendFromArgs = false;
    ParticleBackend particleBackend = ParticleBackend::Auto;
    bool particleBackendFromArgs = false;
    int width = 1280, height = 720;
    int headlessFrames = 60;
    std::string outputPath = "blackhole.png";
    std::string propertiesPath;          // LivelyProperties.json (default: next to exe)
    bool ipc = false;                    // read Lively messages from stdin
    bool debugUi = false;
    bool validation = false;
    bool vsync = false;
    int gpuIndex = 0;
    float scaleOverride = 0.f;           // --scale
    int samplesOverride = 0, stepsOverride = 0;
    int globalMouseOverride = -1;        // -1 keep setting
    int embedMode = 0;                   // --embed auto|plain|layered
    int presentMode = 0;                 // --present auto|native|gdi|d3d|blt|dcomp (auto = dcomp when embedded)
    bool innerChild = false;             // --inner-child: present into a plain child window (Lively layout)
    bool simCursor = false;              // synthetic orbiting cursor (headless demos)
    double fixedDt = 0.0;                // > 0: deterministic frame step (headless default 1/60)
    std::vector<std::pair<std::string, std::string>> overrides;   // --set name=value
};

class Application {
public:
    bool init(const AppOptions& opts);
    int run();
    void shutdown();

    /// Lively API twins
    void livelyPropertyListener(const std::string& name, const nlohmann::json& value);
    void livelyWallpaperPlaybackChanged(bool paused);

private:
    void resize(int fbw, int fbh);
    void applySettings(bool force);
    void processIpc();
    void limitFrameRate();
    bool renderFrame();
    void buildFrame(FrameInput& frame, double dt);
    bool saveScreenshot(const std::string& path);
    bool saveProperties();
    void updateStats(double frameSeconds);

    AppOptions opts_;
    SettingsStore store_;
    Settings applied_;
    Window window_;
    Input input_;
    Clock clock_;
    Camera camera_;
    LivelyIpc ipc_;
    LayeredPresenter presenter_;
    D3DPresenter d3d_;
    DCompPresenter dcomp_;
    bool useDComp_ = false;
    bool presentGdi_ = false;    // any offscreen presenter active
    bool useD3d_ = false;
    bool useBlt_ = false;
    void* presentHwnd_ = nullptr;
    std::unique_ptr<IRenderer> renderer_;
    int fbW_ = 0, fbH_ = 0, inW_ = 0, inH_ = 0;
    uint32_t frame_ = 0;
    bool quit_ = false;
    bool paused_ = false;
    bool resetParticles_ = false;
    bool ipcMode_ = false;
    double lastFrameStart_ = 0;
    double fps_ = 0, frameMs_ = 0;
    double statAccum_ = 0;
    int statFrames_ = 0;
    std::string pendingScreenshot_;
};

}  // namespace bh
