#pragma once
// Minimal OpenGL 4.6 core function loader (glad-like) using glcorearb.h typedefs.
// Do not include together with <GL/gl.h>.
#include "GL/glcorearb.h"

#define BH_GL_FUNCS(X) \
    X(PFNGLGETSTRINGPROC, glGetString) \
    X(PFNGLGETSTRINGIPROC, glGetStringi) \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv) \
    X(PFNGLGETERRORPROC, glGetError) \
    X(PFNGLENABLEPROC, glEnable) \
    X(PFNGLDISABLEPROC, glDisable) \
    X(PFNGLBLENDFUNCPROC, glBlendFunc) \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation) \
    X(PFNGLVIEWPORTPROC, glViewport) \
    X(PFNGLCLEARCOLORPROC, glClearColor) \
    X(PFNGLCLEARPROC, glClear) \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays) \
    X(PFNGLFINISHPROC, glFinish) \
    X(PFNGLFLUSHPROC, glFlush) \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei) \
    X(PFNGLREADPIXELSPROC, glReadPixels) \
    X(PFNGLREADBUFFERPROC, glReadBuffer) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLCREATEFRAMEBUFFERSPROC, glCreateFramebuffers) \
    X(PFNGLNAMEDFRAMEBUFFERTEXTUREPROC, glNamedFramebufferTexture) \
    X(PFNGLNAMEDFRAMEBUFFERDRAWBUFFERPROC, glNamedFramebufferDrawBuffer) \
    X(PFNGLNAMEDFRAMEBUFFERREADBUFFERPROC, glNamedFramebufferReadBuffer) \
    X(PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC, glCheckNamedFramebufferStatus) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    X(PFNGLCLEARNAMEDFRAMEBUFFERFVPROC, glClearNamedFramebufferfv) \
    X(PFNGLCREATETEXTURESPROC, glCreateTextures) \
    X(PFNGLTEXTURESTORAGE2DPROC, glTextureStorage2D) \
    X(PFNGLTEXTUREPARAMETERIPROC, glTextureParameteri) \
    X(PFNGLBINDTEXTUREUNITPROC, glBindTextureUnit) \
    X(PFNGLBINDIMAGETEXTUREPROC, glBindImageTexture) \
    X(PFNGLCLEARTEXIMAGEPROC, glClearTexImage) \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures) \
    X(PFNGLCREATEBUFFERSPROC, glCreateBuffers) \
    X(PFNGLNAMEDBUFFERDATAPROC, glNamedBufferData) \
    X(PFNGLNAMEDBUFFERSUBDATAPROC, glNamedBufferSubData) \
    X(PFNGLCLEARNAMEDBUFFERDATAPROC, glClearNamedBufferData) \
    X(PFNGLGETNAMEDBUFFERSUBDATAPROC, glGetNamedBufferSubData) \
    X(PFNGLBINDBUFFERBASEPROC, glBindBufferBase) \
    X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    X(PFNGLCREATEVERTEXARRAYSPROC, glCreateVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) \
    X(PFNGLCREATESHADERPROC, glCreateShader) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
    X(PFNGLDISPATCHCOMPUTEPROC, glDispatchCompute) \
    X(PFNGLMEMORYBARRIERPROC, glMemoryBarrier) \
    X(PFNGLDEBUGMESSAGECALLBACKPROC, glDebugMessageCallback) \
    X(PFNGLDEBUGMESSAGECONTROLPROC, glDebugMessageControl)

#define BH_GL_DECLARE(type, name) extern type name;
BH_GL_FUNCS(BH_GL_DECLARE)
#undef BH_GL_DECLARE

namespace bh::gl {
using GetProcFn = void* (*)(const char*);
/// Resolve all function pointers; returns false if a core function is missing.
bool load(GetProcFn getProc);
/// Log pending GL errors (returns true if any).
bool checkErrors(const char* where);
void enableDebugOutput();
}  // namespace bh::gl
