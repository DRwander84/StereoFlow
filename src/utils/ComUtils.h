#pragma once
// Windows-only helpers: COM init RAII, IUnknown boilerplate, HRESULT formatting, string helpers.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <wrl/client.h>
#include <atomic>
#include <string>

namespace sf {

using Microsoft::WRL::ComPtr;

class ComApartment {
public:
    explicit ComApartment(DWORD model = COINIT_MULTITHREADED) {
        hr_ = CoInitializeEx(nullptr, model);
        owns_ = SUCCEEDED(hr_);
    }
    ~ComApartment() { if (owns_) CoUninitialize(); }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
    HRESULT result() const { return hr_; }
    bool ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }
private:
    HRESULT hr_;
    bool owns_;
};

// Base for small COM objects implemented in this project.
template <class I>
class ComImpl : public I {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(I)) {
            *ppv = static_cast<I*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG c = --ref_;
        if (c == 0) delete this;
        return c;
    }
protected:
    virtual ~ComImpl() = default;
private:
    std::atomic<ULONG> ref_{1};
};

inline std::wstring hrMessage(HRESULT hr) {
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring text = n ? std::wstring(buf, n) : L"Unknown error";
    if (buf) LocalFree(buf);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) text.pop_back();
    wchar_t code[32];
    swprintf_s(code, L"0x%08X", static_cast<unsigned>(hr));
    return text + L" (" + code + L")";
}

// Friendly descriptions for the common Core Audio failure codes.
inline std::wstring audioHrMessage(HRESULT hr) {
    switch (static_cast<unsigned>(hr)) {
    case 0x88890004: return L"Audio device was invalidated (disconnected or changed) (0x88890004)";
    case 0x88890008: return L"Audio format is not supported by the device (0x88890008)";
    case 0x8889000A: return L"The audio device is in use by another application in exclusive mode (0x8889000A)";
    case 0x88890002: return L"Audio client was not initialised (0x88890002)";
    default: return hrMessage(hr);
    }
}

inline std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
inline std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

} // namespace sf
