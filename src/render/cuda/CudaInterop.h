#pragma once
// CUDA <-> OpenGL / Vulkan resource sharing.
#include <cstddef>
#include <cstdint>

namespace bh::cuda {

/// OpenGL buffer registered with cudaGraphicsGLRegisterBuffer.
class GLBuffer {
public:
    ~GLBuffer();
    bool registerBuffer(unsigned int glBuffer, size_t bytes);
    void* map();     // device pointer valid until unmap()
    void unmap();
private:
    void* resource_ = nullptr;
    size_t bytes_ = 0;
};

/// Opaque OS handle exported from Vulkan (fd on Linux, HANDLE on Windows).
struct ExternalHandle {
    int fd = -1;
    void* win32 = nullptr;
};

/// Vulkan device memory imported into CUDA (VK_KHR_external_memory_*).
class VkExternalBuffer {
public:
    ~VkExternalBuffer();
    bool import(const ExternalHandle& h, size_t bytes);
    void* ptr() const { return ptr_; }
    void release();
private:
    void* extMem_ = nullptr;
    void* ptr_ = nullptr;
};

/// Vulkan semaphore imported into CUDA (VK_KHR_external_semaphore_*).
class VkExternalSemaphore {
public:
    ~VkExternalSemaphore();
    bool import(const ExternalHandle& h);
    void signal();
    void wait();
    void release();
private:
    void* sem_ = nullptr;
};

}  // namespace bh::cuda
