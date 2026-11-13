#pragma once
#include "utils/ComUtils.h"
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <string>
#include <vector>

namespace sf {

struct SessionHandle {
    std::wstring deviceId;
    ComPtr<IAudioSessionControl> ctl;
    ComPtr<IAudioSessionControl2> ctl2;
    ComPtr<ISimpleAudioVolume> volume;
    ComPtr<IAudioMeterInformation> meter;
    DWORD pid = 0;
    bool isSystemSounds = false;
    AudioSessionState state = AudioSessionStateInactive;
    std::wstring instanceId;
    std::wstring displayName;
};

struct DeviceSessions {
    std::wstring deviceId;
    ComPtr<IAudioSessionManager2> manager;
    std::vector<SessionHandle> sessions;
};

// Enumerates sessions on all ACTIVE render endpoints. Must run on a COM-initialised thread.
std::vector<DeviceSessions> enumerateAllSessions(HRESULT* hrOut = nullptr);

} // namespace sf
