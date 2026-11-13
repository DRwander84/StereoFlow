#include "audio/ProcessLoopbackCapture.h"
#include "audio/AudioFormat.h"
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <algorithm>
#include <vector>

namespace sf {

namespace {

class ActivateHandler final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
public:
    ActivateHandler() { evt_ = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    HANDLE event() const { return evt_; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        else if (riid == __uuidof(IAgileObject))
            *ppv = static_cast<IAgileObject*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = --ref_;
        if (!c) delete this;
        return c;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override {
        SetEvent(evt_);
        return S_OK;
    }
private:
    ~ActivateHandler() { if (evt_) CloseHandle(evt_); }
    std::atomic<ULONG> ref_{1};
    HANDLE evt_ = nullptr;
};

} // namespace

ProcessLoopbackCapture::ProcessLoopbackCapture(unsigned long pid, std::shared_ptr<AudioSource> src, FailureCallback cb)
    : pid_(pid), source_(std::move(src)), onFailure_(std::move(cb)) {
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    sampleEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

ProcessLoopbackCapture::~ProcessLoopbackCapture() {
    stop();
    for (HANDLE h : {stopEvent_, readyEvent_, sampleEvent_}) if (h) CloseHandle(h);
}

HRESULT ProcessLoopbackCapture::start() {
    if (thread_.joinable()) return E_ILLEGAL_METHOD_CALL;
    processHandle_ = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid_));
    if (!processHandle_) {
        const DWORD err = GetLastError();
        // Access denied (elevated process): still capturable, we just cannot watch its exit handle.
        if (err != ERROR_ACCESS_DENIED) return HRESULT_FROM_WIN32(err == ERROR_INVALID_PARAMETER ? ERROR_NOT_FOUND : err);
    }
    ResetEvent(stopEvent_);
    ResetEvent(readyEvent_);
    stopping_ = false;
    thread_ = std::thread([this] { threadMain(); });
    if (WaitForSingleObject(readyEvent_, 6000) != WAIT_OBJECT_0) {
        stop();
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    if (FAILED(initResult_)) { stop(); return initResult_; }
    return S_OK;
}

void ProcessLoopbackCapture::stop() {
    stopping_ = true;
    if (stopEvent_) SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    if (processHandle_) { CloseHandle(processHandle_); processHandle_ = nullptr; }
    running_ = false;
}

std::wstring ProcessLoopbackCapture::formatDescription() const {
    return std::to_wstring(rate_) + L" Hz, " + std::to_wstring(channels_) + L" ch, " +
           (isFloat_ ? L"float" : L"PCM") + std::to_wstring(bits_);
}

HRESULT ProcessLoopbackCapture::activateWith(const WAVEFORMATEX& fmt, int variant, ComPtr<IAudioClient>& out) {
    stage_ = L"activate";
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = static_cast<DWORD>(pid_);
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv{};
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(params);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    ActivateHandler* handler = new ActivateHandler();
    ComPtr<IActivateAudioInterfaceAsyncOperation> op;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv, handler, &op);
    if (SUCCEEDED(hr)) {
        if (WaitForSingleObject(handler->event(), 5000) != WAIT_OBJECT_0) hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    handler->Release();
    if (FAILED(hr)) return hr;

    stage_ = L"activate result";
    HRESULT activateHr = E_FAIL;
    ComPtr<IUnknown> unk;
    hr = op->GetActivateResult(&activateHr, &unk);
    if (FAILED(hr)) return hr;
    if (FAILED(activateHr)) return activateHr;
    ComPtr<IAudioClient> client;
    hr = unk.As(&client);
    if (FAILED(hr)) return hr;

    stage_ = L"initialize";
    // Variant 0 mirrors Microsoft's ApplicationLoopback sample (20 ms buffer, automatic format conversion);
    // variant 1 is the plain request. Some Windows 10 builds only accept one of them.
    if (variant == 0)
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                200000, 0, &fmt, nullptr);
    else
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                0, 0, &fmt, nullptr);
    if (FAILED(hr)) return hr;
    out = client;
    return S_OK;
}

