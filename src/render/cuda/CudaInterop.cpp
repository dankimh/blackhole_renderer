#include "render/cuda/CudaInterop.h"
#include "util/Log.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>

namespace bh::cuda {

static bool check(cudaError_t e, const char* what) {
    if (e == cudaSuccess) return true;
    LOG_ERROR("CUDA %s: %s", what, cudaGetErrorString(e));
    return false;
}

// ---- OpenGL --------------------------------------------------------------
GLBuffer::~GLBuffer() {
    if (resource_) cudaGraphicsUnregisterResource(static_cast<cudaGraphicsResource*>(resource_));
}

bool GLBuffer::registerBuffer(unsigned int glBuffer, size_t bytes) {
    // Bind CUDA to the GPU that owns the current GL context (multi-GPU hosts).
    unsigned int n = 0;
    int devs[8];
    if (cudaGLGetDevices(&n, devs, 8, cudaGLDeviceListCurrentFrame) == cudaSuccess && n > 0) cudaSetDevice(devs[0]);
    else cudaGetLastError();
    cudaGraphicsResource* res = nullptr;
    if (!check(cudaGraphicsGLRegisterBuffer(&res, glBuffer, cudaGraphicsRegisterFlagsNone), "GLRegisterBuffer"))
        return false;
    resource_ = res;
    bytes_ = bytes;
    return true;
}

void* GLBuffer::map() {
    auto* res = static_cast<cudaGraphicsResource*>(resource_);
    if (!res) return nullptr;
    if (!check(cudaGraphicsMapResources(1, &res, 0), "MapResources")) return nullptr;
    void* ptr = nullptr;
    size_t n = 0;
    if (!check(cudaGraphicsResourceGetMappedPointer(&ptr, &n, res), "GetMappedPointer")) {
        cudaGraphicsUnmapResources(1, &res, 0);
        return nullptr;
    }
    return ptr;
}

void GLBuffer::unmap() {
    auto* res = static_cast<cudaGraphicsResource*>(resource_);
    if (res) check(cudaGraphicsUnmapResources(1, &res, 0), "UnmapResources");
}

// ---- Vulkan external memory ---------------------------------------------
VkExternalBuffer::~VkExternalBuffer() { release(); }

bool VkExternalBuffer::import(const ExternalHandle& h, size_t bytes) {
    release();
    cudaExternalMemoryHandleDesc desc{};
#ifdef _WIN32
    desc.type = cudaExternalMemoryHandleTypeOpaqueWin32;
    desc.handle.win32.handle = h.win32;
#else
    desc.type = cudaExternalMemoryHandleTypeOpaqueFd;
    desc.handle.fd = h.fd;
#endif
    desc.size = bytes;
    desc.flags = cudaExternalMemoryDedicated;
    cudaExternalMemory_t mem = nullptr;
    if (!check(cudaImportExternalMemory(&mem, &desc), "ImportExternalMemory")) return false;
    cudaExternalMemoryBufferDesc bd{};
    bd.offset = 0;
    bd.size = bytes;
    void* ptr = nullptr;
    if (!check(cudaExternalMemoryGetMappedBuffer(&ptr, mem, &bd), "GetMappedBuffer")) {
        cudaDestroyExternalMemory(mem);
        return false;
    }
    extMem_ = mem;
    ptr_ = ptr;
    return true;
}

void VkExternalBuffer::release() {
    if (ptr_) cudaFree(ptr_);
    if (extMem_) cudaDestroyExternalMemory(static_cast<cudaExternalMemory_t>(extMem_));
    ptr_ = extMem_ = nullptr;
}

// ---- Vulkan external semaphore ------------------------------------------
VkExternalSemaphore::~VkExternalSemaphore() { release(); }

bool VkExternalSemaphore::import(const ExternalHandle& h) {
    release();
    cudaExternalSemaphoreHandleDesc desc{};
#ifdef _WIN32
    desc.type = cudaExternalSemaphoreHandleTypeOpaqueWin32;
    desc.handle.win32.handle = h.win32;
#else
    desc.type = cudaExternalSemaphoreHandleTypeOpaqueFd;
    desc.handle.fd = h.fd;
#endif
    cudaExternalSemaphore_t sem = nullptr;
    if (!check(cudaImportExternalSemaphore(&sem, &desc), "ImportExternalSemaphore")) return false;
    sem_ = sem;
    return true;
}

void VkExternalSemaphore::signal() {
    if (!sem_) return;
    cudaExternalSemaphoreSignalParams params{};
    auto s = static_cast<cudaExternalSemaphore_t>(sem_);
    check(cudaSignalExternalSemaphoresAsync(&s, &params, 1, 0), "SignalExternalSemaphore");
}

void VkExternalSemaphore::wait() {
    if (!sem_) return;
    cudaExternalSemaphoreWaitParams params{};
    auto s = static_cast<cudaExternalSemaphore_t>(sem_);
    check(cudaWaitExternalSemaphoresAsync(&s, &params, 1, 0), "WaitExternalSemaphore");
}

void VkExternalSemaphore::release() {
    if (sem_) cudaDestroyExternalSemaphore(static_cast<cudaExternalSemaphore_t>(sem_));
    sem_ = nullptr;
}

}  // namespace bh::cuda
