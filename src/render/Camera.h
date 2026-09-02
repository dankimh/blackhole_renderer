#pragma once
#include <glm/glm.hpp>

namespace bh {

/// Orbit camera around the black hole (origin). Disk lies in the y = 0 plane.
class Camera {
public:
    float distance = 22.f;      // rs units
    float inclinationDeg = 8.f; // elevation above the disk plane
    float azimuthDeg = 0.f;
    float fovDeg = 55.f;
    float aspect = 16.f / 9.f;

    // Mouse parallax (like rain.js parallax): target/current offsets in degrees.
    void setParallaxTarget(float yawDeg, float pitchDeg) { targetYaw_ = yawDeg; targetPitch_ = pitchDeg; }
    void update(float dt, float orbitSpeedDegPerSec, float damp);

    glm::vec3 position() const { return pos_; }
    glm::vec3 right() const { return right_; }
    glm::vec3 up() const { return up_; }
    glm::vec3 forward() const { return fwd_; }
    float tanHalfFov() const;

    /// Ray direction through normalized screen coordinate (x right, y down, 0..1).
    glm::vec3 rayDir(float nx, float ny) const;
    /// Screen position (ndc, y up) of a world point; returns false if behind the camera.
    bool project(const glm::vec3& world, glm::vec2& ndc) const;

private:
    float targetYaw_ = 0.f, targetPitch_ = 0.f, curYaw_ = 0.f, curPitch_ = 0.f;
    glm::vec3 pos_{0.f}, right_{1, 0, 0}, up_{0, 1, 0}, fwd_{0, 0, -1};
};

}  // namespace bh
