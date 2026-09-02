#include "platform/Window.h"
#include "util/Log.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <GLFW/glfw3native.h>
#elif defined(BH_HAS_X11)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#endif

namespace bh {

static void glfwErrorCb(int code, const char* desc) { LOG_ERROR("GLFW error %d: %s", code, desc); }

Window::~Window() { destroy(); }

bool Window::create(const WindowOptions& opts) {
    glfwSetErrorCallback(glfwErrorCb);
    if (!glfwInit()) {
        LOG_ERROR("glfwInit failed");
        return false;
    }
    openGL_ = opts.openGL;
    if (opts.openGL) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    } else {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    glfwWindowHint(GLFW_DECORATED, opts.borderless ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, opts.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);

    GLFWmonitor* mon = nullptr;
    int w = opts.width, h = opts.height;
    if (opts.fullscreen) {
        mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        w = mode->width; h = mode->height;
        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
    }
    win_ = glfwCreateWindow(w, h, opts.title.c_str(), mon, nullptr);
    if (!win_) {
        LOG_ERROR("glfwCreateWindow failed");
        return false;
    }
    glfwSetWindowUserPointer(win_, this);
    glfwSetFramebufferSizeCallback(win_, [](GLFWwindow* g, int fw, int fh) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(g));
        if (self && self->onResize) self->onResize(fw, fh);
    });
    if (opts.openGL) {
        glfwMakeContextCurrent(win_);
        glfwSwapInterval(0);
    }
    return true;
}

void Window::destroy() {
    if (win_) {
        glfwDestroyWindow(win_);
        win_ = nullptr;
        glfwTerminate();
    }
}

void Window::pollEvents() { glfwPollEvents(); }
bool Window::shouldClose() const { return win_ && glfwWindowShouldClose(win_); }
void Window::requestClose() { if (win_) glfwSetWindowShouldClose(win_, GLFW_TRUE); }
void Window::framebufferSize(int& w, int& h) const { glfwGetFramebufferSize(win_, &w, &h); }
void Window::position(int& x, int& y) const { glfwGetWindowPos(win_, &x, &y); }
void Window::swapBuffers() { if (openGL_) glfwSwapBuffers(win_); }
void Window::makeContextCurrent() { if (openGL_) glfwMakeContextCurrent(win_); }
void Window::setVsync(bool on) { if (openGL_) glfwSwapInterval(on ? 1 : 0); }

void* Window::nativeHandle() const {
    if (!win_) return nullptr;
#ifdef _WIN32
    return (void*)glfwGetWin32Window(win_);
#elif defined(BH_HAS_X11)
    return (void*)(uintptr_t)glfwGetX11Window(win_);
#else
    return nullptr;
#endif
}

}  // namespace bh
