#include "render/gl/EglContext.h"
#include "util/Log.h"
#define EGL_NO_X11
#define EGL_EGLEXT_PROTOTYPES
#include "EGL/egl.h"
#include "EGL/eglext.h"
#include <dlfcn.h>
#include <cstring>

namespace bh {

static void* g_egl = nullptr;
static PFNEGLGETPROCADDRESSPROC p_eglGetProcAddress;
static PFNEGLINITIALIZEPROC p_eglInitialize;
static PFNEGLBINDAPIPROC p_eglBindAPI;
static PFNEGLCHOOSECONFIGPROC p_eglChooseConfig;
static PFNEGLCREATECONTEXTPROC p_eglCreateContext;
static PFNEGLMAKECURRENTPROC p_eglMakeCurrent;
static PFNEGLDESTROYCONTEXTPROC p_eglDestroyContext;
static PFNEGLTERMINATEPROC p_eglTerminate;
static PFNEGLGETERRORPROC p_eglGetError;
static PFNEGLCREATEPBUFFERSURFACEPROC p_eglCreatePbufferSurface;
static PFNEGLDESTROYSURFACEPROC p_eglDestroySurface;
static PFNEGLQUERYSTRINGPROC p_eglQueryString;

template <class T> static bool sym(T& out, const char* name) {
    out = reinterpret_cast<T>(dlsym(g_egl, name));
    if (!out) LOG_ERROR("EGL symbol missing: %s", name);
    return out != nullptr;
}

EglContext::~EglContext() { destroy(); }

void* EglContext::getProcAddress(const char* name) {
    return p_eglGetProcAddress ? (void*)p_eglGetProcAddress(name) : nullptr;
}

bool EglContext::create(int gpuIndex) {
    g_egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!g_egl) { LOG_ERROR("dlopen libEGL.so.1 failed: %s", dlerror()); return false; }
    bool ok = sym(p_eglGetProcAddress, "eglGetProcAddress") && sym(p_eglInitialize, "eglInitialize") &&
              sym(p_eglBindAPI, "eglBindAPI") && sym(p_eglChooseConfig, "eglChooseConfig") &&
              sym(p_eglCreateContext, "eglCreateContext") && sym(p_eglMakeCurrent, "eglMakeCurrent") &&
              sym(p_eglDestroyContext, "eglDestroyContext") && sym(p_eglTerminate, "eglTerminate") &&
              sym(p_eglGetError, "eglGetError") && sym(p_eglCreatePbufferSurface, "eglCreatePbufferSurface") &&
              sym(p_eglDestroySurface, "eglDestroySurface") && sym(p_eglQueryString, "eglQueryString");
    if (!ok) return false;

    auto queryDevices = (PFNEGLQUERYDEVICESEXTPROC)p_eglGetProcAddress("eglQueryDevicesEXT");
    auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)p_eglGetProcAddress("eglGetPlatformDisplayEXT");
    auto queryDeviceString = (PFNEGLQUERYDEVICESTRINGEXTPROC)p_eglGetProcAddress("eglQueryDeviceStringEXT");
    EGLDisplay dpy = EGL_NO_DISPLAY;
    if (queryDevices && getPlatformDisplay) {
        EGLDeviceEXT devices[32];
        EGLint n = 0;
        queryDevices(32, devices, &n);
        LOG_INFO("EGL devices: %d", n);
        if (n > 0) {
            int pick = gpuIndex < n ? gpuIndex : 0;
            // Prefer a device that exposes an NVIDIA/DRM node if the index is out of range.
            if (queryDeviceString) {
                const char* ext = queryDeviceString(devices[pick], EGL_EXTENSIONS);
                LOG_DEBUG("EGL device %d extensions: %s", pick, ext ? ext : "");
            }
            dpy = getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devices[pick], nullptr);
        }
    }
    if (dpy == EGL_NO_DISPLAY) {
        auto getDisplay = (PFNEGLGETDISPLAYPROC)dlsym(g_egl, "eglGetDisplay");
        dpy = getDisplay ? getDisplay(EGL_DEFAULT_DISPLAY) : EGL_NO_DISPLAY;
    }
    if (dpy == EGL_NO_DISPLAY) { LOG_ERROR("No EGL display"); return false; }

    EGLint major, minor;
    if (!p_eglInitialize(dpy, &major, &minor)) { LOG_ERROR("eglInitialize failed 0x%x", p_eglGetError()); return false; }
    LOG_INFO("EGL %d.%d vendor: %s", major, minor, p_eglQueryString(dpy, EGL_VENDOR));
    if (!p_eglBindAPI(EGL_OPENGL_API)) { LOG_ERROR("eglBindAPI(OpenGL) failed"); return false; }

    const EGLint cfgAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!p_eglChooseConfig(dpy, cfgAttribs, &cfg, 1, &ncfg) || ncfg == 0) {
        LOG_ERROR("eglChooseConfig failed");
        return false;
    }
    const EGLint ctxAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 6,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext ctx = p_eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttribs);
    if (ctx == EGL_NO_CONTEXT) { LOG_ERROR("eglCreateContext failed 0x%x", p_eglGetError()); return false; }

    // Surfaceless if supported, else a tiny pbuffer (we render into FBOs anyway).
    EGLSurface surf = EGL_NO_SURFACE;
    const char* ext = p_eglQueryString(dpy, EGL_EXTENSIONS);
    bool surfaceless = ext && strstr(ext, "EGL_KHR_surfaceless_context");
    if (!surfaceless) {
        const EGLint pb[] = {EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE};
        surf = p_eglCreatePbufferSurface(dpy, cfg, pb);
    }
    if (!p_eglMakeCurrent(dpy, surf, surf, ctx)) { LOG_ERROR("eglMakeCurrent failed 0x%x", p_eglGetError()); return false; }
    dpy_ = dpy; ctx_ = ctx; surf_ = surf;
    return true;
}

void EglContext::destroy() {
    if (!dpy_) return;
    p_eglMakeCurrent(dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surf_) p_eglDestroySurface(dpy_, surf_);
    if (ctx_) p_eglDestroyContext(dpy_, ctx_);
    p_eglTerminate(dpy_);
    dpy_ = ctx_ = surf_ = nullptr;
}

}  // namespace bh
