#pragma once
#include "utils/ComUtils.h"
#include <mmdeviceapi.h>
#include <QObject>

namespace sf {

class DeviceNotificationClient;

// Watches endpoint add/remove/state/default changes (IMMNotificationClient).
// Callbacks arrive on Windows' own device-notification thread and are marshalled to Qt.
class DeviceMonitor : public QObject {
    Q_OBJECT
public:
    explicit DeviceMonitor(QObject* parent = nullptr);
    ~DeviceMonitor() override;
    bool start();
    void stop();
signals:
    void devicesChanged();                         // add / remove / state change
    void defaultRenderChanged(const QString& id);
    void deviceRemovedOrDisabled(const QString& id);
private:
    ComPtr<IMMDeviceEnumerator> enumerator_;
    DeviceNotificationClient* client_ = nullptr;
};

} // namespace sf
