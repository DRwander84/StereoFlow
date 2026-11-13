#include "system/SessionVolumeGuard.h"
#include "core/ProcessManager.h"
#include "core/SessionEnumeration.h"
#include "system/RestorePolicy.h"
#include <cmath>
#include <cwctype>
#include <chrono>
#include <fstream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <sstream>

namespace sf {

SessionVolumeGuard::SessionVolumeGuard(std::filesystem::path journalPath) : path_(std::move(journalPath)) {}
SessionVolumeGuard::~SessionVolumeGuard() { restoreAll(); }

void SessionVolumeGuard::writeJournalLocked() {
    std::error_code ec;
    if (entries_.empty()) { std::filesystem::remove(path_, ec); return; }
    std::vector<JournalEntry> v;
    for (auto& e : entries_) v.push_back(e.j);
    std::filesystem::create_directories(path_.parent_path(), ec);
    std::ofstream f(path_, std::ios::binary | std::ios::trunc);
    f << serializeJournal(v);
    f.flush();
}

int SessionVolumeGuard::recoverFromJournal() {
    std::ifstream f(path_, std::ios::binary);
    if (!f) return 0;
    std::stringstream ss;
    ss << f.rdbuf();
    f.close();
    const auto stale = parseJournal(ss.str());
    int restored = 0;
    if (!stale.empty()) {
        ComApartment com;
        HRESULT hr = S_OK;
        auto devices = enumerateAllSessions(&hr);
        for (const auto& j : stale) {
            const std::wstring dev = fromUtf8(j.deviceId), inst = fromUtf8(j.instanceId);
            // If the process is alive but has another creation time the PID was recycled: skip.
            const uint64_t ct = ProcessManager::creationTime(j.pid);
            if (ct != 0 && j.createTime != 0 && ct != j.createTime) continue;
            for (auto& d : devices) {
                if (d.deviceId != dev) continue;
                for (auto& s : d.sessions) {
                    if (s.instanceId != inst || !s.volume) continue;
                    float cur = 1.0f;
                    if (SUCCEEDED(s.volume->GetMasterVolume(&cur)) && shouldRestoreVolume(true, cur)) {
                        if (SUCCEEDED(s.volume->SetMasterVolume(j.originalVolume, nullptr))) ++restored;
                    }
                }
            }
        }
    }
    std::error_code ec;
    std::filesystem::remove(path_, ec);
    return restored;
}

namespace {
std::wstring lowerPath(std::wstring s) {
    for (auto& ch : s) ch = static_cast<wchar_t>(std::towlower(ch));
    return s;
}
constexpr auto kLevelReprobe = std::chrono::seconds(120);   // a shallower depth is retried after this long
constexpr auto kParentsMaxAge = std::chrono::milliseconds(150);
} // namespace

const std::map<unsigned long, unsigned long>& SessionVolumeGuard::parentsLocked(bool forceFresh) {
    const auto now = std::chrono::steady_clock::now();
    if (forceFresh || parents_.empty() || now - parentsAt_ > kParentsMaxAge) {
        parents_ = ProcessManager::parentMap();
        parentsAt_ = now;
    }
    return parents_;
}

bool SessionVolumeGuard::applyHush(ISimpleAudioVolume* v, RootLevel& rl) {
    for (;;) {
        const float h = kHushLevels[rl.level];
        // Windows keeps the exact volume it is given. A read-back that differs almost always means an app (or the user's
        // slider) wrote the volume between our two calls: set again before blaming Windows. Stepping to a shallower
        // depth on the first mismatch used to leave the app at -48 dB (audible in both ears) for the rest of the run.
        bool kept = false;
        for (int attempt = 0; attempt < 3 && !kept; ++attempt) {
            if (FAILED(v->SetMasterVolume(h, nullptr))) return false;
            float back = -1.0f;
            if (FAILED(v->GetMasterVolume(&back)) || std::fabs(back - h) <= kHushTolerance * h) kept = true;
        }
        if (kept) return true;
        if (rl.level + 1 >= kHushLevelCount) return true;
        ++rl.level;                                   // Windows really does not keep this depth: the boost follows
        rl.since = std::chrono::steady_clock::now();
    }
}

std::vector<SuppressResult> SessionVolumeGuard::suppressProcesses(const std::vector<unsigned long>& roots) {
    std::vector<SuppressResult> out(roots.size());
    if (roots.empty()) return out;
    std::lock_guard<std::mutex> lk(mu_);
    HRESULT hr = S_OK;
    auto devices = enumerateAllSessions(&hr);
    if (FAILED(hr)) return out;

    // Process snapshot (cached for ~150 ms). Refreshed at once if a session belongs to a process we have not seen yet
    // (a browser that just started a new helper process): that session must be hushed in this very pass.
    const auto* parents = &parentsLocked(false);
    for (auto& d : devices) {
        bool unknown = false;
        for (auto& s : d.sessions)
            if (s.pid != 0 && !s.isSystemSounds && parents->find(s.pid) == parents->end()) { unknown = true; break; }
        if (unknown) { parents = &parentsLocked(true); break; }
    }

    // Which routed root owns a process: the closest routed ancestor (or the process itself).
    std::unordered_map<unsigned long, size_t> rootIndex;
    rootIndex.reserve(roots.size() * 2);
    for (size_t k = 0; k < roots.size(); ++k) rootIndex.emplace(roots[k], k);
    auto ownerOf = [&](unsigned long pid) -> long {
        unsigned long cur = pid;
        for (int depth = 0; depth < 32; ++depth) {
            auto r = rootIndex.find(cur);
            if (r != rootIndex.end()) return static_cast<long>(r->second);
            auto it = parents->find(cur);
            if (it == parents->end() || it->second == 0 || it->second == cur) break;
            cur = it->second;
        }
        return -1;
    };

    // Index of the entries we already own: (device, instance) -> position. Linear scans per session do not scale
    // when a browser creates a new session per tab/video for hours.
    std::unordered_map<std::string, size_t> index;
    index.reserve(entries_.size() * 2 + 8);
    for (size_t i = 0; i < entries_.size(); ++i) index.emplace(entries_[i].j.deviceId + '\n' + entries_[i].j.instanceId, i);
    bool journalDirty = false;
    std::unordered_set<std::string> present;        // every session that exists right now (on enumerated devices)
    std::unordered_set<std::string> devicesSeen;
    for (auto& d : devices) {
        devicesSeen.insert(toUtf8(d.deviceId));
        for (auto& s : d.sessions) present.insert(toUtf8(s.deviceId) + '\n' + toUtf8(s.instanceId));
    }

    const auto now = std::chrono::steady_clock::now();
    std::vector<RootLevel*> levels(roots.size());
    for (size_t k = 0; k < roots.size(); ++k) {
        RootLevel& rl = rootLevel_[roots[k]];
        if (rl.level > 0 && now - rl.since > kLevelReprobe) { rl.level = 0; rl.since = now; }   // try the deep hush again
        levels[k] = &rl;
    }
    std::vector<int> newly(roots.size(), 0);

    for (auto& d : devices) {
        for (auto& s : d.sessions) {
            if (!s.volume || s.isSystemSounds) continue;
            const long k = ownerOf(s.pid);
            if (k < 0) continue;
            const unsigned long pid = roots[static_cast<size_t>(k)];
            RootLevel& rl = *levels[static_cast<size_t>(k)];
            const std::string inst = toUtf8(s.instanceId), dev = toUtf8(s.deviceId);
            BOOL muted = FALSE;
            if (FAILED(s.volume->GetMute(&muted)) || muted) continue;   // muted by the user: never touch
            float vol = 1.0f;
            if (FAILED(s.volume->GetMasterVolume(&vol))) continue;
            const float h = kHushLevels[rl.level];
            const bool atHush = std::fabs(vol - h) <= kHushTolerance * h;     // at the CURRENT depth of this root
            const bool atAnyHush = atHush || isHushLevel(vol);                // ... or at one of ours

            auto known = index.find(dev + '\n' + inst);
            if (known != index.end()) {
                // Already ours. Apps and Windows sometimes put the volume back (device change, app start-up,
                // volume normalisation, the user's volume slider): the original would then play loud in BOTH ears. Hush
                // it again and keep the value the app chose as the one to restore later.
                Entry& e = entries_[known->second];
                if (atHush) continue;
                if (!atAnyHush && std::fabs(e.j.originalVolume - vol) > 0.001f) { e.j.originalVolume = vol; journalDirty = true; }
                if (applyHush(s.volume.Get(), rl)) { e.applied = true; ++newly[static_cast<size_t>(k)]; }
                continue;
            }

            Entry e;
            e.root = pid;
            e.j.pid = static_cast<uint32_t>(s.pid);
            e.j.createTime = ProcessManager::creationTime(s.pid);
            e.j.originalMuted = false;
            // A session that is ALREADY at the hush volume but unknown to us was hushed by a previous run
            // (Windows remembers the volume per app, so a new session of the app starts hushed). It must be adopted:
            // otherwise it would never be boosted, i.e. the app would stay inaudible. Original volume is unknown: 1.0.
            e.j.originalVolume = atAnyHush ? 1.0f : vol;
            e.j.deviceId = dev;
            e.j.instanceId = inst;
            everHushed_ = true;
            if (atHush) {
                e.applied = true;
                entries_.push_back(e);
                index.emplace(dev + '\n' + inst, entries_.size() - 1);
                journalDirty = true;
                ++newly[static_cast<size_t>(k)];
                continue;
            }
            entries_.push_back(e);
            writeJournalLocked();                       // journal first, then act
            if (applyHush(s.volume.Get(), rl)) {
                entries_.back().applied = true;
                index.emplace(dev + '\n' + inst, entries_.size() - 1);
                ++newly[static_cast<size_t>(k)];
            } else {
                entries_.pop_back();
                writeJournalLocked();
            }
        }
    }
    for (size_t k = 0; k < roots.size(); ++k) {
        int total = 0;
        for (auto& e : entries_) if (e.root == roots[k] && e.applied) ++total;
        out[k] = {total, newly[k], levels[k]->level, kHushLevels[levels[k]->level]};
    }

    // Forget entries whose session no longer exists (tab closed, stream ended): nothing to restore, and the list
    // (and the journal file) must not grow for the whole lifetime of the app. Only judged on devices we really read.
    {
        const size_t before = entries_.size();
        for (auto it = entries_.begin(); it != entries_.end();) {
            const bool deviceRead = devicesSeen.count(it->j.deviceId) != 0;
            if (deviceRead && it->applied && !present.count(it->j.deviceId + '\n' + it->j.instanceId)) it = entries_.erase(it);
            else ++it;
        }
        if (entries_.size() != before) journalDirty = true;
    }
    if (journalDirty) writeJournalLocked();
    return out;
}

int SessionVolumeGuard::suppressProcess(unsigned long pid, int* newlyHushed) {
    const auto r = suppressProcesses({pid});
    if (newlyHushed) *newlyHushed = r[0].newly;
    return r[0].total;
}

int SessionVolumeGuard::restoreEntriesLocked(bool onlyPid, unsigned long pid) {
    if (entries_.empty()) return 0;
    HRESULT hr = S_OK;
    auto devices = enumerateAllSessions(&hr);
    int restored = 0;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (onlyPid && it->root != pid) { ++it; continue; }
        const std::wstring dev = fromUtf8(it->j.deviceId), inst = fromUtf8(it->j.instanceId);
        for (auto& d : devices) {
            if (d.deviceId != dev) continue;
            for (auto& s : d.sessions) {
                if (s.instanceId != inst || !s.volume) continue;
                float cur = 1.0f;
                if (SUCCEEDED(s.volume->GetMasterVolume(&cur)) && shouldRestoreVolume(it->applied, cur)) {
                    if (SUCCEEDED(s.volume->SetMasterVolume(it->j.originalVolume, nullptr))) ++restored;
                }
            }
        }
        it = entries_.erase(it);
    }
    writeJournalLocked();
    return restored;
}

