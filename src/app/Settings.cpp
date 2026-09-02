#include "app/Settings.h"
#include "platform/Clock.h"
#include "util/File.h"
#include "util/Log.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace bh {
using json = nlohmann::json;

static float toF(const json& v, float def) {
    if (v.is_number()) return v.get<float>();
    if (v.is_boolean()) return v.get<bool>() ? 1.f : 0.f;
    if (v.is_string()) { try { return std::stof(v.get<std::string>()); } catch (...) {} }
    return def;
}
static int toI(const json& v, int def) {
    if (v.is_number()) return (int)std::lround(v.get<double>());
    if (v.is_boolean()) return v.get<bool>() ? 1 : 0;
    if (v.is_string()) { try { return std::stoi(v.get<std::string>()); } catch (...) {} }
    return def;
}
static bool toB(const json& v, bool def) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() != 0.0;
    if (v.is_string()) { auto s = v.get<std::string>(); return s == "true" || s == "1"; }
    return def;
}

bool SettingsStore::applyTo(Settings& s, const std::string& name, const json& v) {
#define F(field) if (name == #field) { s.field = toF(v, s.field); return true; }
#define I(field) if (name == #field) { s.field = toI(v, s.field); return true; }
#define B(field) if (name == #field) { s.field = toB(v, s.field); return true; }
    I(renderBackend) I(particleBackend) F(displayScaling) I(samples) I(maxSteps) F(stepSize)
    I(fpsLock) F(temporalBlend) B(bloomChk)
    F(diskBrightness) F(diskTemperature) F(diskInner) F(diskOuter) F(diskSpeed) F(diskOpacity)
    I(diskRotation) F(diskNoiseScale) F(dopplerStrength) F(redshiftStrength)
    F(starDensity) F(starBrightness) F(nebulaBrightness)
    F(cameraDistance) F(cameraInclination) F(cameraOrbitSpeed) F(fov) F(parallaxIntensity) F(parallaxDamp)
    F(exposure) F(bloomStrength) F(bloomThreshold)
    B(particlesChk) I(particleCount) F(particleEmitRate) F(particleSpeed) F(particleSize)
    F(particleBrightness) F(particleLife) F(particleSpawnRadius) F(particleOrbitBias) F(particleDrag)
    F(particleSpread) F(mouseIdleTimeout) F(lensStrength)
    B(globalMouse) B(debug)
#undef F
#undef I
#undef B
    return false;
}

bool SettingsStore::apply(const std::string& name, const json& value) {
    bool ok = applyTo(s, name, value);
    if (!ok) LOG_WARN("Unknown property '%s'", name.c_str());
    // sanity clamps
    s.samples = std::clamp(s.samples, 1, 256);
    s.maxSteps = std::clamp(s.maxSteps, 16, 8192);
    s.displayScaling = std::clamp(s.displayScaling, 0.1f, 8.f);
    s.particleCount = std::clamp(s.particleCount, 0, 8'000'000);
    s.diskInner = std::max(s.diskInner, 1.2f);
    s.diskOuter = std::max(s.diskOuter, s.diskInner + 0.5f);
    return ok;
}

bool SettingsStore::loadFile(const std::filesystem::path& path) {
    std::string text;
    if (!file::readText(path, text)) {
        LOG_WARN("Cannot read %s", path.string().c_str());
        return false;
    }
    json doc;
    try {
        doc = json::parse(text, nullptr, true, true);
    } catch (const std::exception& e) {
        LOG_ERROR("JSON parse error in %s: %s", path.string().c_str(), e.what());
        return false;
    }
    if (!doc.is_object()) return false;
    int n = 0;
    for (auto it = doc.begin(); it != doc.end(); ++it) {
        const json& prop = it.value();
        if (prop.is_object() && prop.contains("value")) {
            if (applyTo(s, it.key(), prop["value"])) ++n;
        } else if (!prop.is_object()) {   // also accept flat {"name": value}
            if (applyTo(s, it.key(), prop)) ++n;
        }
    }
    applyTo(s, "", json()); // no-op
    s.samples = std::clamp(s.samples, 1, 256);
    s.maxSteps = std::clamp(s.maxSteps, 16, 8192);
    s.displayScaling = std::clamp(s.displayScaling, 0.1f, 8.f);
    s.particleCount = std::clamp(s.particleCount, 0, 8000000);
    path_ = path;
    mtime_ = file::modifiedTime(path);
    LOG_INFO("Loaded %d properties from %s", n, path.string().c_str());
    return true;
}

bool SettingsStore::pollReload() {
    if (path_.empty()) return false;
    double now = Clock::now();
    if (now - lastCheck_ < 0.5) return false;
    lastCheck_ = now;
    int64_t m = file::modifiedTime(path_);
    if (m == 0 || m == mtime_) return false;
    LOG_INFO("Properties file changed - reloading");
    return loadFile(path_);
}

}  // namespace bh
