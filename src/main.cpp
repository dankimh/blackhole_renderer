// Black Hole live wallpaper - entry point.
// Structure follows rocksdanister/rain (index.html + js/script.js -> src/main.cpp + src/app/).
#include "app/Application.h"
#include "platform/Wallpaper.h"
#include "util/Log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "util/File.h"
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

using namespace bh;

static void usage() {
    std::printf(
        "blackhole_render %s - ray-marched black hole wallpaper (OpenGL compute / Vulkan / CUDA)\n\n"
        "Window modes:\n"
        "  --windowed              normal window (default)\n"
        "  --fullscreen            exclusive fullscreen\n"
        "  --wallpaper             embed behind desktop icons (Windows WorkerW / X11 desktop type)\n"
        "  --lively                launched by Lively Wallpaper (borderless, global cursor, stdin IPC)\n"
        "  --headless              no window: render N frames with EGL/Vulkan and write --out\n"
        "Rendering:\n"
        "  --backend gl|vk         render backend (default: LivelyProperties renderBackend)\n"
        "  --particles auto|cuda|compute\n"
        "  --width W --height H    window / headless size (default 1280x720)\n"
        "  --scale F               internal resolution scale override\n"
        "  --samples N --steps N   quality overrides\n"
        "  --frames N --out PATH   headless frame count / output image (png|jpg|bmp)\n"
        "  --dt SECONDS            fixed frame step (headless default 1/60; 0 = wall clock)\n"
        "  --gpu N                 GPU index (headless EGL / Vulkan)\n"
        "  --vsync                 enable vsync\n"
        "Integration:\n"
        "  --properties PATH       LivelyProperties.json to load (hot-reloaded on change)\n"
        "  --ipc / --no-ipc        Lively player protocol on stdin/stdout\n"
        "  --global-mouse 0|1      track the OS cursor even without focus\n"
        "  --debug                 show the Dear ImGui tuning panel (rain: datUI)\n"
        "  --sim-cursor            synthetic orbiting cursor (particle demo / headless)\n"
        "  --dump-desktop          print the desktop window tree (compare with a working Lively wallpaper)\n"
        "  --embed auto|plain|layered|bottom  --wallpaper attach method (bottom = z-order fallback)\n"
        "  --present auto|native|gdi|d3d  frame presentation (auto: d3d blt swapchain when embedded)\n"
        "  --set name=value        override any LivelyProperties.json value (repeatable)\n"
        "  --validation            GL debug output / Vulkan validation layers\n"
        "  --log debug|info|warn\n"
        "  --log-file PATH         mirror log to a file (default in --lively/--wallpaper: <exe dir>\\blackhole_render.log)\n"
        "Lively player arguments (--wallpaper-property, --wallpaper-geometry, ...) are accepted.\n",
        BH_VERSION);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    // Release builds use /SUBSYSTEM:WINDOWS; when started from a console, attach to it so
    // --help and log output are visible.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
#endif
    AppOptions o;
    std::string logFile;
    auto next = [&](int& i) -> std::string {
        if (i + 1 >= argc) { LOG_ERROR("Missing value for %s", argv[i]); std::exit(2); }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "--windowed") o.mode = WindowMode::Windowed;
        else if (a == "--fullscreen") o.mode = WindowMode::Fullscreen;
        else if (a == "--wallpaper") { o.mode = WindowMode::Wallpaper; o.globalMouseOverride = 1; }
        else if (a == "--lively") { o.mode = WindowMode::Lively; o.ipc = true; o.globalMouseOverride = 1; }
        else if (a == "--dump-desktop") { log::setLevel(log::Level::Info); bh::wallpaper::dumpDesktop(); return 0; }
        else if (a == "--headless") { o.mode = WindowMode::Headless; if (o.fixedDt <= 0.0) o.fixedDt = 1.0 / 60.0; }
        else if (a == "--dt") o.fixedDt = std::atof(next(i).c_str());
        else if (a == "--backend") {
            std::string v = next(i);
            o.backend = (v == "vk" || v == "vulkan") ? Backend::Vulkan : Backend::OpenGL;
            o.backendFromArgs = true;
        } else if (a == "--particles") {
            std::string v = next(i);
            o.particleBackend = v == "cuda" ? ParticleBackend::Cuda : v == "compute" ? ParticleBackend::Compute : ParticleBackend::Auto;
            o.particleBackendFromArgs = true;
        }
        else if (a == "--width") o.width = std::atoi(next(i).c_str());
        else if (a == "--height") o.height = std::atoi(next(i).c_str());
        else if (a == "--scale") o.scaleOverride = (float)std::atof(next(i).c_str());
        else if (a == "--samples") o.samplesOverride = std::atoi(next(i).c_str());
        else if (a == "--steps") o.stepsOverride = std::atoi(next(i).c_str());
        else if (a == "--frames") o.headlessFrames = std::atoi(next(i).c_str());
        else if (a == "--out") o.outputPath = next(i);
        else if (a == "--gpu") o.gpuIndex = std::atoi(next(i).c_str());
        else if (a == "--vsync") o.vsync = true;
        else if (a == "--properties" || a == "--wallpaper-property") o.propertiesPath = next(i);
        else if (a == "--ipc") o.ipc = true;
        else if (a == "--no-ipc") o.ipc = false;
        else if (a == "--global-mouse") o.globalMouseOverride = std::atoi(next(i).c_str());
        else if (a == "--debug") o.debugUi = true;
        else if (a == "--sim-cursor") o.simCursor = true;
        else if (a == "--present") { std::string v = next(i); o.presentMode = v == "native" ? 1 : v == "gdi" ? 2 : v == "d3d" ? 3 : 0; }
        else if (a == "--embed") { std::string v = next(i); o.embedMode = v == "plain" ? 1 : v == "layered" ? 2 : v == "bottom" ? 3 : 0; }
        else if (a == "--set") {
            std::string v = next(i);
            size_t eq = v.find('=');
            if (eq != std::string::npos) o.overrides.emplace_back(v.substr(0, eq), v.substr(eq + 1));
        }
        else if (a == "--validation") o.validation = true;
        else if (a == "--log-file") logFile = next(i);
        else if (a == "--log") {
            std::string v = next(i);
            log::setLevel(v == "debug" ? log::Level::Debug : v == "warn" ? log::Level::Warn : log::Level::Info);
        }
        else if (a == "--wallpaper-geometry") {
            std::string v = next(i);
            size_t x = v.find('x');
            if (x != std::string::npos) { o.width = std::atoi(v.substr(0, x).c_str()); o.height = std::atoi(v.substr(x + 1).c_str()); }
        }
        // Remaining Lively player args take one value and are ignored.
        else if (a.rfind("--wallpaper-", 0) == 0) { if (i + 1 < argc && argv[i + 1][0] != '-') ++i; }
        else { LOG_WARN("Unknown argument: %s", a.c_str()); }
    }
    if (o.width <= 0 || o.height <= 0) { o.width = 1280; o.height = 720; }
    if (logFile.empty() && (o.mode == WindowMode::Lively || o.mode == WindowMode::Wallpaper))
        logFile = (file::executableDir() / "blackhole_render.log").string();
    if (!logFile.empty() && log::setLogFile(logFile)) LOG_INFO("Logging to %s", logFile.c_str());
    LOG_INFO("blackhole_render %s starting (mode %d)", BH_VERSION, (int)o.mode);

    Application app;
    if (!app.init(o)) {
        app.shutdown();
        return 1;
    }
    int rc = app.run();
    app.shutdown();
    return rc;
}
