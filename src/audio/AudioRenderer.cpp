#include "audio/AudioRenderer.h"
#include <xmmintrin.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <algorithm>
#include <vector>

namespace sf {

static const GUID kSubtypeIeeeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

AudioRenderer::AudioRenderer(AudioMixer& m, FailureCallback cb) : mixer_(m), onFailure_(std::move(cb)) {
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bufferEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

AudioRenderer::~AudioRenderer() {
    stop();
    for (HANDLE h : {stopEvent_, readyEvent_, bufferEvent_}) if (h) CloseHandle(h);
}

HRESULT AudioRenderer::start(const std::wstring& deviceId, unsigned bufferMs) {
    if (thread_.joinable()) return E_ILLEGAL_METHOD_CALL;
    ResetEvent(stopEvent_);
    ResetEvent(readyEvent_);
    stopping_ = false;
    thread_ = std::thread([this, deviceId, bufferMs] { threadMain(deviceId, bufferMs); });
    if (WaitForSingleObject(readyEvent_, 6000) != WAIT_OBJECT_0) { stop(); return HRESULT_FROM_WIN32(ERROR_TIMEOUT); }
    if (FAILED(initResult_)) { stop(); return initResult_; }
    return S_OK;
}

void AudioRenderer::stop() {
    stopping_ = true;
    if (stopEvent_) SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    running_ = false;
}

HRESULT AudioRenderer::initialise(const std::wstring& deviceId, unsigned bufferMs) {
    ComPtr<IMMDeviceEnumerator> en;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    if (FAILED(hr)) return hr;
    hr = deviceId.empty() ? en->GetDefaultAudioEndpoint(eRender, eConsole, &device_)
                          : en->GetDevice(deviceId.c_str(), &device_);
    if (FAILED(hr)) return hr;
    LPWSTR id = nullptr;
    if (SUCCEEDED(device_->GetId(&id))) { deviceId_ = id; CoTaskMemFree(id); }

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client_);
    if (FAILED(hr)) return hr;

    WAVEFORMATEX* mix = nullptr;
    hr = client_->GetMixFormat(&mix);
    if (FAILED(hr)) return hr;
    bool isFloat = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->cbSize >= 22)
        isFloat = IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->SubFormat, kSubtypeIeeeFloat) != 0;
    rate_ = mix->nSamplesPerSec;
    channels_ = mix->nChannels;
    const uint32_t bits = mix->wBitsPerSample;
    CoTaskMemFree(mix);
    // Some wired/Bluetooth/USB headset drivers publish an integer mix format. Support those too instead of failing.
    const bool formatOk = isFloat ? (bits == 32) : (bits == 16 || bits == 24 || bits == 32);
    if (!formatOk || channels_ == 0) return AUDCLNT_E_UNSUPPORTED_FORMAT;  // Device Format Not Supported
    deviceFloat_ = isFloat;
    containerBytes_ = bits / 8;

    REFERENCE_TIME defPeriod = 0, minPeriod = 0;
    client_->GetDevicePeriod(&defPeriod, &minPeriod);
    const REFERENCE_TIME wanted = static_cast<REFERENCE_TIME>(bufferMs) * 10000;
    const REFERENCE_TIME duration = std::max(wanted, defPeriod);

    // Re-fetch the format pointer for Initialize (GetMixFormat memory was freed above)
    WAVEFORMATEX* fmt = nullptr;
    hr = client_->GetMixFormat(&fmt);
    if (FAILED(hr)) return hr;
    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, duration, 0, fmt, nullptr);
    CoTaskMemFree(fmt);
    if (FAILED(hr)) return hr;

    hr = client_->SetEventHandle(bufferEvent_);
    if (FAILED(hr)) return hr;
    hr = client_->GetBufferSize(&bufferFrames_);
    if (FAILED(hr)) return hr;
    hr = client_->GetService(IID_PPV_ARGS(&render_));
    if (FAILED(hr)) return hr;

    REFERENCE_TIME streamLat = 0;
    client_->GetStreamLatency(&streamLat);
    bufferMsActual_ = 1000.0 * bufferFrames_ / rate_;
    latencyMs_ = bufferMsActual_ + streamLat / 10000.0;

    // pre-fill with silence so Start() does not underrun
    BYTE* data = nullptr;
    hr = render_->GetBuffer(bufferFrames_, &data);
    if (FAILED(hr)) return hr;
    render_->ReleaseBuffer(bufferFrames_, AUDCLNT_BUFFERFLAGS_SILENT);
    mixer_.setSampleRate(rate_);
    return client_->Start();
}

