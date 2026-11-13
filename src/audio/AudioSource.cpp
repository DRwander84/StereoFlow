#include "audio/AudioSource.h"
#include "audio/AudioFormat.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace sf {

AudioSource::AudioSource(uint32_t id, uint32_t outRate, size_t ringFrames)
    : id_(id), outRate_(outRate), ring_(ringFrames) {
    mono_.resize(8192);
    resampled_.resize(16384);
}

void AudioSource::pushInterleaved(const float* d, size_t frames, uint32_t ch, uint32_t inRate) {
    if (!frames || !ch) return;
    lastPushNs_.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now().time_since_epoch()).count(),
                      std::memory_order_relaxed);
    const uint32_t outRate = outRate_.load(std::memory_order_relaxed);
    if (inRate != resIn_ || outRate != resOut_) {
        resampler_.configure(inRate, outRate);
        resIn_ = inRate;
        resOut_ = outRate;
    }
    size_t pos = 0;
    while (pos < frames) {
        const size_t chunk = std::min(frames - pos, mono_.size());
        downmixToMono(d + pos * ch, chunk, ch, mono_.data());
        const size_t cap = StreamResampler::maxOutput(chunk, inRate, outRate);
        if (resampled_.size() < cap) resampled_.resize(cap);
        const size_t produced = resampler_.process(mono_.data(), chunk, resampled_.data(), resampled_.size());
        const size_t w = ring_.write(resampled_.data(), produced);
        if (w < produced) overflows_.fetch_add(produced - w, std::memory_order_relaxed);
        pos += chunk;
    }
}

size_t AudioSource::pullMono(float* out, size_t n) noexcept {
    if (flush_.exchange(false, std::memory_order_acq_rel)) {
        ring_.skip(ring_.available());
        primed_ = false;
    }
    const double rate = static_cast<double>(outRate_.load(std::memory_order_relaxed));
    const size_t target = static_cast<size_t>(rate * targetMs_ / 1000.0);
    if (!primed_) {
        // (Re)start only once a full jitter buffer is available, so one late packet cannot cut the sound again.
        if (ring_.available() < target) return 0;
        primed_ = true;
    }
    const size_t got = ring_.read(out, n);
    if (got < n) {
        const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count();
        const int64_t last = lastPushNs_.load(std::memory_order_relaxed);
        // A REAL underrun: the stream is still delivering (packet < 60 ms ago) but we ran dry.
        // (A stream that simply ended or paused must not make the buffer grow.)
        if (last != 0 && now - last < 60'000'000) {
            underruns_.fetch_add(1, std::memory_order_relaxed);
            targetMs_ = std::min(kJitterMaxMs, targetMs_ + kJitterStepMs);
            jitterMs_.store(targetMs_, std::memory_order_relaxed);
            cleanFrames_ = 0;
        }
        if (got == 0) primed_ = false;
    } else {
        cleanFrames_ += n;
        if (cleanFrames_ > static_cast<uint64_t>(rate * 8.0)) {          // 8 s without an underrun: shrink slowly
            cleanFrames_ = 0;
            if (targetMs_ > kJitterMinMs) {
                targetMs_ = std::max(kJitterMinMs, targetMs_ - 5.0f);
                jitterMs_.store(targetMs_, std::memory_order_relaxed);
            }
        }
        // Capture and render clocks drift apart; keep latency bounded instead of letting a backlog build up.
        const size_t rem = ring_.available();
        const size_t cap = target + static_cast<size_t>(rate * kJitterTrimMs / 1000.0);
        if (rem > cap) ring_.skip(rem - target);
    }
    return got;
}

void AudioSource::reportBlockPeak(float p) noexcept {
    const float cur = peak_.load(std::memory_order_relaxed) * 0.92f; // visual decay per block
    peak_.store(std::max(p, cur), std::memory_order_relaxed);
}

} // namespace sf
