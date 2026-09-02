#include "platform/D3DPresenter.h"
#include "util/Log.h"
#include <cstring>
#include <iterator>
#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace bh {

template <class T> static void release(void*& p) {
    if (p) { static_cast<T*>(p)->Release(); p = nullptr; }
}

D3DPresenter::~D3DPresenter() { shutdown(); }

bool D3DPresenter::init(void* hwnd, int width, int height) {
    shutdown();
    hwnd_ = hwnd;
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferDesc.RefreshRate = {0, 1};
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = (HWND)hwnd;
    sd.SampleDesc = {1, 0};
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;   // blt model: required for layered children
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* sc = nullptr;
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                               (UINT)std::size(levels), D3D11_SDK_VERSION, &sd, &sc, &dev, &got, &ctx);
    if (FAILED(hr)) {
        LOG_ERROR("D3D11CreateDeviceAndSwapChain failed: 0x%08lx", (unsigned long)hr);
        return false;
    }
    device_ = dev; context_ = ctx; swapchain_ = sc;
    IDXGIDevice* dxgiDev = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev))) {
        IDXGIAdapter* ad = nullptr;
        if (SUCCEEDED(dxgiDev->GetAdapter(&ad))) {
            DXGI_ADAPTER_DESC d{};
            ad->GetDesc(&d);
            WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, adapter_, sizeof(adapter_), nullptr, nullptr);
            ad->Release();
        }
        dxgiDev->Release();
    }
    if (!resize(width, height)) return false;
    LOG_INFO("D3D11 presenter: %s, %dx%d, blt swapchain", adapter_, width, height);
    return true;
}

bool D3DPresenter::resize(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    auto* dev = static_cast<ID3D11Device*>(device_);
    auto* sc = static_cast<IDXGISwapChain*>(swapchain_);
    release<ID3D11Texture2D>(staging_);
    if (w_ != 0) {
        HRESULT hr = sc->ResizeBuffers(1, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
        if (FAILED(hr)) { LOG_ERROR("ResizeBuffers failed: 0x%08lx", (unsigned long)hr); return false; }
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width; td.Height = height;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc = {1, 0};
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = dev->CreateTexture2D(&td, nullptr, &tex);
    if (FAILED(hr)) { LOG_ERROR("CreateTexture2D failed: 0x%08lx", (unsigned long)hr); return false; }
    staging_ = tex;
    w_ = width; h_ = height;
    return true;
}

bool D3DPresenter::present(const uint8_t* bgra, int width, int height, bool topDown) {
    if (!device_) return false;
    if (width != w_ || height != h_) { if (!resize(width, height)) return false; }
    auto* ctx = static_cast<ID3D11DeviceContext*>(context_);
    auto* sc = static_cast<IDXGISwapChain*>(swapchain_);
    auto* tex = static_cast<ID3D11Texture2D*>(staging_);

    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx->Map(tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return false;
    const size_t row = (size_t)width * 4;
    for (int y = 0; y < height; ++y) {
        const uint8_t* src = bgra + row * (topDown ? y : (height - 1 - y));
        std::memcpy(static_cast<uint8_t*>(map.pData) + (size_t)map.RowPitch * y, src, row);
    }
    ctx->Unmap(tex, 0);

    ID3D11Texture2D* back = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back))) return false;
    ctx->CopyResource(back, tex);
    back->Release();
    HRESULT hr = sc->Present(0, 0);
    if (FAILED(hr)) {
        static int n = 0;
        if (n++ % 300 == 0) LOG_WARN("Present failed: 0x%08lx", (unsigned long)hr);
        return false;
    }
    return true;
}

void D3DPresenter::shutdown() {
    release<ID3D11Texture2D>(staging_);
    release<IDXGISwapChain>(swapchain_);
    release<ID3D11DeviceContext>(context_);
    release<ID3D11Device>(device_);
    w_ = h_ = 0;
}

}  // namespace bh
#else
namespace bh {
D3DPresenter::~D3DPresenter() {}
bool D3DPresenter::init(void*, int, int) { return false; }
bool D3DPresenter::present(const uint8_t*, int, int, bool) { return false; }
void D3DPresenter::shutdown() {}
bool D3DPresenter::resize(int, int) { return false; }
}  // namespace bh
#endif
