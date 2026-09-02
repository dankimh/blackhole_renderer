#include "render/Camera.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace bh {

void Camera::update(float dt, float orbitSpeedDegPerSec, float damp) {
    azimuthDeg += orbitSpeedDegPerSec * dt;
    if (azimuthDeg > 360.f) azimuthDeg -= 360.f;
    if (azimuthDeg < -360.f) azimuthDeg += 360.f;

    // Damped parallax, same feel as rain's animateParallax().
    float dx = targetYaw_ - curYaw_, dy = targetPitch_ - curPitch_;
    float speed = std::sqrt(dx * dx + dy * dy);
    float k = std::min(0.35f, damp + speed * 0.012f);
    k = 1.f - std::pow(1.f - k, dt * 60.f);   // frame-rate independent
    curYaw_ += dx * k;
    curPitch_ += dy * k;

    float az = glm::radians(azimuthDeg + curYaw_);
    float el = glm::radians(std::clamp(inclinationDeg + curPitch_, -89.f, 89.f));
    pos_ = distance * glm::vec3(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az));
    fwd_ = glm::normalize(-pos_);
    glm::vec3 worldUp(0.f, 1.f, 0.f);
    right_ = glm::normalize(glm::cross(fwd_, worldUp));
    up_ = glm::cross(right_, fwd_);
}

float Camera::tanHalfFov() const { return std::tan(glm::radians(fovDeg) * 0.5f); }

glm::vec3 Camera::rayDir(float nx, float ny) const {
    float t = tanHalfFov();
    float x = (nx * 2.f - 1.f) * t * aspect;
    float y = (1.f - ny * 2.f) * t;
    return glm::normalize(right_ * x + up_ * y + fwd_);
}

bool Camera::project(const glm::vec3& world, glm::vec2& ndc) const {
    glm::vec3 rel = world - pos_;
    float z = glm::dot(rel, fwd_);
    if (z <= 1e-4f) return false;
    float t = tanHalfFov();
    ndc = glm::vec2(glm::dot(rel, right_) / (z * t * aspect), glm::dot(rel, up_) / (z * t));
    return true;
}

}  // namespace bh