int SessionVolumeGuard::releaseOrphansLocked(const std::set<unsigned long>& roots, const std::set<std::wstring>& exes) {
    HRESULT hr = S_OK;
    auto devices = enumerateAllSessions(&hr);
    if (FAILED(hr)) return 0;
    std::unordered_set<std::string> owned;           // sessions we currently manage: never judged here
    for (const auto& e : entries_) owned.insert(e.j.deviceId + '\n' + e.j.instanceId);
    bool freshSnapshot = false;                      // process snapshot is taken lazily: most passes find nothing hushed
    std::unordered_map<unsigned long, bool> verdict; // pid -> protected? (a hushed session is rare, but a pid repeats)
    auto isProtected = [&](unsigned long pid) {
        auto cached = verdict.find(pid);
        if (cached != verdict.end()) return cached->second;
        const auto& parents = parentsLocked(!freshSnapshot);
        freshSnapshot = true;
        bool prot = false;
        unsigned long cur = pid;
        for (int depth = 0; depth < 32 && !prot; ++depth) {
            if (roots.count(cur)) { prot = true; break; }
            if (!exes.empty() && exes.count(lowerPath(ProcessManager::exePath(cur)))) { prot = true; break; }
            auto it = parents.find(cur);
            if (it == parents.end() || it->second == 0 || it->second == cur) break;
            cur = it->second;
        }
        verdict.emplace(pid, prot);
        return prot;
    };
    int restored = 0;
    for (auto& d : devices) {
        for (auto& s : d.sessions) {
            if (!s.volume || s.isSystemSounds) continue;
            float cur = 1.0f;
            if (FAILED(s.volume->GetMasterVolume(&cur))) continue;
            if (!isHushLevel(cur)) continue;                                          // cheap test first
            if (owned.count(toUtf8(s.deviceId) + '\n' + toUtf8(s.instanceId))) continue;
            if (!shouldReleaseOrphan(s.isSystemSounds, isProtected(s.pid), cur)) continue;
            if (SUCCEEDED(s.volume->SetMasterVolume(1.0f, nullptr))) ++restored;
        }
    }
    return restored;
}

int SessionVolumeGuard::releaseOrphans(const std::set<unsigned long>& roots, const std::set<std::wstring>& exes) {
    std::lock_guard<std::mutex> lk(mu_);
    return releaseOrphansLocked(roots, exes);
}

void SessionVolumeGuard::restoreProcess(unsigned long pid) {
    std::lock_guard<std::mutex> lk(mu_);
    restoreEntriesLocked(true, pid);
    rootLevel_.erase(pid);
}

void SessionVolumeGuard::restoreAll() {
    std::lock_guard<std::mutex> lk(mu_);
    restoreEntriesLocked(false, 0);
    // Also sessions we no longer track (adopted ones whose entry was forgotten, apps that put the volume back): anything
    // still sitting at a hush depth now is a leftover and would stay nearly silent in both ears.
    if (everHushed_) releaseOrphansLocked({}, {});
    rootLevel_.clear();
}

bool SessionVolumeGuard::everHushed() const {
    std::lock_guard<std::mutex> lk(mu_);
    return everHushed_;
}

bool SessionVolumeGuard::isSuppressed(unsigned long pid) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& e : entries_) if (e.root == pid && e.applied) return true;
    return false;
}

} // namespace sf
