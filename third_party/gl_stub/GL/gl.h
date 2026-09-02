/* Minimal <GL/gl.h> stand-in for cuda_gl_interop.h on systems without Mesa dev headers.
 * Only used for the CUDA interop translation unit (see CMakeLists.txt). */
#ifndef __gl_h_
#define __gl_h_
#include "../../GL/glcorearb.h"
#endif
