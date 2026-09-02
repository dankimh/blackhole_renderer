#pragma once
#include "platform/Window.h"

namespace bh::wallpaper {
/// Attach the window behind the desktop icons (Windows: Progman/WorkerW trick,
/// X11: _NET_WM_WINDOW_TYPE_DESKTOP). Returns false if unsupported.
bool embed(Window& window);
/// Windows only: 0 = auto (layered child of Progman on new Win11), 1 = force plain WorkerW child, 2 = force layered.
void setEmbedMode(int mode);
/// Call periodically in wallpaper mode: re-attaches after an explorer restart.
bool maintain(Window& window);
/// Remove from taskbar / alt-tab (Windows only, harmless elsewhere).
void hideFromTaskbar(Window& window);
/// Primary monitor size in pixels.
bool primaryMonitorSize(int& w, int& h);
}  // namespace bh::wallpaper
