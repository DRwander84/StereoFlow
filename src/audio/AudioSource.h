#pragma once
#include "audio/AudioResampler.h"
#include "routing/ChannelMapping.h"
#include "utils/RingBuffer.h"
#include "audio/Suppression.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

namespace sf {

// One captured stream (one process). Producer = capture thread, consumer = render thread.
// Stored internally as mono float at the OUTPUT sample rate.
class AudioSource {
public:
    // Jitter buffer between the capture thread (10 ms packets, scheduler jitter) and the render clock.
    static constexpr float kJitterMinMs = 25.0f;
    static constexpr float kJitterMaxMs = 120.0f;
    static constexpr float kJitterStepMs = 15.0f;     // growth per real underrun
    static constexpr float kJitterTrimMs = 90.0f;     // backlog above target that is trimmed (clock drift)

    AudioSource(uint32_t id, uint32_t outRate, size_t ringFrames);

    uint32_t id() const noexcept { return id_; }

    // ---- producer side (capture thread) ----
    void pushInterleaved(const float* data, size_t frames, uint32_t channels, uint32_t inRate);

    // ---- consumer side (render thread) ----
    size_t pullMono(float* out, size_t frames) noexcept;

    // ---- control (any thread) ----
    void setChannel(Channel c) noexcept { channel_.store(c, std::memory_order_relaxed); }
    Channel channel() const noexcept { return channel_.load(std::memory_order_relaxed); }
    void setGain(float g) noexcept { gain_.store(g, std::memory_order_relaxed); }
    float gain() const noexcept { return gain_.load(std::memory_order_relaxed); }
    void setMuted(bool m) noexcept { muted_.store(m, std::memory_order_relaxed); }
    bool muted() const noexcept { return muted_.load(std::memory_order_relaxed); }
    void setOutputRate(uint32_t r) noexcept { outRate_.store(r, std::memory_order_relaxed); }
    uint32_t outputRate() const noexcept { return outRate_.load(std::memory_order_relaxed); }
    void requestFlush() noexcept { flush_.store(true, std::memory_order_release); }

    // Compensation for a hushed original (see Suppression.h). The mixer trips it back to 1 the
    // instant a block is louder than a hushed capture can be, so an un-hushed session never blasts.
    void setBoost(float b) noexcept { boost_.store(b, std::memory_order_relaxed); }
    float boost() const noexcept { return boost_.load(std::memory_order_relaxed); }
    // Loudest block a correctly hushed capture can reach. Depends on how many sessions of the app are hushed
    // (a browser with several tabs sums them), so the engine adjusts it; the mixer only reads it.
    void setCeiling(float c) noexcept { ceiling_.store(c, std::memory_order_relaxed); }
    float ceiling() const noexcept { return ceiling_.load(std::memory_order_relaxed); }
    void tripBoost() noexcept { boost_.store(1.0f, std::memory_order_relaxed); tripped_.store(true, std::memory_order_release); }
    bool consumeTrip() noexcept { return tripped_.exchange(false, std::memory_order_acq_rel); }
    // meter / stats
    float peak() const noexcept { return peak_.load(std::memory_order_relaxed); }
    void reportBlockPeak(float p) noexcept;
    uint64_t underruns() const noexcept { return underruns_.load(std::memory_order_relaxed); }
    uint64_t overflows() const noexcept { return overflows_.load(std::memory_order_relaxed); }
    // Current adaptive jitter-buffer depth (ms), render-thread owned but read-only for diagnostics.
    float jitterMs() const noexcept { return jitterMs_.load(std::memory_order_relaxed); }

    // render-thread-owned smoothed gain (used by the mixer)
    // One smoothed gain per bus: moving a source to the other ear cross-fades (old bus down, new bus up) instead
    // of jumping, so neither ear clicks or drops.
    float currentGainL = 0.0f, currentGainR = 0.0f;
    bool gainInitialised = false;

private:
    uint32_t id_;
    std::atomic<uint32_t> outRate_;
    std::atomic<Channel> channel_{Channel::Left};
    std::atomic<float> gain_{1.0f};
    std::atomic<bool> muted_{false};
    std::atomic<bool> flush_{false};
    std::atomic<float> peak_{0.0f};
    std::atomic<float> boost_{1.0f};
    std::atomic<float> ceiling_{kHushedCeiling};
    std::atomic<int64_t> lastPushNs_{0};      // steady-clock time of the last captured packet (0 = never)
    std::atomic<float> jitterMs_{kJitterMinMs};
    std::atomic<bool> tripped_{false};
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> overflows_{0};

    SpscFloatRing ring_;
    // producer-owned
    StreamResampler resampler_;
    uint32_t resIn_ = 0, resOut_ = 0;
    std::vector<float> mono_;
    std::vector<float> resampled_;
    // consumer-owned
    bool primed_ = false;
    float targetMs_ = kJitterMinMs;           // adaptive jitter buffer: grows after a real underrun, decays when clean
    uint64_t cleanFrames_ = 0;
};

} // namespace sf
