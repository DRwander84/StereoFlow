#pragma once
#include "audio/AudioSource.h"
#include <array>
#include <chrono>
#include <atomic>
#include <memory>
#include <vector>

namespace sf {

// Real-time safe stereo mixer. Left-assigned sources only ever write the left bus,
// right-assigned sources only the right bus. A per-channel peak limiter prevents clipping.
class AudioMixer {
public:
    static constexpr size_t kMaxBlock = 4096;
    using SourceList = std::vector<std::shared_ptr<AudioSource>>;

    AudioMixer();

    void setSampleRate(uint32_t rate) noexcept;
    void setSources(SourceList list); // control thread
    void collectRetired();            // control thread: free lists the render thread can no longer be using
    void mix(float* interleavedLR, size_t frames) noexcept; // render thread

    float outputPeak(Channel c) const noexcept {
        return outPeak_[c == Channel::Left ? 0 : 1].load(std::memory_order_relaxed);
    }
    bool limiterEngaged() const noexcept { return limiterHit_.load(std::memory_order_relaxed); }

private:
    void mixBlock(float* out, size_t n) noexcept;

    // The render thread reads one raw pointer (wait-free, never frees memory). Replaced lists are kept
    // alive ~2 s by the control thread, far longer than any mix block, then freed there.
    struct Retired { std::unique_ptr<const SourceList> list; std::chrono::steady_clock::time_point at; };
    std::atomic<const SourceList*> current_{nullptr};
    std::unique_ptr<const SourceList> owned_;
    std::vector<Retired> retired_;
    std::array<float, kMaxBlock> l_{}, r_{}, tmp_{};
    float limGain_[2] = {1.0f, 1.0f};
    float releaseCoef_ = 0.001f;
    size_t rampFrames_ = 240;
    float threshold_ = 0.98f;
    std::atomic<float> outPeak_[2] = {0.0f, 0.0f};
    std::atomic<bool> limiterHit_{false};
};

} // namespace sf
