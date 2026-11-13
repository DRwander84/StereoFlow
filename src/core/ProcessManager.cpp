#include "core/ProcessManager.h"
#include "utils/ComUtils.h"
#include <shellapi.h>
#include <winver.h>
#include <tlhelp32.h>
#include <filesystem>
#include <vector>
#include <wchar.h>

namespace sf {

namespace {
struct HandleCloser { void operator()(HANDLE h) const { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); } };
}

std::wstring ProcessManager::exePath(unsigned long pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    std::wstring buf(1024, L'\0');
    DWORD size = (DWORD)buf.size();
    std::wstring result;
    if (QueryFullProcessImageNameW(h, 0, buf.data(), &size)) result.assign(buf.data(), size);
    CloseHandle(h);
    return result;
}

uint64_t ProcessManager::creationTime(unsigned long pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    FILETIME c, e, k, u;
    uint64_t t = 0;
    if (GetProcessTimes(h, &c, &e, &k, &u)) t = (uint64_t(c.dwHighDateTime) << 32) | c.dwLowDateTime;
    CloseHandle(h);
    return t;
}

bool ProcessManager::isAlive(unsigned long pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

std::vector<unsigned long> ProcessManager::pidsForExe(const std::wstring& path) {
    std::vector<unsigned long> out;
    if (path.empty()) return out;
    const std::wstring name = std::filesystem::path(path).filename().wstring();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name.c_str()) != 0) continue;
            const std::wstring full = exePath(pe.th32ProcessID);
            // Protected / elevated processes refuse the path query: accept them by file name instead of silently
            // never routing (and never hushing) that app.
            if (full.empty() || _wcsicmp(full.c_str(), path.c_str()) == 0) out.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

std::map<unsigned long, unsigned long> ProcessManager::parentMap() {
    std::map<unsigned long, unsigned long> m;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return m;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do { m[pe.th32ProcessID] = pe.th32ParentProcessID; } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return m;
}

bool ProcessManager::hasAncestorIn(unsigned long pid, const std::map<unsigned long, unsigned long>& parents,
                                   const std::map<unsigned long, bool>& candidates) {
    unsigned long cur = pid;
    for (int depth = 0; depth < 32; ++depth) {
        auto it = parents.find(cur);
        if (it == parents.end()) return false;
        cur = it->second;
        if (cur == 0 || cur == pid) return false;
        if (candidates.count(cur)) return true;
    }
    return false;
}

QString ProcessManager::friendlyName(const std::wstring& path) {
    QString fallback = QString::fromStdWString(std::filesystem::path(path).stem().wstring());
    if (path.empty()) return fallback;
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (!size) return fallback;
    std::vector<char> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return fallback;
    struct LangCp { WORD lang, cp; } *tr = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&tr), &len) || len < sizeof(LangCp))
        return fallback;
    wchar_t key[96];
    swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr->lang, tr->cp);
    wchar_t* desc = nullptr;
    if (VerQueryValueW(data.data(), key, reinterpret_cast<LPVOID*>(&desc), &len) && desc && len > 1) {
        QString s = QString::fromWCharArray(desc).trimmed();
        if (!s.isEmpty()) return s;
    }
    return fallback;
}

static QImage hiconToImage(HICON icon) {
    ICONINFO ii{};
    if (!GetIconInfo(icon, &ii)) return {};
    QImage img;
    BITMAP bm{};
    if (ii.hbmColor && GetObjectW(ii.hbmColor, sizeof(bm), &bm)) {
        const int w = bm.bmWidth, h = bm.bmHeight;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        QImage tmp(w, h, QImage::Format_ARGB32);
        HDC dc = GetDC(nullptr);
        if (GetDIBits(dc, ii.hbmColor, 0, h, tmp.bits(), &bi, DIB_RGB_COLORS)) {
            bool anyAlpha = false;
            for (int y = 0; y < h && !anyAlpha; ++y) {
                const QRgb* line = reinterpret_cast<const QRgb*>(tmp.constScanLine(y));
                for (int x = 0; x < w; ++x) if (qAlpha(line[x])) { anyAlpha = true; break; }
            }
            img = anyAlpha ? tmp : tmp.convertToFormat(QImage::Format_RGB32);
        }
        ReleaseDC(nullptr, dc);
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return img;
}

QImage ProcessManager::icon(const std::wstring& path) {
    if (path.empty()) return {};
    HICON large = nullptr;
    if (ExtractIconExW(path.c_str(), 0, &large, nullptr, 1) < 1 || !large) return {};
    QImage img = hiconToImage(large);
    DestroyIcon(large);
    return img;
}

} // namespace sf
