#include "core/ShortcutResolver.h"
#include "utils/ComUtils.h"
#include <QDir>
#include <QFileInfo>
#include <shlobj.h>
#include <shobjidl.h>

namespace sf {

namespace {

QString expandEnv(const wchar_t* in) {
    wchar_t out[2048] = {};
    const DWORD n = ExpandEnvironmentStringsW(in, out, 2048);
    return (n > 0 && n <= 2048) ? QString::fromWCharArray(out) : QString::fromWCharArray(in);
}

bool isRealExe(const QString& p) {
    const QFileInfo fi(p);
    return !p.isEmpty() && fi.exists() && fi.isFile() && fi.suffix().compare("exe", Qt::CaseInsensitive) == 0;
}

} // namespace

LaunchTarget resolveLaunchTarget(const QString& path) {
    LaunchTarget t;
    const QFileInfo fi(path);
    if (!fi.exists()) { t.error = fi.fileName() + ": file not found"; return t; }
    const QString suffix = fi.suffix().toLower();
    if (suffix == "exe") {
        t.exePath = QDir::toNativeSeparators(fi.absoluteFilePath());
        return t;
    }
    if (suffix != "lnk") { t.error = fi.fileName() + ": drop an app (.exe) or its shortcut (.lnk)"; return t; }
    t.isShortcut = true;

    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        t.error = "Windows could not open the shortcut"; return t;
    }
    ComPtr<IPersistFile> file;
    if (FAILED(link.As(&file)) || FAILED(file->Load(QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString().c_str(), STGM_READ))) {
        t.error = fi.fileName() + ": the shortcut could not be read"; return t;
    }
    // No UI, no slow disk search; a moved target is still found through the tracking data.
    link->Resolve(nullptr, SLR_NO_UI | SLR_NOSEARCH | SLR_NOUPDATE | (3000 << 16));

    wchar_t buf[MAX_PATH * 2] = {};
    QString target;
    if (SUCCEEDED(link->GetPath(buf, MAX_PATH * 2, nullptr, SLGP_RAWPATH)) && buf[0]) target = expandEnv(buf);
    if (!isRealExe(target)) {
        // Advertised / shell-item shortcuts keep the target only as an ID list.
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(link->GetIDList(&pidl)) && pidl) {
            wchar_t p2[MAX_PATH * 2] = {};
            if (SHGetPathFromIDListEx(pidl, p2, MAX_PATH * 2, GPFIDL_DEFAULT)) target = QString::fromWCharArray(p2);
            CoTaskMemFree(pidl);
        }
    }
    if (!isRealExe(target)) {
        const QFileInfo ti(target);
        t.error = fi.fileName() + (ti.suffix().isEmpty() ? ": this shortcut does not point to a desktop app (Store apps: open it, then pick it in Applications)"
                                                         : ": the shortcut target is not an .exe");
        return t;
    }
    // Shell hosts are never the app the user means (Store apps open through explorer.exe).
    if (QFileInfo(target).fileName().compare("explorer.exe", Qt::CaseInsensitive) == 0) {
        t.error = fi.fileName() + ": Store app shortcut. Open the app, then pick it in Applications.";
        return t;
    }
    t.exePath = QDir::toNativeSeparators(QFileInfo(target).absoluteFilePath());
    return t;
}

bool isProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION e{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &e, sizeof(e), &size) != 0;
    CloseHandle(token);
    return ok && e.TokenIsElevated != 0;
}

} // namespace sf
