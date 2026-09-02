#include "platform/Wallpaper.h"
#include "util/Log.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <windows.h>
#elif defined(BH_HAS_X11)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

namespace bh::wallpaper {

#ifdef _WIN32
static HWND findWorkerW() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return nullptr;
    // Ask Progman to spawn a WorkerW behind the desktop icons (undocumented 0x052C).
    DWORD_PTR result = 0;
    SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &result);
    HWND workerw = nullptr;
    struct Ctx { HWND* out; } ctx{&workerw};
    EnumWindows([](HWND top, LPARAM lp) -> BOOL {
        HWND shell = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
        if (shell) {
            *reinterpret_cast<Ctx*>(lp)->out = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    // Windows 11 24H2+: WorkerW may be a child of Progman instead.
    if (!workerw) workerw = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
    return workerw ? workerw : progman;
}
#endif

bool embed(Window& window) {
#ifdef _WIN32
    HWND hwnd = (HWND)window.nativeHandle();
    if (!hwnd) return false;
    HWND parent = findWorkerW();
    if (!parent) {
        LOG_ERROR("WorkerW not found - cannot embed as wallpaper");
        return false;
    }
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    style |= WS_CHILD;
    SetWindowLongW(hwnd, GWL_STYLE, style);
    SetParent(hwnd, parent);
    RECT rc;
    GetClientRect(parent, &rc);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    LOG_INFO("Embedded window behind desktop icons (%ldx%ld)", rc.right - rc.left, rc.bottom - rc.top);
    return true;
#elif defined(BH_HAS_X11)
    Display* dpy = glfwGetX11Display();
    ::Window xw = glfwGetX11Window(window.handle());
    if (!dpy || !xw) return false;
    Atom type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom desktop = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XChangeProperty(dpy, xw, type, XA_ATOM, 32, PropModeReplace, (unsigned char*)&desktop, 1);
    Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom below = XInternAtom(dpy, "_NET_WM_STATE_BELOW", False);
    Atom skipTb = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom skipPg = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
    Atom states[3] = {below, skipTb, skipPg};
    XChangeProperty(dpy, xw, state, XA_ATOM, 32, PropModeReplace, (unsigned char*)states, 3);
    XLowerWindow(dpy, xw);
    XFlush(dpy);
    LOG_INFO("X11 window marked as desktop-type (behind other windows)");
    return true;
#else
    (void)window;
    return false;
#endif
}

void hideFromTaskbar(Window& window) {
#ifdef _WIN32
    HWND hwnd = (HWND)window.nativeHandle();
    if (!hwnd) return;
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    ex = (ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~WS_EX_APPWINDOW;
    SetWindowLongW(hwnd, GWL_EXSTYLE, ex);
#else
    (void)window;
#endif
}

bool primaryMonitorSize(int& w, int& h) {
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    if (!mon) return false;
    const GLFWvidmode* mode = glfwGetVideoMode(mon);
    if (!mode) return false;
    w = mode->width; h = mode->height;
    return true;
}

}  // namespace bh::wallpaper
