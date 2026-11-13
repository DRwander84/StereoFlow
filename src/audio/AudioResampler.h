#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sf {

// Streaming mono resampler using 4-point Catmull-Rom (cubic) interpolation.
// Cheap and allocation-free after the first few calls (buffer is pre-reserved).
class StreamResampler {
public:
    StreamResampler() { configure(48000, 48000); }

    void configure(uint32_t inRate, uint32_t outRate);
    void reset();

    // Writes at most `cap` samples. Returns the number written.
    size_t process(const float* in, size_t n, float* out, size_t cap);

    static size_t maxOutput(size_t inFrames, uint32_t inRate, uint32_t outRate) noexcept {
        return static_cast<size_t>((static_cast<unsigned long long>(inFrames) * outRate) / (inRate ? inRate : 1)) + 8;
    }

private:
    bool passthrough_ = true;
    double step_ = 1.0;
    double pos_ = 1.0;
    std::vector<float> buf_;
};

} // namespace sf
