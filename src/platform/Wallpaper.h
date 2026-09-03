#pragma once
#include "platform/Window.h"

namespace bh::wallpaper {
/// Attach the window behind the desktop icons (Windows: Progman/WorkerW trick,
/// X11: _NET_WM_WINDOW_TYPE_DESKTOP). Returns false if unsupported.
bool embed(Window& window);
/// Windows only: 0 = auto (layered child of Progman on new Win11), 1 = plain WorkerW child, 2 = force layered, 3 = bottom-most top-level window.
void setEmbedMode(int mode);
/// Call periodically in wallpaper mode: re-attaches after an explorer restart.
bool maintain(Window& window);
/// Log the desktop window hierarchy (Progman / WorkerW / wallpaper hosts). Windows only.
void dumpDesktop();
/// Windows: create a plain (non-layered) child window filling `window`, like Lively's WebView2 host
/// child. Returns its HWND (as void*) or null. Presenters can target it instead of the layered window.
void* createInnerChild(Window& window);
/// Remove from taskbar / alt-tab (Windows only, harmless elsewhere).
void hideFromTaskbar(Window& window);
/// Primary monitor size in pixels.
bool primaryMonitorSize(int& w, int& h);
}  // namespace bh::wallpaper
