#include "util/Log.h"
#include <mutex>
#include <cstring>

namespace bh::log {
static Level g_level = Level::Info;
static bool g_lively = false;
static std::mutex g_mutex;

void setLevel(Level lvl) { g_level = lvl; }
void setLivelyConsole(bool enabled) { g_lively = enabled; }

static std::string jsonEscape(const char* s) {
    std::string out;
    for (; *s; ++s) {
        switch (*s) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += *s;
        }
    }
    return out;
}

void write(Level lvl, const char* fmt, ...) {
    if (lvl < g_level) return;
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    static const char* names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    std::lock_guard<std::mutex> lock(g_mutex);
    fprintf(stderr, "[%s] %s\n", names[(int)lvl], buf);
    if (g_lively) {
        // Lively MessageType.msg_console == 1 ; ConsoleMessageType: 0 log, 1 error, 2 console
        int cat = lvl == Level::Error ? 1 : 0;
        fprintf(stdout, "{\"Type\":1,\"Category\":%d,\"Message\":\"%s\"}\n", cat, jsonEscape(buf).c_str());
        fflush(stdout);
    }
}
}  // namespace bh::log
