#pragma once
#include <QString>

namespace sf {

struct LaunchTarget {
    QString exePath;      // native path of the .exe that the dropped item starts (empty on failure)
    bool isShortcut = false;
    QString error;
};

// Resolves a dropped item to the executable it runs. Accepts .exe and .lnk (IShellLink: environment variables,
// advertised/MSI shortcuts and moved targets are handled, which QFileInfo::symLinkTarget cannot do).
// Must be called on a COM-initialised thread (the Qt GUI thread is).
LaunchTarget resolveLaunchTarget(const QString& path);

// True when this process runs with administrator rights (Explorer cannot drop files onto it then).
bool isProcessElevated();

} // namespace sf
