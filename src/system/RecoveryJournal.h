#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sf {

struct JournalEntry {
    uint32_t pid = 0;
    uint64_t createTime = 0;   // process creation FILETIME, guards against PID reuse
    bool originalMuted = false;
    std::string deviceId;      // UTF-8
    std::string instanceId;    // UTF-8 session instance identifier
    float originalVolume = 1.0f;   // session volume before it was hushed
};

std::string serializeJournal(const std::vector<JournalEntry>& entries);
std::vector<JournalEntry> parseJournal(const std::string& text);

} // namespace sf
