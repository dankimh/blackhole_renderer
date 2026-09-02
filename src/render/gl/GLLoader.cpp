#include "render/gl/GLLoader.h"
#include "util/Log.h"

#define BH_GL_DEFINE(type, name) type name = nullptr;
BH_GL_FUNCS(BH_GL_DEFINE)
#undef BH_GL_DEFINE

namespace bh::gl {

bool load(GetProcFn getProc) {
    bool ok = true;
#define BH_GL_LOAD(type, name) \
    name = reinterpret_cast<type>(getProc(#name)); \
    if (!name && std::string(#name) != "glDebugMessageCallback" && std::string(#name) != "glDebugMessageControl") { \
        LOG_ERROR("Missing GL function: %s", #name); ok = false; }
    BH_GL_FUNCS(BH_GL_LOAD)
#undef BH_GL_LOAD
    return ok;
}

bool checkErrors(const char* where) {
    bool any = false;
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) {
        LOG_ERROR("GL error 0x%04x at %s", e, where);
        any = true;
    }
    return any;
}

static void APIENTRY debugCb(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* msg, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    if (type == GL_DEBUG_TYPE_ERROR) LOG_ERROR("GL: %s", msg);
    else LOG_WARN("GL: %s", msg);
}

void enableDebugOutput() {
    if (!glDebugMessageCallback) return;
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(debugCb, nullptr);
}

}  // namespace bh::gl
