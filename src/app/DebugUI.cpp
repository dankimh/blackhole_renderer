#include "app/DebugUI.h"
#if BH_ENABLE_IMGUI
#include <imgui.h>
#endif

namespace bh {

bool drawDebugUi(Settings& s, const DebugStats& st, bool& saveRequested, bool& resetParticles) {
    saveRequested = false;
    resetParticles = false;
#if BH_ENABLE_IMGUI
    bool changed = false;
    ImGui::SetNextWindowSize(ImVec2(360, 640), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::Begin("Black Hole (debug)");
    ImGui::Text("%s | %s particles", st.backend.c_str(), st.particleBackend.c_str());
    ImGui::TextWrapped("%s", st.device.c_str());
    ImGui::Text("%.1f fps  %.2f ms  render %dx%d", st.fps, st.frameMs, st.internalW, st.internalH);
    ImGui::Text("cursor %s (%.0f, %.0f)", st.cursorActive ? "active" : "idle", st.mouseX, st.mouseY);

    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Render scale", &s.displayScaling, 0.1f, 4.f, "%.2f");
        changed |= ImGui::SliderInt("Samples / px", &s.samples, 1, 64);
        changed |= ImGui::SliderInt("Ray steps", &s.maxSteps, 32, 4096);
        changed |= ImGui::SliderFloat("Step size", &s.stepSize, 0.05f, 1.f);
        changed |= ImGui::Combo("FPS", &s.fpsLock, "Unlimited\0" "30\0" "60\0" "120\0" "144\0");
        changed |= ImGui::SliderFloat("Temporal blend", &s.temporalBlend, 0.f, 0.97f);
        changed |= ImGui::Checkbox("Bloom", &s.bloomChk);
    }
    if (ImGui::CollapsingHeader("Accretion disk", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Brightness", &s.diskBrightness, 0.f, 4.f);
        changed |= ImGui::SliderFloat("Temperature K", &s.diskTemperature, 2000.f, 30000.f, "%.0f");
        changed |= ImGui::SliderFloat("Inner radius", &s.diskInner, 1.5f, 8.f);
        changed |= ImGui::SliderFloat("Outer radius", &s.diskOuter, 5.f, 40.f);
        changed |= ImGui::SliderFloat("Rotation speed", &s.diskSpeed, 0.f, 5.f);
        changed |= ImGui::SliderFloat("Opacity", &s.diskOpacity, 0.f, 1.f);
        changed |= ImGui::Combo("Direction", &s.diskRotation, "Counter-clockwise\0Clockwise\0");
        changed |= ImGui::SliderFloat("Noise scale", &s.diskNoiseScale, 0.2f, 4.f);
        changed |= ImGui::SliderFloat("Doppler", &s.dopplerStrength, 0.f, 1.f);
        changed |= ImGui::SliderFloat("Redshift", &s.redshiftStrength, 0.f, 1.f);
    }
    if (ImGui::CollapsingHeader("Sky")) {
        changed |= ImGui::SliderFloat("Star density", &s.starDensity, 0.f, 1.f);
        changed |= ImGui::SliderFloat("Star brightness", &s.starBrightness, 0.f, 4.f);
        changed |= ImGui::SliderFloat("Nebula", &s.nebulaBrightness, 0.f, 4.f);
    }
    if (ImGui::CollapsingHeader("Camera")) {
        changed |= ImGui::SliderFloat("Distance", &s.cameraDistance, 6.f, 80.f);
        changed |= ImGui::SliderFloat("Inclination", &s.cameraInclination, -80.f, 80.f);
        changed |= ImGui::SliderFloat("Orbit deg/s", &s.cameraOrbitSpeed, -5.f, 5.f);
        changed |= ImGui::SliderFloat("FOV", &s.fov, 15.f, 120.f);
        changed |= ImGui::SliderFloat("Parallax", &s.parallaxIntensity, 0.f, 5.f);
        changed |= ImGui::SliderFloat("Parallax damp", &s.parallaxDamp, 0.01f, 1.f);
    }
    if (ImGui::CollapsingHeader("Post")) {
        changed |= ImGui::SliderFloat("Exposure", &s.exposure, 0.05f, 6.f);
        changed |= ImGui::SliderFloat("Bloom strength", &s.bloomStrength, 0.f, 3.f);
        changed |= ImGui::SliderFloat("Bloom threshold", &s.bloomThreshold, 0.f, 4.f);
    }
    if (ImGui::CollapsingHeader("Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Enabled", &s.particlesChk);
        changed |= ImGui::SliderInt("Count", &s.particleCount, 0, 4000000);
        changed |= ImGui::SliderFloat("Emit / s", &s.particleEmitRate, 0.f, 400000.f, "%.0f");
        changed |= ImGui::SliderFloat("Speed", &s.particleSpeed, 0.1f, 40.f);
        changed |= ImGui::SliderFloat("Size", &s.particleSize, 0.2f, 10.f);
        changed |= ImGui::SliderFloat("Brightness##p", &s.particleBrightness, 0.f, 8.f);
        changed |= ImGui::SliderFloat("Life s", &s.particleLife, 1.f, 60.f);
        changed |= ImGui::SliderFloat("Spawn radius", &s.particleSpawnRadius, 0.05f, 6.f);
        changed |= ImGui::SliderFloat("Orbit bias", &s.particleOrbitBias, 0.f, 1.5f);
        changed |= ImGui::SliderFloat("Drag", &s.particleDrag, 0.f, 1.f);
        changed |= ImGui::SliderFloat("Spread", &s.particleSpread, 0.f, 1.f);
        changed |= ImGui::SliderFloat("Idle timeout s", &s.mouseIdleTimeout, 0.f, 30.f);
        changed |= ImGui::SliderFloat("Lens strength", &s.lensStrength, 0.f, 3.f);
        if (ImGui::Button("Reset particles")) resetParticles = true;
    }
    if (ImGui::CollapsingHeader("Misc")) {
        changed |= ImGui::Checkbox("Global cursor tracking", &s.globalMouse);
    }
    ImGui::Separator();
    if (ImGui::Button("Save to LivelyProperties.json")) saveRequested = true;
    ImGui::SameLine();
    if (ImGui::Button("Hide (debug=false)")) { s.debug = false; changed = true; }
    ImGui::End();
    return changed;
#else
    (void)s; (void)st;
    return false;
#endif
}

}  // namespace bh
