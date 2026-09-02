#pragma once
// Wallpaper settings. Field names == LivelyProperties.json keys. The
// `SettingsStore::apply()` switch is the C++ twin of rain's livelyPropertyListener().
#include <nlohmann/json_fwd.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace bh {

struct Settings {
    // Performance
    int   renderBackend = 0;        // 0 OpenGL, 1 Vulkan (restart)
    int   particleBackend = 0;      // 0 Auto, 1 CUDA, 2 Compute shader (restart)
    float displayScaling = 1.0f;    // internal resolution scale (rain: displayScaling)
    int   samples = 2;              // sub-samples per pixel per frame
    int   maxSteps = 400;
    float stepSize = 0.35f;
    int   fpsLock = 2;              // dropdown index: Unlimited/30/60/120/144
    float temporalBlend = 0.0f;     // 0 = off, else weight of history
    bool  bloomChk = true;

    // Black hole & disk
    float diskBrightness = 1.0f;
    float diskTemperature = 6500.f;
    float diskInner = 3.0f;
    float diskOuter = 14.0f;
    float diskSpeed = 1.0f;
    float diskOpacity = 0.85f;
    int   diskRotation = 0;         // 0 ccw, 1 cw
    float diskNoiseScale = 1.0f;
    float dopplerStrength = 1.0f;
    float redshiftStrength = 1.0f;

    // Sky
    float starDensity = 0.5f;
    float starBrightness = 1.0f;
    float nebulaBrightness = 1.0f;

    // Camera
    float cameraDistance = 22.f;
    float cameraInclination = 8.f;
    float cameraOrbitSpeed = 0.25f; // deg / s
    float fov = 55.f;
    float parallaxIntensity = 1.0f; // rain: parallaxIntensity
    float parallaxDamp = 0.08f;     // rain: parallaxDamp

    // Post
    float exposure = 1.2f;
    float bloomStrength = 0.6f;
    float bloomThreshold = 0.9f;

    // Particles (cursor -> black hole)
    bool  particlesChk = true;
    int   particleCount = 200000;
    float particleEmitRate = 20000.f;   // particles / s while the cursor is active
    float particleSpeed = 8.f;          // simulation time scale
    float particleSize = 2.f;
    float particleBrightness = 1.f;
    float particleLife = 12.f;          // s
    float particleSpawnRadius = 1.f;    // rs
    float particleOrbitBias = 1.f;      // 0 radial plunge .. 1 circular
    float particleDrag = 0.05f;
    float particleSpread = 0.3f;
    float mouseIdleTimeout = 4.f;       // s, 0 = emit forever
    float lensStrength = 1.f;

    // Misc
    bool  globalMouse = true;
    bool  debug = false;

    int fps() const {
        static const int table[] = {0, 30, 60, 120, 144};
        return (fpsLock >= 0 && fpsLock < 5) ? table[fpsLock] : 60;
    }
};

class SettingsStore {
public:
    Settings s;

    /// Load LivelyProperties.json (each key's "value" is applied).
    bool loadFile(const std::filesystem::path& path);
    /// livelyPropertyListener(name, val). Returns false for unknown names.
    bool apply(const std::string& name, const nlohmann::json& value);
    /// Re-read the file when its mtime changes (checked at most every 0.5 s).
    bool pollReload();
    const std::filesystem::path& path() const { return path_; }
    static bool applyTo(Settings& s, const std::string& name, const nlohmann::json& value);

private:
    std::filesystem::path path_;
    int64_t mtime_ = 0;
    double lastCheck_ = 0;
};

}  // namespace bh
