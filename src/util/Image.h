#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bh::image {
/// Writes RGBA8 (row 0 = top) as PNG or JPG based on the extension.
bool write(const std::string& path, const std::vector<uint8_t>& rgba, int w, int h);
/// Flip rows in place (GL readback is bottom-up).
void flipVertical(std::vector<uint8_t>& rgba, int w, int h);
}  // namespace bh::image
