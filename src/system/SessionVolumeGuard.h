#pragma once
#include "audio/Suppression.h"
#include "system/RecoveryJournal.h"
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

struct ISimpleAudioVolume;

namespace sf {

// Transactional control of the ORIGINAL (direct) audio of routed processes.
// We only ever change the Mute flag (never the volume slider) of sessions that belong to
// the processes the user explicitly routed. Every change is journaled to disk BEFORE it is
// applied so that a crash can be repaired on the next start.
struct SuppressResult {
    int total = -1;     // sessions hushed for this root (-1 = enumeration failed)
    int newly = 0;      // hushed by this very call
    int level = 0;      // hush depth in use for THIS root (every routed app has its own depth)
    float volume = kHushLevels[0];   // session volume of that depth; the boost is exactly 1 / volume
};

class SessionVolumeGuard {
public:
    explicit SessionVolumeGuard(std::filesystem::path journalPath);
    ~SessionVolumeGuard();

    // Repairs sessions left muted by a previous crashed run. Call once at startup.
    // Returns number of sessions restored.
    int recoverFromJournal();

    // Mutes every session owned by `pid` (idempotent, picks up newly created sessions).
    // Returns number of sessions currently muted by us for this pid, or -1 on failure.
    // Hushes (volume -> kHushVolume) the sessions of `pid` and of all its child processes (browsers and
    // Electron apps play from children). Returns how many sessions are hushed for this root (or -1);
    // `newlyHushed` receives how many were hushed by THIS call.
    int suppressProcess(unsigned long pid, int* newlyHushed = nullptr);
    // Same for many routed roots with ONE session enumeration (cheap, no per-app COM churn).
    std::vector<SuppressResult> suppressProcesses(const std::vector<unsigned long>& roots);

    // Gives their normal volume back to every session that is stuck at one of OUR hush depths although no routed app
    // owns it: an app that was closed while hushed (Windows remembers the volume per app, so its next start is almost
    // silent in BOTH ears), a crashed earlier run, an app that was un-assigned. Sessions inside the process tree of
    // `protectedRoots`, or belonging to an exe in `protectedExes` (lower-case full paths: routed apps that are about to
    // be picked up) are left alone. Returns how many sessions were restored.
    int releaseOrphans(const std::set<unsigned long>& protectedRoots, const std::set<std::wstring>& protectedExes);

    void restoreProcess(unsigned long pid);
    void restoreAll();                       // our entries + every leftover hushed session
    bool isSuppressed(unsigned long pid) const;
    bool everHushed() const;                 // StereoFlow hushed something during this run (decides on the restore watchdog)

private:
    struct Entry {
        JournalEntry j;
        bool applied = false;
        unsigned long root = 0;   // routed process; sessions of its whole process tree are muted
    };
    // Hush depth of one routed root. Steps shallower only if Windows really refuses the deeper volume, and is probed
    // again later: one unlucky read-back (the user dragging a slider) must never leave an app at -48 dB for good.
    struct RootLevel {
        int level = 0;
        std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
    };
    bool applyHush(ISimpleAudioVolume* v, RootLevel& rl);   // set + read back (3 tries) ; steps shallower if not kept
    void writeJournalLocked();
    int restoreEntriesLocked(bool onlyPid, unsigned long pid);
    int releaseOrphansLocked(const std::set<unsigned long>& roots, const std::set<std::wstring>& exes);
    const std::map<unsigned long, unsigned long>& parentsLocked(bool forceFresh);

    std::filesystem::path path_;
    mutable std::mutex mu_;
    std::vector<Entry> entries_;
    std::unordered_map<unsigned long, RootLevel> rootLevel_;
    std::map<unsigned long, unsigned long> parents_;       // process snapshot, refreshed at most every ~150 ms
    std::chrono::steady_clock::time_point parentsAt_{};
    bool everHushed_ = false;
};

} // namespace sf
