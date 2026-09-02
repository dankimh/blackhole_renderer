#pragma once
// Windows: presents a finished BGRA frame through a Direct3D 11 bit-block-transfer swapchain
// (DXGI_SWAP_EFFECT_DISCARD). This is the presentation model Microsoft prescribes for
// WS_EX_LAYERED children of the Windows 11 "raised desktop" Progman - the model Lively's
// web/video wallpapers use - whereas GL/Vulkan swapchains of such windows are never composed.
#include <cstdint>

namespace bh {
class D3DPresenter {
public:
    ~D3DPresenter();
    bool init(void* hwnd, int width, int height);
    /// `bgra` = width*height*4 bytes; topDown = row 0 is the top of the image.
    bool present(const uint8_t* bgra, int width, int height, bool topDown);
    void shutdown();
    const char* adapterName() const { return adapter_; }
private:
    bool resize(int width, int height);
    void* hwnd_ = nullptr;
    void* device_ = nullptr;     // ID3D11Device*
    void* context_ = nullptr;    // ID3D11DeviceContext*
    void* swapchain_ = nullptr;  // IDXGISwapChain*
    void* staging_ = nullptr;    // ID3D11Texture2D* (dynamic, CPU write)
    int w_ = 0, h_ = 0;
    char adapter_[128] = "";
};
}  // namespace bh
