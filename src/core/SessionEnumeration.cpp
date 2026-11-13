#include "core/SessionEnumeration.h"

namespace sf {

static std::wstring takeString(LPWSTR p) {
    std::wstring s = p ? p : L"";
    if (p) CoTaskMemFree(p);
    return s;
}

std::vector<DeviceSessions> enumerateAllSessions(HRESULT* hrOut) {
    std::vector<DeviceSessions> result;
    ComPtr<IMMDeviceEnumerator> en;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    if (FAILED(hr)) { if (hrOut) *hrOut = hr; return result; }
    ComPtr<IMMDeviceCollection> devs;
    hr = en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devs);
    if (FAILED(hr)) { if (hrOut) *hrOut = hr; return result; }
    UINT count = 0;
    devs->GetCount(&count);
    for (UINT d = 0; d < count; ++d) {
        ComPtr<IMMDevice> dev;
        if (FAILED(devs->Item(d, &dev))) continue;
        LPWSTR idRaw = nullptr;
        if (FAILED(dev->GetId(&idRaw))) continue;
        DeviceSessions ds;
        ds.deviceId = takeString(idRaw);
        if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &ds.manager))) continue;
        ComPtr<IAudioSessionEnumerator> se;
        if (FAILED(ds.manager->GetSessionEnumerator(&se))) continue;
        int n = 0;
        se->GetCount(&n);
        for (int i = 0; i < n; ++i) {
            SessionHandle h;
            h.deviceId = ds.deviceId;
            if (FAILED(se->GetSession(i, &h.ctl))) continue;
            if (FAILED(h.ctl.As(&h.ctl2))) continue;
            h.ctl.As(&h.volume);
            h.ctl.As(&h.meter);
            h.isSystemSounds = (h.ctl2->IsSystemSoundsSession() == S_OK);
            h.ctl2->GetProcessId(&h.pid);
            h.ctl->GetState(&h.state);
            LPWSTR p = nullptr;
            if (SUCCEEDED(h.ctl2->GetSessionInstanceIdentifier(&p))) h.instanceId = takeString(p);
            p = nullptr;
            if (SUCCEEDED(h.ctl->GetDisplayName(&p))) h.displayName = takeString(p);
            ds.sessions.push_back(std::move(h));
        }
        result.push_back(std::move(ds));
    }
    if (hrOut) *hrOut = S_OK;
    return result;
}

} // namespace sf
