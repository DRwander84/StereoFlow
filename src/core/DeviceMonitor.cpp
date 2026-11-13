#include "core/DeviceMonitor.h"
#include <QMetaObject>

namespace sf {

class DeviceNotificationClient final : public ComImpl<IMMNotificationClient> {
public:
    explicit DeviceNotificationClient(DeviceMonitor* m) : mon_(m) {}
    void detach() { mon_.store(nullptr); }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD state) override {
        const QString qid = QString::fromWCharArray(id);
        post([qid, state](DeviceMonitor* m) {
            emit m->devicesChanged();
            if (state != DEVICE_STATE_ACTIVE) emit m->deviceRemovedOrDisabled(qid);
        });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { post([](DeviceMonitor* m) { emit m->devicesChanged(); }); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override {
        const QString qid = QString::fromWCharArray(id);
        post([qid](DeviceMonitor* m) { emit m->devicesChanged(); emit m->deviceRemovedOrDisabled(qid); });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR id) override {
        if (flow == eRender && role == eConsole) {
            const QString qid = id ? QString::fromWCharArray(id) : QString();
            post([qid](DeviceMonitor* m) { emit m->defaultRenderChanged(qid); emit m->devicesChanged(); });
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    template <class F> void post(F f) {
        DeviceMonitor* m = mon_.load();
        if (m) QMetaObject::invokeMethod(m, [m, f]() { f(m); }, Qt::QueuedConnection);
    }
    std::atomic<DeviceMonitor*> mon_;
};

DeviceMonitor::DeviceMonitor(QObject* parent) : QObject(parent) {}
DeviceMonitor::~DeviceMonitor() { stop(); }

bool DeviceMonitor::start() {
    if (enumerator_) return true;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_)))) return false;
    client_ = new DeviceNotificationClient(this);
    if (FAILED(enumerator_->RegisterEndpointNotificationCallback(client_))) {
        client_->Release();
        client_ = nullptr;
        enumerator_.Reset();
        return false;
    }
    return true;
}

void DeviceMonitor::stop() {
    if (!enumerator_) return;
    if (client_) {
        client_->detach();
        enumerator_->UnregisterEndpointNotificationCallback(client_);
        client_->Release();
        client_ = nullptr;
    }
    enumerator_.Reset();
}

} // namespace sf
