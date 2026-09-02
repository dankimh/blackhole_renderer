#include "util/File.h"
#include "util/Log.h"
#include <fstream>
#include <sstream>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace bh::file {
namespace fs = std::filesystem;

bool exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

bool readText(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool readBinary(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return false;
    auto size = f.tellg();
    out.resize((size_t)size);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), size);
    return true;
}

bool writeText(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f << text;
    return true;
}

int64_t modifiedTime(const fs::path& p) {
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    return (int64_t)t.time_since_epoch().count();
}

fs::path executableDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0) return fs::current_path();
    return fs::path(buf).parent_path();
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return fs::current_path();
    buf[n] = 0;
    return fs::path(buf).parent_path();
#endif
}

fs::path resource(const std::string& rel) {
    const fs::path candidates[] = {
        executableDir() / rel,
        fs::current_path() / rel,
#ifdef BH_SOURCE_DIR
        fs::path(BH_SOURCE_DIR) / rel,
#endif
    };
    for (const auto& c : candidates)
        if (bh::file::exists(c)) return c;
    return executableDir() / rel;
}
}  // namespace bh::file
