#pragma once
#include <QImage>
#include <QString>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sf {

class ProcessManager {
public:
    static std::wstring exePath(unsigned long pid);              // empty if inaccessible
    static uint64_t creationTime(unsigned long pid);             // FILETIME as 64-bit, 0 if unknown
    static bool isAlive(unsigned long pid);
    static std::vector<unsigned long> pidsForExe(const std::wstring& exePath);   // every running process of this exe
    static std::map<unsigned long, unsigned long> parentMap();   // pid -> parent pid snapshot
    static QString friendlyName(const std::wstring& exePath);    // FileDescription or file stem
    static QImage icon(const std::wstring& exePath);             // 32x32 icon, may be null
    // True if `pid` has an ancestor contained in `candidates` (used to avoid duplicate tree captures).
    static bool hasAncestorIn(unsigned long pid, const std::map<unsigned long, unsigned long>& parents,
                              const std::map<unsigned long, bool>& candidates);
};

} // namespace sf
