#pragma once
// Windows: pushes a finished frame into a WS_EX_LAYERED window with UpdateLayeredWindow.
// This is the only presentation path that works for child windows of a Progman that has
// WS_EX_NOREDIRECTIONBITMAP (Windows 11 24H2+ "raised desktop"): GL/Vulkan swapchains of
// such children never reach DWM, GDI layered updates do. No-op on other platforms.
#include <cstdint>

namespace bh {
class LayeredPresenter {
public:
    ~LayeredPresenter();
    bool init(void* hwnd);
    /// `bgra` is width*height*4 bytes. topDown = row 0 is the top of the image.
    bool present(const uint8_t* bgra, int width, int height, bool topDown);
    void shutdown();
private:
    bool ensureLayered(bool reset);
    void* hwnd_ = nullptr;
    void* memDc_ = nullptr;
    void* bitmap_ = nullptr;
    void* bits_ = nullptr;
    int w_ = 0, h_ = 0;
    bool topDown_ = true;
    int failures_ = 0;
};
}  // namespace bh
