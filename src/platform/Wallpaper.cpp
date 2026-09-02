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
#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

// Desktop layer handles (Lively WinDesktopCore.SetupDesktopLayer equivalent).
struct DesktopLayer {
    HWND progman = nullptr, workerW = nullptr, defView = nullptr;
    bool layeredShellView = false;   // Windows 11 24H2+: Progman has WS_EX_NOREDIRECTIONBITMAP
};
static HWND g_parent = nullptr;

static DesktopLayer findDesktopLayer() {
    DesktopLayer d;
    d.progman = FindWindowW(L"Progman", nullptr);
    if (!d.progman) return d;
    d.layeredShellView = (GetWindowLongW(d.progman, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) != 0;
    // Ask Progman to spawn the WorkerW behind the icons (undocumented 0x052C).
    DWORD_PTR result = 0;
    SendMessageTimeoutW(d.progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &result);
    // Classic layout: top-level window owning SHELLDLL_DefView; WorkerW is its next sibling.
    EnumWindows([](HWND top, LPARAM lp) -> BOOL {
        auto* dl = reinterpret_cast<DesktopLayer*>(lp);
        HWND shell = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
        if (shell) {
            dl->defView = shell;
            dl->workerW = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&d));
    if (d.layeredShellView) {
        // Raised desktop: Progman > { SHELLDLL_DefView (layered), WorkerW }
        d.workerW = FindWindowExW(d.progman, nullptr, L"WorkerW", nullptr);
        if (!d.defView) d.defView = FindWindowExW(d.progman, nullptr, L"SHELLDLL_DefView", nullptr);
    }
    return d;
}

static bool attach(HWND hwnd) {
    DesktopLayer d = findDesktopLayer();
    LOG_INFO("Desktop layer: progman=%p workerW=%p defView=%p layeredShellView=%d",
             (void*)d.progman, (void*)d.workerW, (void*)d.defView, (int)d.layeredShellView);
    if (!d.progman) { LOG_ERROR("Progman not found - cannot embed as wallpaper"); return false; }

    int mw = GetSystemMetrics(SM_CXSCREEN), mh = GetSystemMetrics(SM_CYSCREEN);
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_POPUP);
    style |= WS_CHILD;
    SetWindowLongW(hwnd, GWL_STYLE, style);

    HWND parent;
    if (d.layeredShellView) {
        // Microsoft guidance: our window must be a WS_EX_LAYERED child of Progman, z-ordered
        // below SHELLDLL_DefView (icons) and above the WorkerW that paints the static wallpaper.
        LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED;
        SetWindowLongW(hwnd, GWL_EXSTYLE, ex);
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        parent = d.progman;
        if (!SetParent(hwnd, parent)) { LOG_ERROR("SetParent(Progman) failed: %lu", GetLastError()); return false; }
        if (d.defView)
            SetWindowPos(hwnd, d.defView, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (d.workerW) {
            // WorkerW must stay the bottom-most child of Progman.
            HWND last = GetWindow(GetWindow(d.progman, GW_CHILD), GW_HWNDLAST);
            if (last != d.workerW)
                SetWindowPos(d.workerW, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    } else {
        parent = d.workerW ? d.workerW : d.progman;
        if (!SetParent(hwnd, parent)) { LOG_ERROR("SetParent(WorkerW) failed: %lu", GetLastError()); return false; }
    }
    // Cover the primary monitor (parent client coordinates).
    POINT origin{0, 0};
    ScreenToClient(parent, &origin);
    SetWindowPos(hwnd, nullptr, origin.x, origin.y, mw, mh, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    g_parent = parent;
    LOG_INFO("Embedded window behind desktop icons (%dx%d, parent=%p)", mw, mh, (void*)parent);
    return true;
}
#endif

bool embed(Window& window) {
#ifdef _WIN32
    HWND hwnd = (HWND)window.nativeHandle();
    if (!hwnd) return false;
    return attach(hwnd);
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

/// Re-attach if the desktop layer was recreated (explorer restart / theme change).
bool maintain(Window& window) {
#ifdef _WIN32
    HWND hwnd = (HWND)window.nativeHandle();
    if (!hwnd || !g_parent) return true;
    if (IsWindow(g_parent) && GetParent(hwnd) == g_parent) return true;
    LOG_WARN("Desktop layer changed - re-embedding");
    return attach(hwnd);
#else
    (void)window;
    return true;
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
