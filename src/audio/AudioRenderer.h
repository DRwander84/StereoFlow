#pragma once
#include "audio/AudioMixer.h"
#include "utils/ComUtils.h"
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace sf {

// WASAPI shared-mode, event-driven renderer. Pulls interleaved stereo from the mixer.
class AudioRenderer {
public:
    using FailureCallback = std::function<void(HRESULT hr)>;

    AudioRenderer(AudioMixer& mixer, FailureCallback cb);
    ~AudioRenderer();

    // deviceId empty => default render device. Blocks until running or failed.
    HRESULT start(const std::wstring& deviceId, unsigned bufferMs);
    void stop();
    void setPaused(bool p) { paused_.store(p); }

    uint32_t sampleRate() const { return rate_; }
    uint32_t channels() const { return channels_; }
    bool isMono() const { return channels_ == 1; }
    double latencyMs() const { return latencyMs_; }
    double bufferMs() const { return bufferMsActual_; }
    std::wstring deviceId() const { return deviceId_; }
    uint64_t renderErrors() const { return errors_.load(); }
    bool running() const { return running_.load(); }

private:
    void threadMain(std::wstring deviceId, unsigned bufferMs);
    void writeFrames(const float* stereo, size_t n, float* dst) const noexcept;
    void convertToPcm(const float* src, size_t count, BYTE* dst) const noexcept;
    HRESULT initialise(const std::wstring& deviceId, unsigned bufferMs);

    AudioMixer& mixer_;
    FailureCallback onFailure_;
    std::thread thread_;
    HANDLE stopEvent_ = nullptr, readyEvent_ = nullptr, bufferEvent_ = nullptr;
    std::atomic<bool> running_{false}, stopping_{false}, paused_{false};
    std::atomic<uint64_t> errors_{0};
    HRESULT initResult_ = E_FAIL;

    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioRenderClient> render_;
    UINT32 bufferFrames_ = 0;
    uint32_t rate_ = 48000, channels_ = 2;
    uint32_t containerBytes_ = 4;      // bytes per sample of the device mix format
    bool deviceFloat_ = true;          // float32 (the normal case) or integer PCM 16/24/32
    double latencyMs_ = 0, bufferMsActual_ = 0;
    std::wstring deviceId_;
};

} // namespace sf
