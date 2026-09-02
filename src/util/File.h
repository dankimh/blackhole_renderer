#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>

namespace bh::file {
bool exists(const std::filesystem::path& p);
bool readText(const std::filesystem::path& p, std::string& out);
bool readBinary(const std::filesystem::path& p, std::vector<uint8_t>& out);
bool writeText(const std::filesystem::path& p, const std::string& text);
int64_t modifiedTime(const std::filesystem::path& p);   // 0 if missing
std::filesystem::path executableDir();
/// Locate a resource (e.g. "shaders", "LivelyProperties.json") next to the exe,
/// then in the source tree (development builds).
std::filesystem::path resource(const std::string& rel);
}  // namespace bh::file
