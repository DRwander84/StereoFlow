#pragma once
#include <QIcon>
#include <QList>
#include <QString>

namespace sf {

// One visible top-level application window.
struct WindowInfo {
    QString key;       // lower-case exe path: identifies the application across restarts
    QString appName;   // friendly application name
    QString title;     // window title
    QString exePath;
    quint32 pid = 0;
};

// All visible, user-facing top-level windows (taskbar-style), except those owned by `excludePid`.
QList<WindowInfo> enumerateAppWindows(quint32 excludePid);

// Application icon from the executable (cached). Never null.
QIcon appIcon(const QString& exePath);

} // namespace sf
