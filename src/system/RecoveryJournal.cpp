#include "system/RecoveryJournal.h"
#include <sstream>

namespace sf {

static const char* kHeader = "STEREOFLOW-JOURNAL 1";

std::string serializeJournal(const std::vector<JournalEntry>& entries) {
    std::ostringstream os;
    os << kHeader << '\n';
    for (const auto& e : entries)
        os << e.pid << '\t' << e.createTime << '\t' << (e.originalMuted ? 1 : 0) << '\t'
           << e.deviceId << '\t' << e.instanceId << '\t' << e.originalVolume << '\n';
    return os.str();
}

std::vector<JournalEntry> parseJournal(const std::string& text) {
    std::vector<JournalEntry> out;
    std::istringstream is(text);
    std::string line;
    if (!std::getline(is, line) || line != kHeader) return out;
    while (std::getline(is, line)) {
        std::vector<std::string> f;
        size_t start = 0;
        for (int i = 0; i < 4; ++i) {
            size_t p = line.find('\t', start);
            if (p == std::string::npos) break;
            f.push_back(line.substr(start, p - start));
            start = p + 1;
        }
        if (f.size() != 4) continue;
        f.push_back(line.substr(start));      // "<instanceId>[\t<originalVolume>]"
        float volume = 1.0f;
        if (const size_t tab = f[4].find('\t'); tab != std::string::npos) {
            try { volume = std::stof(f[4].substr(tab + 1)); } catch (...) { volume = 1.0f; }
            f[4].resize(tab);
        }
        try {
            JournalEntry e;
            e.pid = static_cast<uint32_t>(std::stoul(f[0]));
            e.createTime = std::stoull(f[1]);
            e.originalMuted = (f[2] == "1");
            e.deviceId = f[3];
            e.instanceId = f[4];
            e.originalVolume = volume;
            if (!e.instanceId.empty()) out.push_back(std::move(e));
        } catch (...) { /* skip malformed line */ }
    }
    return out;
}

} // namespace sf
