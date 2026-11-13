#include "app/Application.h"
#include "utils/ComUtils.h"
#include "system/SessionVolumeGuard.h"
#include "ui/Theme.h"
#include <QApplication>
#include <QMessageBox>
#include <cstring>
#include <windows.h>

// Headless mode started by StereoFlow when it quits: apps that were routed may have had no audio session at that
// moment, but Windows keeps their hushed volume and they would start almost silent. Watch for them for a while and give
// them their volume back. Ends by itself after 30 minutes, or at once when StereoFlow is started again.
static int runRestoreWatch() {
    sf::ComApartment com(COINIT_MULTITHREADED);
    sf::SessionVolumeGuard guard{std::filesystem::path()};
    const ULONGLONG started = GetTickCount64();
    Sleep(4000);                                    // the quitting instance still owns the mutex for a moment
    for (;;) {
        if (HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\StereoFlow.SingleInstance")) { CloseHandle(m); return 0; }
        guard.releaseOrphans({}, {});
        if (GetTickCount64() - started > 30ULL * 60ULL * 1000ULL) return 0;
        Sleep(1500);
    }
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--restore-watch") == 0) return runRestoreWatch();
    QApplication qt(argc, argv);
    QApplication::setApplicationName("StereoFlow");
    QApplication::setOrganizationName("StereoFlow");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setStyle("Fusion");
    QApplication::setWindowIcon(sf::theme::logoIcon());     // taskbar / Alt+Tab / window icon
    sf::theme::initFonts();

    // Only one instance: two routers would fight over the same sessions.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\StereoFlow.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        QMessageBox::information(nullptr, "StereoFlow", "StereoFlow is already running.");
        return 0;
    }

    int rc = 0;
    {
        sf::Application app;
        if (!app.initialise()) return 1;
        app.show();
        QObject::connect(&qt, &QApplication::aboutToQuit, [] { /* window close already restores audio */ });
        rc = qt.exec();
    }   // Application destructor: engine stops, guard restores every muted session
    if (mutex) CloseHandle(mutex);
    return rc;
}
