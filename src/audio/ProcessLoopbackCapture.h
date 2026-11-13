#pragma once
#include "audio/AudioSource.h"
#include "utils/ComUtils.h"
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace sf {

enum class CaptureFailure { ProcessExited, CaptureError };

// Captures the audio of ONE process tree (PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE)
// via ActivateAudioInterfaceAsync and pushes it into an AudioSource from a dedicated thread.
class ProcessLoopbackCapture {
public:
    using FailureCallback = std::function<void(unsigned long pid, CaptureFailure kind, HRESULT hr)>;

    ProcessLoopbackCapture(unsigned long pid, std::shared_ptr<AudioSource> source, FailureCallback cb);
    ~ProcessLoopbackCapture();

    // Blocks (max ~6 s) until the stream is running or failed. Returns S_OK on success.
    HRESULT start();
    void stop();

    unsigned long pid() const { return pid_; }
    bool running() const { return running_.load(); }
    std::wstring formatDescription() const;
    bool isFloat() const { return isFloat_; }   // 32-bit float capture (required for the deep hush + boost)
    uint64_t discontinuities() const { return discontinuities_.load(); }
    std::wstring failureStage() const { return stage_; }   // which step failed (valid after start() failed)

private:
    void threadMain();
    HRESULT initialise();                      // runs on capture thread
    HRESULT activateWith(const WAVEFORMATEX& fmt, int variant, ComPtr<IAudioClient>& out);

    unsigned long pid_;
    std::shared_ptr<AudioSource> source_;
    FailureCallback onFailure_;
    std::thread thread_;
    HANDLE stopEvent_ = nullptr;
    HANDLE readyEvent_ = nullptr;
    HANDLE sampleEvent_ = nullptr;
    HANDLE processHandle_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<uint64_t> discontinuities_{0};
    HRESULT initResult_ = E_FAIL;
    std::wstring stage_;

    ComPtr<IAudioClient> client_;
    ComPtr<IAudioCaptureClient> capture_;
    uint32_t rate_ = 0, channels_ = 0, bits_ = 0;
    bool isFloat_ = false;
};

} // namespace sf
