#include "core/WindowEnumerator.h"
#include "core/ProcessManager.h"
#include <QApplication>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QStyle>
#include <algorithm>
#include <cwchar>
#include <windows.h>
#include <dwmapi.h>

namespace sf {

namespace {

struct EnumCtx {
    QList<WindowInfo>* out;
    quint32 exclude;
};

QHash<QString, QString>& nameCache() { static QHash<QString, QString> c; return c; }
QHash<QString, QIcon>& iconCache() { static QHash<QString, QIcon> c; return c; }

// Store apps (UWP) are hosted by ApplicationFrameHost.exe; the real app owns a child window.
DWORD resolvePid(HWND h, DWORD pid, const wchar_t* cls) {
    if (wcscmp(cls, L"ApplicationFrameWindow") != 0) return pid;
    struct Found { DWORD frame; DWORD pid; } f{pid, 0};
    EnumChildWindows(h, [](HWND child, LPARAM lp) -> BOOL {
        auto* fo = reinterpret_cast<Found*>(lp);
        DWORD p = 0;
        GetWindowThreadProcessId(child, &p);
        if (p && p != fo->frame) { fo->pid = p; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&f));
    return f.pid ? f.pid : pid;
}

BOOL CALLBACK enumProc(HWND h, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER) != nullptr) return TRUE;
    const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) return TRUE;

    int cloaked = 0;   // windows on other virtual desktops / suspended Store apps
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;

    wchar_t title[256] = {};
    if (GetWindowTextW(h, title, 256) <= 0) return TRUE;
    wchar_t cls[128] = {};
    GetClassNameW(h, cls, 128);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") ||
        !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd")) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    pid = resolvePid(h, pid, cls);
    if (!pid || pid == ctx->exclude) return TRUE;

    const std::wstring path = ProcessManager::exePath(pid);
    if (path.empty()) return TRUE;

    WindowInfo w;
    w.exePath = QString::fromStdWString(path);
    w.key = w.exePath.toLower();
    w.pid = pid;
    w.title = QString::fromWCharArray(title);
    auto it = nameCache().find(w.key);
    if (it == nameCache().end()) it = nameCache().insert(w.key, ProcessManager::friendlyName(path));
    w.appName = it.value();
    ctx->out->append(w);
    return TRUE;
}

} // namespace

QList<WindowInfo> enumerateAppWindows(quint32 excludePid) {
    QList<WindowInfo> out;
    EnumCtx ctx{&out, excludePid};
    EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx));
    std::sort(out.begin(), out.end(), [](const WindowInfo& a, const WindowInfo& b) {
        const int c = a.appName.compare(b.appName, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QIcon appIcon(const QString& exePath) {
    const QString key = exePath.toLower();
    auto it = iconCache().find(key);
    if (it != iconCache().end()) return it.value();
    QIcon icon;
    const QImage img = ProcessManager::icon(exePath.toStdWString());
    if (!img.isNull()) icon = QIcon(QPixmap::fromImage(img));
    else icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    iconCache().insert(key, icon);
    return icon;
}

} // namespace sf
