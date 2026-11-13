#pragma once
#include <string>

namespace sf {

struct WindowsVersionInfo {
    unsigned major = 0, minor = 0, build = 0;
    bool processLoopbackSupported = false;
    std::wstring description;
};

// Process Loopback Capture (ActivateAudioInterfaceAsync / VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK)
// is documented for build 20348 / Windows 11, but the API is present and works since Windows 10 2004
// (build 19041), which is what OBS and other apps rely on. So 19041 is the real minimum.
constexpr unsigned kMinProcessLoopbackBuild = 19041;

WindowsVersionInfo queryWindowsVersion();

} // namespace sf
