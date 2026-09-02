#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#include "util/Image.h"
#include <algorithm>
#include <cctype>

namespace bh::image {
bool write(const std::string& path, const std::vector<uint8_t>& rgba, int w, int h) {
    std::string ext = path.size() > 4 ? path.substr(path.size() - 4) : "";
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (ext == ".jpg" || ext == "jpeg")
        return stbi_write_jpg(path.c_str(), w, h, 4, rgba.data(), 92) != 0;
    if (ext == ".bmp") return stbi_write_bmp(path.c_str(), w, h, 4, rgba.data()) != 0;
    return stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
}

void flipVertical(std::vector<uint8_t>& rgba, int w, int h) {
    const size_t row = (size_t)w * 4;
    std::vector<uint8_t> tmp(row);
    for (int y = 0; y < h / 2; ++y) {
        uint8_t* a = rgba.data() + row * y;
        uint8_t* b = rgba.data() + row * (h - 1 - y);
        std::copy(a, a + row, tmp.begin());
        std::copy(b, b + row, a);
        std::copy(tmp.begin(), tmp.end(), b);
    }
}
}  // namespace bh::image
