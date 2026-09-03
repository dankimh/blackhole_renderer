#include "platform/DCompPresenter.h"
#include "util/Log.h"
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

namespace bh {

template <class T> static void release(void*& p) {
    if (p) { static_cast<T*>(p)->Release(); p = nullptr; }
}
static bool ok(HRESULT hr, const char* what) {
    if (SUCCEEDED(hr)) return true;
    LOG_ERROR("DComp presenter: %s failed 0x%08lx", what, (unsigned long)hr);
    return false;
}

DCompPresenter::~DCompPresenter() { shutdown(); }

bool DCompPresenter::init(void* hwnd, int width, int height) {
    shutdown();
    hwnd_ = hwnd;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL got;
    if (!ok(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 3, D3D11_SDK_VERSION, &dev, &got, &ctx),
            "D3D11CreateDevice")) return false;
    device_ = dev; context_ = ctx;

    IDXGIDevice* dxgiDev = nullptr;
    if (!ok(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev), "QueryInterface(IDXGIDevice)")) return false;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    bool good = ok(dxgiDev->GetAdapter(&adapter), "GetAdapter") &&
                ok(adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory), "GetParent(IDXGIFactory2)");
    if (adapter) adapter->Release();
    if (!good) { dxgiDev->Release(); return false; }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = width; sd.Height = height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc = {1, 0};
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    IDXGISwapChain1* sc = nullptr;
    good = ok(factory->CreateSwapChainForComposition(dev, &sd, nullptr, &sc), "CreateSwapChainForComposition");
    factory->Release();
    if (!good) { dxgiDev->Release(); return false; }
    swapchain_ = sc;

    IDCompositionDevice* dcomp = nullptr;
    good = ok(DCompositionCreateDevice(dxgiDev, __uuidof(IDCompositionDevice), (void**)&dcomp), "DCompositionCreateDevice");
    dxgiDev->Release();
    if (!good) return false;
    dcompDevice_ = dcomp;
    IDCompositionTarget* target = nullptr;
    if (!ok(dcomp->CreateTargetForHwnd((HWND)hwnd, TRUE, &target), "CreateTargetForHwnd")) return false;
    dcompTarget_ = target;
    IDCompositionVisual* visual = nullptr;
    if (!ok(dcomp->CreateVisual(&visual), "CreateVisual")) return false;
    dcompVisual_ = visual;
    if (!ok(visual->SetContent(sc), "SetContent") || !ok(target->SetRoot(visual), "SetRoot") ||
        !ok(dcomp->Commit(), "Commit")) return false;

    w_ = width; h_ = height;
    if (!resize(width, height)) return false;
    LOG_INFO("DirectComposition presenter: %dx%d flip swapchain on visual", width, height);
    return true;
}

bool DCompPresenter::resize(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    auto* dev = static_cast<ID3D11Device*>(device_);
    auto* sc = static_cast<IDXGISwapChain1*>(swapchain_);
    release<ID3D11Texture2D>(staging_);
    if (width != w_ || height != h_) {
        if (!ok(sc->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0), "ResizeBuffers")) return false;
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
    if (!ok(dev->CreateTexture2D(&td, nullptr, &tex), "CreateTexture2D")) return false;
    staging_ = tex;
    w_ = width; h_ = height;
    return true;
}

bool DCompPresenter::present(const uint8_t* bgra, int width, int height, bool topDown) {
    if (!device_) return false;
    if (width != w_ || height != h_) { if (!resize(width, height)) return false; }
    auto* ctx = static_cast<ID3D11DeviceContext*>(context_);
    auto* sc = static_cast<IDXGISwapChain1*>(swapchain_);
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
    HRESULT hr = sc->Present(1, 0);
    if (FAILED(hr)) {
        static int n = 0;
        if (n++ % 300 == 0) LOG_WARN("DComp Present failed: 0x%08lx", (unsigned long)hr);
        return false;
    }
    return true;
}

void DCompPresenter::shutdown() {
    release<IDCompositionVisual>(dcompVisual_);
    release<IDCompositionTarget>(dcompTarget_);
    release<IDCompositionDevice>(dcompDevice_);
    release<ID3D11Texture2D>(staging_);
    release<IDXGISwapChain1>(swapchain_);
    release<ID3D11DeviceContext>(context_);
    release<ID3D11Device>(device_);
    w_ = h_ = 0;
}

}  // namespace bh
#else
namespace bh {
DCompPresenter::~DCompPresenter() {}
bool DCompPresenter::init(void*, int, int) { return false; }
bool DCompPresenter::present(const uint8_t*, int, int, bool) { return false; }
void DCompPresenter::shutdown() {}
bool DCompPresenter::resize(int, int) { return false; }
}  // namespace bh
#endif
