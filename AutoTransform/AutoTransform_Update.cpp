#include "AutoTransform_Update.h"

#include <atomic>
#include <cctype>
#include <mutex>
#include <thread>

namespace {

struct UpdateCheckState {
    std::once_flag startOnce;
    std::atomic<bool> updateAvailable{false};
    std::thread worker;

    ~UpdateCheckState() {
        if (worker.joinable()) worker.join();
    }
};

UpdateCheckState& State() {
    static UpdateCheckState state;
    return state;
}

bool ParseComponent(const std::string& tag, size_t& offset, int& value) {
    if (offset >= tag.size() || !std::isdigit(static_cast<unsigned char>(tag[offset]))) return false;
    value = 0;
    do {
        const int digit = tag[offset++] - '0';
        if (value > (9999 - digit) / 10) return false;
        value = value * 10 + digit;
    } while (offset < tag.size() && std::isdigit(static_cast<unsigned char>(tag[offset])));
    return true;
}

bool ParseVersion(const std::string& tag, int& major, int& minor, int& patch) {
    size_t offset = (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) ? 1 : 0;
    if (!ParseComponent(tag, offset, major) || offset >= tag.size() || tag[offset++] != '.') return false;
    if (!ParseComponent(tag, offset, minor) || offset >= tag.size() || tag[offset++] != '.') return false;
    return ParseComponent(tag, offset, patch) && offset == tag.size();
}

bool ReadReleaseTag(const std::string& json, std::string& tag) {
    const size_t key = json.find("\"tag_name\"");
    if (key == std::string::npos) return false;
    size_t offset = json.find(':', key + 10);
    if (offset == std::string::npos) return false;
    ++offset;
    while (offset < json.size() && std::isspace(static_cast<unsigned char>(json[offset]))) ++offset;
    if (offset >= json.size() || json[offset++] != '"') return false;
    const size_t end = json.find('"', offset);
    if (end == std::string::npos || end - offset > 32) return false;
    tag = json.substr(offset, end - offset);
    return true;
}

} // namespace

bool AT_IsNewerReleaseTag(const std::string& json, int major, int minor, int patch) {
    std::string tag;
    int remoteMajor = 0, remoteMinor = 0, remotePatch = 0;
    if (!ReadReleaseTag(json, tag) || !ParseVersion(tag, remoteMajor, remoteMinor, remotePatch)) return false;
    if (remoteMajor != major) return remoteMajor > major;
    if (remoteMinor != minor) return remoteMinor > minor;
    return remotePatch > patch;
}

void AT_StartUpdateCheck(int major, int minor, int patch) {
    UpdateCheckState& state = State();
    try {
        std::call_once(state.startOnce, [&state, major, minor, patch]() {
            state.worker = std::thread([&state, major, minor, patch]() {
                try {
                    std::string response;
                    if (AT_FetchLatestReleaseJSON(response) &&
                        AT_IsNewerReleaseTag(response, major, minor, patch)) {
                        state.updateAvailable.store(true, std::memory_order_release);
                    }
                } catch (...) {
                    // Network and parsing failures leave the optional banner hidden.
                }
            });
        });
    } catch (...) {
        // A failed optional update check must never prevent the effect from loading.
    }
}

bool AT_IsUpdateAvailable() {
    return State().updateAvailable.load(std::memory_order_acquire);
}
