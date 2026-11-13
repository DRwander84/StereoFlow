#pragma once
#include "audio/AudioEngine.h"
#include "core/DeviceMonitor.h"
#include "routing/StereoRouter.h"
#include "system/SessionVolumeGuard.h"
#include "ui/MainWindow.h"
#include "utils/AppSettings.h"
#include <QObject>
#include <QTimer>
#include <memory>

namespace sf {

class Application : public QObject {
    Q_OBJECT
public:
    Application();
    ~Application() override;
    // Returns false if the system cannot run StereoFlow (message already shown).
    bool initialise();
    void show();
private:
    void sweepOrphans(bool protectAssigned);   // give hushed leftovers of closed / un-assigned apps their sound back
    // Declaration order = construction order; destruction runs backwards (window first, engine/guard last).
    std::unique_ptr<AppSettings> settings_;
    std::unique_ptr<SessionVolumeGuard> guard_;
    std::unique_ptr<AudioEngine> engine_;
    std::unique_ptr<DeviceMonitor> devices_;
    std::unique_ptr<StereoRouter> router_;
    std::unique_ptr<MainWindow> window_;
    QTimer sweepTimer_;
};

} // namespace sf
