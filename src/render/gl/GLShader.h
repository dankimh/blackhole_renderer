#pragma once
#include "render/gl/GLLoader.h"
#include <filesystem>
#include <string>
#include <vector>

namespace bh::gl {
/// Loads a GLSL file, resolving `#include "..."` relative to `root` and injecting
/// `#define GL 1` after `#version` (the Vulkan path gets VULKAN from glslc).
std::string preprocess(const std::filesystem::path& root, const std::string& relPath);

struct Stage { GLenum type; std::string file; };
/// Compile + link. Returns 0 on failure (errors logged).
GLuint buildProgram(const std::filesystem::path& root, const std::vector<Stage>& stages);
}  // namespace bh::gl