HRESULT ProcessLoopbackCapture::initialise() {
    struct Candidate { WORD tag, bits; DWORD rate; };
    // Preferred: 32-bit float @48k. Fall back to 16-bit PCM like Microsoft's reference sample.
    const Candidate candidates[] = {
        {WAVE_FORMAT_IEEE_FLOAT, 32, 48000}, {WAVE_FORMAT_PCM, 16, 48000},
        {WAVE_FORMAT_IEEE_FLOAT, 32, 44100}, {WAVE_FORMAT_PCM, 16, 44100}};
    HRESULT last = E_FAIL;
    for (const auto& c : candidates) {
        WAVEFORMATEX f{};
        f.wFormatTag = c.tag;
        f.nChannels = 2;
        f.nSamplesPerSec = c.rate;
        f.wBitsPerSample = c.bits;
        f.nBlockAlign = static_cast<WORD>(f.nChannels * f.wBitsPerSample / 8);
        f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
        f.cbSize = 0;
        for (int variant = 0; variant < 2 && FAILED(last); ++variant) {
            ComPtr<IAudioClient> cl;
            last = activateWith(f, variant, cl);
            if (SUCCEEDED(last)) {
                client_ = cl;
                rate_ = c.rate; channels_ = 2; bits_ = c.bits; isFloat_ = (c.tag == WAVE_FORMAT_IEEE_FLOAT);
            }
        }
        if (SUCCEEDED(last)) break;
    }
    if (FAILED(last)) return last;
    stage_ = L"set event handle";
    HRESULT hr = client_->SetEventHandle(sampleEvent_);
    if (FAILED(hr)) return hr;
    stage_ = L"get capture service";
    hr = client_->GetService(IID_PPV_ARGS(&capture_));
    if (FAILED(hr)) return hr;
    stage_ = L"start";
    return client_->Start();
}

void ProcessLoopbackCapture::threadMain() {
    ComApartment com(COINIT_MULTITHREADED);
    initResult_ = com.ok() ? initialise() : com.result();
    if (SUCCEEDED(initResult_)) running_ = true;
    SetEvent(readyEvent_);
    if (FAILED(initResult_)) { capture_.Reset(); client_.Reset(); return; }

    // Same scheduling class as the renderer: without it a busy CPU (a game!) delays capture packets and the sound
    // cuts in and out.
    DWORD mmTask = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &mmTask);
    if (!mmcss) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    std::vector<float> scratch(4096 * 2);
    HANDLE waits[3] = {stopEvent_, sampleEvent_, nullptr};
    DWORD waitCount = 2;
    if (processHandle_) waits[waitCount++] = processHandle_;
    bool failed = false;
    CaptureFailure kind = CaptureFailure::CaptureError;
    HRESULT failHr = S_OK;

    while (!failed) {
        DWORD w = WaitForMultipleObjects(waitCount, waits, FALSE, 2000);
        if (w == WAIT_OBJECT_0) break;                       // stop requested
        if (waitCount == 3 && w == WAIT_OBJECT_0 + 2) { failed = true; kind = CaptureFailure::ProcessExited; break; }
        if (w == WAIT_OBJECT_0 + 1 || w == WAIT_TIMEOUT) {
            UINT32 packet = 0;
            HRESULT hr = capture_->GetNextPacketSize(&packet);
            while (SUCCEEDED(hr) && packet > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                hr = capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) break;
                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) discontinuities_.fetch_add(1);
                const size_t need = size_t(frames) * channels_;
                if (scratch.size() < need) scratch.resize(need);   // growth only, never in steady state
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    std::fill(scratch.begin(), scratch.begin() + need, 0.0f);
                } else if (!convertToFloat(data, frames, channels_, bits_, isFloat_, scratch.data())) {
                    capture_->ReleaseBuffer(frames);
                    hr = AUDCLNT_E_UNSUPPORTED_FORMAT;           // Audio Stream Format Mismatch
                    break;
                }
                source_->pushInterleaved(scratch.data(), frames, channels_, rate_);
                capture_->ReleaseBuffer(frames);
                hr = capture_->GetNextPacketSize(&packet);
            }
            if (FAILED(hr)) { failed = true; kind = CaptureFailure::CaptureError; failHr = hr; }
        } else {
            failed = true; failHr = HRESULT_FROM_WIN32(GetLastError());
        }
    }

    client_->Stop();
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    capture_.Reset();
    client_.Reset();
    running_ = false;
    if (failed && !stopping_.load() && onFailure_) onFailure_(pid_, kind, failHr);
}

} // namespace sf
