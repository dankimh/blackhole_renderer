#pragma once
#include "platform/Window.h"

namespace bh {

struct MouseState {
    double x = 0, y = 0;         // window pixel coordinates (origin top-left)
    double nx = 0.5, ny = 0.5;   // normalized 0..1
    bool inside = false;         // cursor over the window / screen
    double lastMoveTime = -1e9;  // Clock::now() seconds of the last movement
};

/// Mouse tracking. In wallpaper mode the window never receives real mouse
/// events (it lives behind the desktop), so `globalPoll` reads the OS cursor.
class Input {
public:
    void attach(Window& window);
    void update(bool globalPoll);
    const MouseState& mouse() const { return mouse_; }
    /// Cursor "presence": true if moved within `idleTimeout` seconds (0 = always).
    bool cursorActive(double idleTimeout) const;
    /// Headless / demo: fake a cursor at normalized (nx, ny).
    void setSynthetic(double nx, double ny);

private:
    bool pollGlobalCursor(double& sx, double& sy);
    Window* window_ = nullptr;
    MouseState mouse_;
    double lastX_ = -1, lastY_ = -1;
};

}  // namespace bh
