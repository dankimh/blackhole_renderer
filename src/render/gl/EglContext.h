#pragma once
// Headless OpenGL context via EGL device platform (no X server needed).
namespace bh {
class EglContext {
public:
    ~EglContext();
    bool create(int gpuIndex = 0);
    void destroy();
    static void* getProcAddress(const char* name);
private:
    void* dpy_ = nullptr;
    void* ctx_ = nullptr;
    void* surf_ = nullptr;
};
}  // namespace bh
