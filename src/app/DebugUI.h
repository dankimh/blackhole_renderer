#pragma once
// Dear ImGui panel == rain's datUI(): live tweaking without editing JSON.
#include "app/Settings.h"
#include <string>

namespace bh {

struct DebugStats {
    float fps = 0.f, frameMs = 0.f;
    int internalW = 0, internalH = 0;
    std::string backend, particleBackend, device;
    uint32_t aliveParticlesEstimate = 0;
    bool cursorActive = false;
    float mouseX = 0.f, mouseY = 0.f;
};

/// Draws the panel; returns true if any setting changed. `saveRequested` is set
/// when the user pressed "Save to LivelyProperties.json".
bool drawDebugUi(Settings& s, const DebugStats& st, bool& saveRequested, bool& resetParticles);

}  // namespace bh
