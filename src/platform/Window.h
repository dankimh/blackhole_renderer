#pragma once
#include <functional>
#include <string>

struct GLFWwindow;

namespace bh {

struct WindowOptions {
    int width = 1280, height = 720;
    std::string title = "Black Hole";
    bool openGL = true;        // false => GLFW_NO_API (Vulkan)
    bool borderless = false;
    bool fullscreen = false;
    bool visible = true;
    bool alwaysOnBottom = false;
    /// Presentation window without a GL pixel format (GLFW_NO_API); the GL context lives
    /// on a hidden helper window instead. Needed for layered desktop children on Windows.
    bool separateContext = false;
};

/// Thin GLFW wrapper. One window per process.
class Window {
public:
    ~Window();
    bool create(const WindowOptions& opts);
    void destroy();
    void pollEvents();
    bool shouldClose() const;
    void requestClose();
    void framebufferSize(int& w, int& h) const;
    void position(int& x, int& y) const;
    void swapBuffers();
    void makeContextCurrent();
    void setVsync(bool on);
    GLFWwindow* handle() const { return win_; }
    /// Window that owns the GL context (== handle() unless separateContext).
    GLFWwindow* contextHandle() const { return ctx_ ? ctx_ : win_; }
    /// Native handle: HWND on Windows, X11 Window id on Linux (0 if unavailable).
    void* nativeHandle() const;
    std::function<void(int, int)> onResize;

private:
    GLFWwindow* win_ = nullptr;
    GLFWwindow* ctx_ = nullptr;
    bool openGL_ = true;
};

}  // namespace bh
