#pragma once
#include <chrono>

namespace bh {
/// Wall clock with pause support and periodic reset (rain resets every 6 h to
/// keep float precision in shaders).
class Clock {
public:
    using clock = std::chrono::steady_clock;
    static constexpr double kResetSeconds = 21600.0;

    Clock() : last_(clock::now()) {}

    /// Advance; returns frame delta in seconds (0 while paused, clamped to 0.1).
    double tick() {
        auto now = clock::now();
        double dt = std::chrono::duration<double>(now - last_).count();
        last_ = now;
        if (paused_) return 0.0;
        if (dt > 0.1) dt = 0.1;
        elapsed_ += dt;
        if (elapsed_ > kResetSeconds) elapsed_ = 0.0;
        return dt;
    }
    /// Advance by a fixed step (headless / deterministic rendering).
    double tickFixed(double dt) {
        last_ = clock::now();
        if (paused_) return 0.0;
        elapsed_ += dt;
        if (elapsed_ > kResetSeconds) elapsed_ = 0.0;
        return dt;
    }
    double elapsed() const { return elapsed_; }
    void setPaused(bool p) { paused_ = p; }
    bool paused() const { return paused_; }
    static double now() { return std::chrono::duration<double>(clock::now().time_since_epoch()).count(); }

private:
    clock::time_point last_;
    double elapsed_ = 0.0;
    bool paused_ = false;
};
}  // namespace bh
