#include "app/Application.h"
#include "system/WindowsVersion.h"
#include "utils/Log.h"
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <cwctype>
#include <set>
#include <QMessageBox>
#include <QStandardPaths>

namespace sf {

static SessionVolumeGuard* g_guardForCrash = nullptr;

static LONG WINAPI crashFilter(EXCEPTION_POINTERS*) {
    // Best effort: make sure no app is left muted if we crash. The on-disk journal covers hard kills.
    if (g_guardForCrash) g_guardForCrash->restoreAll();
    return EXCEPTION_CONTINUE_SEARCH;
}

Application::Application() = default;

Application::~Application() {
    sweepTimer_.stop();
    if (router_) router_->shutdown();     // stop capture and un-mute every app before anything else goes away
    // An app that was routed may have NO audio session right now (idle browser, paused player): there is nothing to
    // restore at this moment, yet Windows keeps its hushed volume and the app would start almost silent, in both ears.
    // A tiny headless copy of this exe keeps watching for a while and restores such apps as soon as they show up.
    if (guard_ && guard_->everHushed())
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--restore-watch")});
    g_guardForCrash = nullptr;
}

void Application::sweepOrphans(bool protectAssigned) {
    if (!guard_) return;
    std::set<unsigned long> roots;
    std::set<std::wstring> exes;
    if (engine_) for (quint32 pid : engine_->sourcePids()) roots.insert(pid);
    if (protectAssigned) {
        // Routed apps are picked up (and re-hushed) by the engine within a couple of seconds: do not flash them loud.
        const auto list = settings_ ? settings_->assignments() : QList<Assignment>();
        for (const auto& a : list) exes.insert(a.key.toLower().toStdWString());
    }
    const int n = guard_->releaseOrphans(roots, exes);
    if (n > 0) logLine(QString("restored %1 leftover hushed session(s)").arg(n));
}

bool Application::initialise() {
    const auto ver = queryWindowsVersion();
    if (!ver.processLoopbackSupported) {
        QMessageBox::critical(nullptr, "StereoFlow - Unsupported Windows version",
            QString("Per-application audio capture needs Windows 10 build %1 or newer / Windows 11.\n"
                    "This system is %2.\n\nStereoFlow will not run in a degraded or unsafe mode.")
                .arg(kMinProcessLoopbackBuild).arg(QString::fromStdWString(ver.description)));
        return false;
    }
    settings_ = std::make_unique<AppSettings>();

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    guard_ = std::make_unique<SessionVolumeGuard>(std::filesystem::path(QDir(dir).filePath("volume-journal.txt").toStdWString()));
    g_guardForCrash = guard_.get();
    SetUnhandledExceptionFilter(crashFilter);
    guard_->recoverFromJournal();     // repair anything left muted by a previous crash
    sweepOrphans(true);               // ... and apps left hushed by a closed / killed earlier run

    engine_ = std::make_unique<AudioEngine>(guard_.get());
    router_ = std::make_unique<StereoRouter>(engine_.get(), settings_.get());
    window_ = std::make_unique<MainWindow>(router_.get(), settings_.get());

    // Follow the Windows default output device (e.g. headphones plugged in).
    devices_ = std::make_unique<DeviceMonitor>();
    devices_->start();
    connect(devices_.get(), &DeviceMonitor::defaultRenderChanged, this, [this](const QString&) {
        if (engine_->state() != AudioEngine::State::Stopped) engine_->reinitialiseDevice();
    });
    connect(devices_.get(), &DeviceMonitor::deviceRemovedOrDisabled, this, [this](const QString& id) {
        Q_UNUSED(id);
        if (engine_->state() != AudioEngine::State::Stopped)
            window_->showMessage("Output device disconnected. Waiting for it to return...");
    });
    // Apps that were closed while hushed come back at the hushed volume (Windows remembers it per app): this restores them
    // within ~2 s of their first sound, also when they are not routed any more.
    sweepTimer_.setInterval(2000);
    connect(&sweepTimer_, &QTimer::timeout, this, [this] {
        const auto st = engine_->state();
        sweepOrphans(st != AudioEngine::State::Stopped && st != AudioEngine::State::Error);
    });
    sweepTimer_.start();
    return true;
}

void Application::show() { window_->show(); }

} // namespace sf
