#include "audio/AudioMixer.h"
#include "audio/Suppression.h"
#include <algorithm>
#include <cmath>

namespace sf {

AudioMixer::AudioMixer() {
    setSources({});
    setSampleRate(48000);
}

void AudioMixer::setSampleRate(uint32_t rate) noexcept {
    if (rate == 0) rate = 48000;
    releaseCoef_ = 1.0f - std::exp(-1.0f / (0.1f * static_cast<float>(rate))); // ~100 ms release
    rampFrames_ = static_cast<size_t>(rate / 200);                                // ~5 ms gain ramp
}

void AudioMixer::setSources(SourceList list) {
    auto fresh = std::make_unique<const SourceList>(std::move(list));
    current_.store(fresh.get(), std::memory_order_release);
    if (owned_) retired_.push_back({std::move(owned_), std::chrono::steady_clock::now()});
    owned_ = std::move(fresh);
    collectRetired();
}

void AudioMixer::collectRetired() {
    const auto limit = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    for (auto it = retired_.begin(); it != retired_.end();)
        it = (it->at < limit) ? retired_.erase(it) : it + 1;
}

void AudioMixer::mix(float* out, size_t frames) noexcept {
    size_t done = 0;
    while (done < frames) {
        const size_t n = std::min(frames - done, kMaxBlock);
        mixBlock(out + done * 2, n);
        done += n;
    }
}

void AudioMixer::mixBlock(float* out, size_t n) noexcept {
    float* L = l_.data();
    float* R = r_.data();
    float* T = tmp_.data();
    std::fill(L, L + n, 0.0f);
    std::fill(R, R + n, 0.0f);

    const SourceList* list = current_.load(std::memory_order_acquire);
    for (const auto& sp : *list) {
        AudioSource& s = *sp;
        const size_t got = s.pullMono(T, n);
        if (got == 0) { s.reportBlockPeak(0.0f); continue; }

        float pk = 0.0f;
        for (size_t i = 0; i < got; ++i) pk = std::max(pk, std::fabs(T[i]));
        s.reportBlockPeak(pk);

        // A hushed original gives a capture that is tiny by construction. A louder block means the
        // app (or a new session of it) is NOT hushed: play it at normal level and drop the boost.
        float eff = 1.0f;
        const float boost = s.boost();
        if (boost > 1.0f) {
            if (pk > s.ceiling()) s.tripBoost(); else eff = boost;
        }
        const float base = s.muted() ? 0.0f : s.gain() * eff;
        const bool left = (s.channel() == Channel::Left);
        const float tL = left ? base : 0.0f, tR = left ? 0.0f : base;
        if (!s.gainInitialised) { s.currentGainL = tL; s.currentGainR = tR; s.gainInitialised = true; }
        // Ramp over at most ~5 ms: a channel move or a boost change is smooth but never a slow fade of a whole block.
        const size_t ramp = std::min<size_t>(got, std::max<size_t>(32, rampFrames_));
        const float invRamp = 1.0f / static_cast<float>(ramp);
        // A bus whose gain is and stays exactly 0 is never touched: the opposite ear stays bit-exact silent.
        const float gains[2][2] = {{s.currentGainL, tL}, {s.currentGainR, tR}};
        float* const buses[2] = {L, R};
        for (int c = 0; c < 2; ++c) {
            const float g0 = gains[c][0], g1 = gains[c][1];
            if (g0 == 0.0f && g1 == 0.0f) continue;
            float* dst = buses[c];
            const float step = (g1 - g0) * invRamp;
            size_t i = 0;
            for (; i < ramp; ++i) dst[i] += T[i] * (g0 + step * static_cast<float>(i + 1));
            for (; i < got; ++i) dst[i] += T[i] * g1;
        }
        s.currentGainL = tL;
        s.currentGainR = tR;
    }

    // per-channel peak limiter (instant attack, smooth release) + hard safety clamp
    float* bus[2] = {L, R};
    bool hit = false;
    for (int c = 0; c < 2; ++c) {
        float g = limGain_[c];
        float pk = 0.0f;
        float* b = bus[c];
        for (size_t i = 0; i < n; ++i) {
            const float x = b[i];
            const float a = std::fabs(x);
            if (a > threshold_) {
                const float need = threshold_ / a;
                if (need < g) g = need;
                hit = true;
            } else {
                g += (1.0f - g) * releaseCoef_;
            }
            float y = x * g;
            y = std::min(1.0f, std::max(-1.0f, y));
            b[i] = y;
            pk = std::max(pk, std::fabs(y));
        }
        limGain_[c] = g;
        outPeak_[c].store(pk, std::memory_order_relaxed);
    }
    limiterHit_.store(hit, std::memory_order_relaxed);

    for (size_t i = 0; i < n; ++i) {
        out[2 * i] = L[i];
        out[2 * i + 1] = R[i];
    }
}

} // namespace sf
