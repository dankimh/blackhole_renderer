#include "platform/Input.h"
#include "platform/Clock.h"
#include "util/Log.h"
#include <GLFW/glfw3.h>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#elif defined(BH_HAS_X11)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#endif

namespace bh {

void Input::attach(Window& window) { window_ = &window; }

bool Input::pollGlobalCursor(double& sx, double& sy) {
#ifdef _WIN32
    POINT p;
    if (!GetCursorPos(&p)) return false;
    sx = p.x; sy = p.y;
    return true;
#elif defined(BH_HAS_X11)
    Display* dpy = glfwGetX11Display();
    if (!dpy) return false;
    ::Window root = DefaultRootWindow(dpy), child;
    int rx, ry, wx, wy;
    unsigned int mask;
    if (!XQueryPointer(dpy, root, &root, &child, &rx, &ry, &wx, &wy, &mask)) return false;
    sx = rx; sy = ry;
    return true;
#else
    (void)sx; (void)sy;
    return false;
#endif
}

void Input::update(bool globalPoll) {
    if (!window_ || !window_->handle()) return;
    int fw, fh;
    window_->framebufferSize(fw, fh);
    double x, y;
    bool inside;
    if (globalPoll && pollGlobalCursor(x, y)) {
        int wx, wy;
        window_->position(wx, wy);
        x -= wx; y -= wy;
        inside = x >= 0 && y >= 0 && x < fw && y < fh;
    } else {
        glfwGetCursorPos(window_->handle(), &x, &y);
        inside = glfwGetWindowAttrib(window_->handle(), GLFW_HOVERED) != 0 &&
                 x >= 0 && y >= 0 && x < fw && y < fh;
    }
    if (std::fabs(x - lastX_) > 0.5 || std::fabs(y - lastY_) > 0.5) {
        mouse_.lastMoveTime = Clock::now();
        lastX_ = x; lastY_ = y;
    }
    mouse_.x = x; mouse_.y = y;
    mouse_.nx = fw > 0 ? x / fw : 0.5;
    mouse_.ny = fh > 0 ? y / fh : 0.5;
    mouse_.inside = inside;
}

void Input::setSynthetic(double nx, double ny) {
    mouse_.nx = nx; mouse_.ny = ny;
    mouse_.x = nx * 1000.0; mouse_.y = ny * 1000.0;
    mouse_.inside = true;
    mouse_.lastMoveTime = Clock::now();
}

bool Input::cursorActive(double idleTimeout) const {
    if (!mouse_.inside) return false;
    if (idleTimeout <= 0.0) return true;
    return (Clock::now() - mouse_.lastMoveTime) < idleTimeout;
}

}  // namespace bh
