#pragma once
// Windows: presents frames through DirectComposition - a DXGI flip-model composition swapchain
// bound to a DComp visual whose target is our HWND. This is how the shell's own
// SHELLDLL_DefView (a WS_EX_LAYERED alpha=255 window) and Chromium/WebView2 wallpapers put
// pixels on the Windows 11 raised desktop; GDI and blt-model DXGI presents are not composed there.
#include <cstdint>

namespace bh {
class DCompPresenter {
public:
    ~DCompPresenter();
    bool init(void* hwnd, int width, int height);
    bool present(const uint8_t* bgra, int width, int height, bool topDown);
    void shutdown();
private:
    bool resize(int width, int height);
    void* hwnd_ = nullptr;
    void* device_ = nullptr;     // ID3D11Device*
    void* context_ = nullptr;    // ID3D11DeviceContext*
    void* swapchain_ = nullptr;  // IDXGISwapChain1*
    void* staging_ = nullptr;    // ID3D11Texture2D* dynamic
    void* dcompDevice_ = nullptr;   // IDCompositionDevice*
    void* dcompTarget_ = nullptr;   // IDCompositionTarget*
    void* dcompVisual_ = nullptr;   // IDCompositionVisual*
    int w_ = 0, h_ = 0;
};
}  // namespace bh
