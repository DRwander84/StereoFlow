#include "system/WindowsVersion.h"
#include "utils/ComUtils.h"

namespace sf {

WindowsVersionInfo queryWindowsVersion() {
    WindowsVersionInfo v;
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto fn = nt ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(nt, "RtlGetVersion")) : nullptr;
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (fn && fn(&info) == 0) {
        v.major = info.dwMajorVersion;
        v.minor = info.dwMinorVersion;
        v.build = info.dwBuildNumber;
    }
    v.processLoopbackSupported = (v.major > 10) || (v.major == 10 && v.build >= kMinProcessLoopbackBuild);
    v.description = L"Windows " + std::to_wstring(v.major) + L"." + std::to_wstring(v.minor) +
                    L" build " + std::to_wstring(v.build);
    return v;
}

} // namespace sf
