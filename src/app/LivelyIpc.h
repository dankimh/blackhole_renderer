#pragma once
// Lively Wallpaper IPC (stdin/stdout JSON lines), see Lively.Models/Message/*.cs.
// Message "Type" is the MessageType enum index; string names are accepted too.
#include <nlohmann/json.hpp>
#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace bh {

enum class IpcType : int {
    msg_hwnd = 0, msg_console, msg_wploaded, msg_screenshot,
    cmd_reload, cmd_close, cmd_screenshot, cmd_suspend, cmd_resume, cmd_volume,
    lsp_perfcntr, lsp_nowplaying,
    lp_slider, lp_textbox, lp_dropdown, lp_fdropdown, lp_button, lp_cpicker, lp_chekbox,
    lp_dropdown_scaler,
    unknown = 100, terminate = 101,
};

struct IpcMessage {
    IpcType type = IpcType::unknown;
    std::string name;      // property name for lp_* messages
    nlohmann::json value;  // property value
    nlohmann::json raw;
};

class LivelyIpc {
public:
    ~LivelyIpc();
    void start();          // spawns the stdin reader thread
    void stop();
    bool poll(IpcMessage& out);
    static IpcMessage parse(const std::string& line);

    // stdout messages (Lively player protocol)
    static void sendHwnd(unsigned long long hwnd);
    static void sendWallpaperLoaded(bool success);
    static void sendScreenshotDone(const std::string& path, bool success);

private:
    std::thread thread_;
    std::mutex mutex_;
    std::queue<IpcMessage> queue_;
    std::atomic<bool> running_{false};
};

}  // namespace bh
