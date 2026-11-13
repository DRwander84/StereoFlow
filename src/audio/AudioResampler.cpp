#include "audio/AudioResampler.h"
#include <algorithm>
#include <cstring>

namespace sf {

void StreamResampler::configure(uint32_t inRate, uint32_t outRate) {
    if (inRate == 0 || outRate == 0) inRate = outRate = 48000;
    passthrough_ = (inRate == outRate);
    step_ = static_cast<double>(inRate) / static_cast<double>(outRate);
    buf_.reserve(32768);
    reset();
}

void StreamResampler::reset() {
    buf_.clear();
    buf_.push_back(0.0f); // history sample so index i-1 always exists
    pos_ = 1.0;
}

size_t StreamResampler::process(const float* in, size_t n, float* out, size_t cap) {
    if (passthrough_) {
        const size_t m = std::min(n, cap);
        if (m) std::memcpy(out, in, m * sizeof(float));
        return m;
    }
    buf_.insert(buf_.end(), in, in + n);
    size_t produced = 0;
    while (produced < cap) {
        const size_t i = static_cast<size_t>(pos_);
        if (i + 2 >= buf_.size()) break;
        const float t = static_cast<float>(pos_ - static_cast<double>(i));
        const float y0 = buf_[i - 1], y1 = buf_[i], y2 = buf_[i + 1], y3 = buf_[i + 2];
        const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c = -0.5f * y0 + 0.5f * y2;
        out[produced++] = ((a * t + b) * t + c) * t + y1;
        pos_ += step_;
    }
    const size_t i = static_cast<size_t>(pos_);
    if (i > 1) {
        const size_t drop = i - 1;
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(drop));
        pos_ -= static_cast<double>(drop);
    }
    return produced;
}

} // namespace sf
