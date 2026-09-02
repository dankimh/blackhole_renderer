#include "app/LivelyIpc.h"
#include "util/Log.h"
#include <iostream>
#include <map>

namespace bh {
using json = nlohmann::json;

LivelyIpc::~LivelyIpc() { stop(); }

IpcMessage LivelyIpc::parse(const std::string& lineIn) {
    IpcMessage m;
    std::string line = lineIn;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
    if (line.empty()) return m;
    if (line == "lively:terminate" || line == "lively:close") { m.type = IpcType::terminate; return m; }
    if (line.front() != '{') { m.raw = line; return m; }
    json j;
    try { j = json::parse(line); } catch (const std::exception& e) {
        LOG_WARN("IPC: bad JSON: %s", e.what());
        return m;
    }
    m.raw = j;
    static const std::map<std::string, IpcType> names = {
        {"msg_hwnd", IpcType::msg_hwnd}, {"msg_console", IpcType::msg_console},
        {"cmd_reload", IpcType::cmd_reload}, {"cmd_close", IpcType::cmd_close},
        {"cmd_screenshot", IpcType::cmd_screenshot}, {"cmd_suspend", IpcType::cmd_suspend},
        {"cmd_resume", IpcType::cmd_resume}, {"cmd_volume", IpcType::cmd_volume},
        {"lp_slider", IpcType::lp_slider}, {"lp_textbox", IpcType::lp_textbox},
        {"lp_dropdown", IpcType::lp_dropdown}, {"lp_fdropdown", IpcType::lp_fdropdown},
        {"lp_button", IpcType::lp_button}, {"lp_cpicker", IpcType::lp_cpicker},
        {"lp_chekbox", IpcType::lp_chekbox}, {"lp_checkbox", IpcType::lp_chekbox},
        {"lp_dropdown_scaler", IpcType::lp_dropdown_scaler},
        // convenience aliases
        {"slider", IpcType::lp_slider}, {"checkbox", IpcType::lp_chekbox},
        {"dropdown", IpcType::lp_dropdown}, {"close", IpcType::cmd_close},
        {"screenshot", IpcType::cmd_screenshot}, {"suspend", IpcType::cmd_suspend},
        {"resume", IpcType::cmd_resume}, {"reload", IpcType::cmd_reload},
    };
    if (j.contains("Type")) {
        const json& t = j["Type"];
        if (t.is_number_integer()) m.type = (IpcType)t.get<int>();
        else if (t.is_string()) {
            auto it = names.find(t.get<std::string>());
            if (it != names.end()) m.type = it->second;
        }
    }
    if (j.contains("Name") && j["Name"].is_string()) m.name = j["Name"].get<std::string>();
    if (j.contains("Value")) m.value = j["Value"];
    return m;
}

void LivelyIpc::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread([this] {
        std::string line;
        size_t received = 0;
        while (running_ && std::getline(std::cin, line)) {
            IpcMessage m = parse(line);
            if (m.type == IpcType::unknown && m.raw.is_null()) continue;   // blank line
            ++received;
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(m));
        }
        // stdin closed. Lively's application-wallpaper host never attaches stdin, so an
        // immediate EOF is normal; only treat EOF as "host died" after a real conversation.
        if (running_ && received > 0) {
            LOG_INFO("IPC: stdin closed after %zu messages - shutting down", received);
            IpcMessage m; m.type = IpcType::terminate;
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(m);
        } else if (running_) {
            LOG_INFO("IPC: stdin not connected (no host messages) - continuing without IPC");
        }
    });
    thread_.detach();   // blocked in getline; process exit tears it down
}

void LivelyIpc::stop() { running_ = false; }

bool LivelyIpc::poll(IpcMessage& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.pop();
    return true;
}

void LivelyIpc::sendHwnd(unsigned long long hwnd) {
    std::cout << "{\"Type\":0,\"Hwnd\":" << hwnd << "}" << std::endl;
}
void LivelyIpc::sendWallpaperLoaded(bool success) {
    std::cout << "{\"Type\":2,\"Success\":" << (success ? "true" : "false") << "}" << std::endl;
}
void LivelyIpc::sendScreenshotDone(const std::string& path, bool success) {
    json j = {{"Type", 3}, {"FilePath", path}, {"Success", success}};
    std::cout << j.dump() << std::endl;
}

}  // namespace bh