// stereo -> the device channel layout (mono: L+R downmix, >2 channels: FL/FR only, the rest silent).
void AudioRenderer::writeFrames(const float* stereo, size_t n, float* dst) const noexcept {
    if (channels_ == 2) {
        std::copy(stereo, stereo + n * 2, dst);
    } else if (channels_ == 1) {
        for (size_t i = 0; i < n; ++i) dst[i] = 0.5f * (stereo[2 * i] + stereo[2 * i + 1]);
    } else {
        for (size_t i = 0; i < n; ++i) {
            float* f = dst + i * channels_;
            f[0] = stereo[2 * i];
            f[1] = stereo[2 * i + 1];
            for (uint32_t c = 2; c < channels_; ++c) f[c] = 0.0f;
        }
    }
}

void AudioRenderer::convertToPcm(const float* src, size_t count, BYTE* dst) const noexcept {
    auto clip = [](float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); };
    switch (containerBytes_) {
    case 2: {
        int16_t* d = reinterpret_cast<int16_t*>(dst);
        for (size_t i = 0; i < count; ++i) d[i] = static_cast<int16_t>(clip(src[i]) * 32767.0f);
        break;
    }
    case 3:
        for (size_t i = 0; i < count; ++i) {
            const int32_t v = static_cast<int32_t>(clip(src[i]) * 8388607.0f);
            dst[3 * i] = static_cast<BYTE>(v & 0xFF);
            dst[3 * i + 1] = static_cast<BYTE>((v >> 8) & 0xFF);
            dst[3 * i + 2] = static_cast<BYTE>((v >> 16) & 0xFF);
        }
        break;
    default: {
        int32_t* d = reinterpret_cast<int32_t*>(dst);
        for (size_t i = 0; i < count; ++i) d[i] = static_cast<int32_t>(static_cast<double>(clip(src[i])) * 2147483647.0);
        break;
    }
    }
}

void AudioRenderer::threadMain(std::wstring deviceId, unsigned bufferMs) {
    _mm_setcsr(_mm_getcsr() | 0x8040);      // flush-to-zero + denormals-are-zero: no CPU spikes on decaying tails
    ComApartment com(COINIT_MULTITHREADED);
    initResult_ = com.ok() ? initialise(deviceId, bufferMs) : com.result();
    if (SUCCEEDED(initResult_)) running_ = true;
    SetEvent(readyEvent_);
    if (FAILED(initResult_)) { render_.Reset(); client_.Reset(); device_.Reset(); return; }

    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    constexpr size_t kChunk = AudioMixer::kMaxBlock;
    std::vector<float> stereo(kChunk * 2);
    std::vector<float> frames(deviceFloat_ ? 0 : kChunk * std::max<uint32_t>(channels_, 2));
    const HANDLE waits[2] = {stopEvent_, bufferEvent_};
    HRESULT failHr = S_OK;

    for (;;) {
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, 1000);
        if (w == WAIT_OBJECT_0) break;
        UINT32 padding = 0;
        HRESULT hr = client_->GetCurrentPadding(&padding);
        if (FAILED(hr)) { failHr = hr; break; }
        if (w == WAIT_TIMEOUT) continue;
        UINT32 avail = bufferFrames_ - padding;
        if (avail == 0) continue;
        BYTE* data = nullptr;
        hr = render_->GetBuffer(avail, &data);
        if (FAILED(hr)) { failHr = hr; break; }
        float* out = reinterpret_cast<float*>(data);
        if (paused_.load(std::memory_order_relaxed)) {
            render_->ReleaseBuffer(avail, AUDCLNT_BUFFERFLAGS_SILENT);
            continue;
        }
        UINT32 done = 0;
        while (done < avail) {
            const size_t n = std::min<size_t>(avail - done, kChunk);
            mixer_.mix(stereo.data(), n);
            if (deviceFloat_) {
                float* dst = out + size_t(done) * channels_;
                writeFrames(stereo.data(), n, dst);
            } else {
                // Integer device format: build the float frames first, then convert once.
                writeFrames(stereo.data(), n, frames.data());
                BYTE* dst = data + size_t(done) * channels_ * containerBytes_;
                convertToPcm(frames.data(), n * channels_, dst);
            }
            done += static_cast<UINT32>(n);
        }
        render_->ReleaseBuffer(avail, 0);
    }

    client_->Stop();
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    render_.Reset();
    client_.Reset();
    device_.Reset();
    running_ = false;
    if (FAILED(failHr) && !stopping_.load()) {
        errors_.fetch_add(1);
        if (onFailure_) onFailure_(failHr);
    }
}

} // namespace sf
