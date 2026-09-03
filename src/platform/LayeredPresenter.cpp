#include "platform/LayeredPresenter.h"
#include "util/Log.h"
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

namespace bh {

LayeredPresenter::~LayeredPresenter() { shutdown(); }

bool LayeredPresenter::init(void* hwnd, bool blt) {
    hwnd_ = hwnd;
    blt_ = blt;
#ifdef _WIN32
    if (blt) return true;   // keep whatever layered state the embedder set
    return ensureLayered(true);
#else
    return false;
#endif
}

bool LayeredPresenter::ensureLayered(bool reset) {
#ifdef _WIN32
    HWND h = (HWND)hwnd_;
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    if (reset && (ex & WS_EX_LAYERED)) {
        // Leaving and re-entering WS_EX_LAYERED clears "SetLayeredWindowAttributes" mode,
        // which is mutually exclusive with UpdateLayeredWindow (Lively applies it on attach).
        SetWindowLongW(h, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
        ex = GetWindowLongW(h, GWL_EXSTYLE);
    }
    if (!(ex & WS_EX_LAYERED)) SetWindowLongW(h, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    return true;
#else
    (void)reset;
    return false;
#endif
}

bool LayeredPresenter::present(const uint8_t* bgra, int width, int height, bool topDown) {
#ifdef _WIN32
    if (!hwnd_ || width <= 0 || height <= 0) return false;
    if (width != w_ || height != h_ || topDown != topDown_ || !bitmap_) {
        if (bitmap_) DeleteObject((HBITMAP)bitmap_);
        if (memDc_) DeleteDC((HDC)memDc_);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = topDown ? -height : height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(nullptr);
        memDc_ = CreateCompatibleDC(screen);
        void* bits = nullptr;
        bitmap_ = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (!bitmap_ || !memDc_) { LOG_ERROR("CreateDIBSection failed"); return false; }
        SelectObject((HDC)memDc_, (HBITMAP)bitmap_);
        bits_ = bits;
        w_ = width; h_ = height; topDown_ = topDown;
        LOG_INFO("Layered presenter: %dx%d DIB", width, height);
    }
    std::memcpy(bits_, bgra, (size_t)width * height * 4);

    if (blt_) {
        HDC dc = GetDC((HWND)hwnd_);
        if (!dc) return false;
        BOOL ok = BitBlt(dc, 0, 0, width, height, (HDC)memDc_, 0, 0, SRCCOPY);
        ReleaseDC((HWND)hwnd_, dc);
        if (!ok && failures_++ % 300 == 0) LOG_WARN("BitBlt to window failed (%lu)", GetLastError());
        return ok != 0;
    }

    POINT src{0, 0};
    SIZE size{width, height};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};   // alpha channel is 255 everywhere
    if (!UpdateLayeredWindow((HWND)hwnd_, nullptr, nullptr, &size, (HDC)memDc_, &src, 0, &bf, ULW_ALPHA)) {
        DWORD err = GetLastError();
        if (failures_++ % 300 == 0) LOG_WARN("UpdateLayeredWindow failed (%lu) - resetting layered style", err);
        ensureLayered(true);
        return false;
    }
    failures_ = 0;
    return true;
#else
    (void)bgra; (void)width; (void)height; (void)topDown;
    return false;
#endif
}

void LayeredPresenter::shutdown() {
#ifdef _WIN32
    if (bitmap_) DeleteObject((HBITMAP)bitmap_);
    if (memDc_) DeleteDC((HDC)memDc_);
#endif
    bitmap_ = memDc_ = bits_ = nullptr;
    w_ = h_ = 0;
}

}  // namespace bh
