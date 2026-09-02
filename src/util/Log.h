#pragma once
#include <string>
#include <cstdio>
#include <cstdarg>

namespace bh::log {
enum class Level { Debug, Info, Warn, Error };
void setLevel(Level lvl);
void setLivelyConsole(bool enabled);   // also emit {"Type":1,"Message":..} lines for Lively
void write(Level lvl, const char* fmt, ...);
}  // namespace bh::log

#define LOG_DEBUG(...) ::bh::log::write(::bh::log::Level::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::bh::log::write(::bh::log::Level::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::bh::log::write(::bh::log::Level::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::bh::log::write(::bh::log::Level::Error, __VA_ARGS__)
