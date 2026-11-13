#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sf {

// Converts interleaved PCM (16/24/32-bit int) or IEEE float (32/64) into float [-1,1].
// Returns false for unsupported layouts. No allocation.
inline bool convertToFloat(const void* src, size_t frames, uint32_t channels,
                           uint32_t bitsPerSample, bool isFloat, float* dst) noexcept {
    const size_t n = frames * channels;
    if (isFloat) {
        if (bitsPerSample == 32) { std::memcpy(dst, src, n * sizeof(float)); return true; }
        if (bitsPerSample == 64) {
            const double* s = static_cast<const double*>(src);
            for (size_t i = 0; i < n; ++i) dst[i] = static_cast<float>(s[i]);
            return true;
        }
        return false;
    }
    switch (bitsPerSample) {
    case 16: {
        const int16_t* s = static_cast<const int16_t*>(src);
        constexpr float k = 1.0f / 32768.0f;
        for (size_t i = 0; i < n; ++i) dst[i] = static_cast<float>(s[i]) * k;
        return true;
    }
    case 24: {
        const uint8_t* s = static_cast<const uint8_t*>(src);
        constexpr float k = 1.0f / 8388608.0f;
        for (size_t i = 0; i < n; ++i) {
            int32_t v = (int32_t(s[i * 3]) | (int32_t(s[i * 3 + 1]) << 8) | (int32_t(s[i * 3 + 2]) << 16));
            if (v & 0x800000) v |= ~0xFFFFFF;
            dst[i] = static_cast<float>(v) * k;
        }
        return true;
    }
    case 32: {
        const int32_t* s = static_cast<const int32_t*>(src);
        constexpr double k = 1.0 / 2147483648.0;
        for (size_t i = 0; i < n; ++i) dst[i] = static_cast<float>(static_cast<double>(s[i]) * k);
        return true;
    }
    default: return false;
    }
}

// Downmix interleaved float to mono: mono = (ch0 + ch1) / 2 for >= 2 channels.
inline void downmixToMono(const float* in, size_t frames, uint32_t channels, float* out) noexcept {
    if (channels == 1) { std::memcpy(out, in, frames * sizeof(float)); return; }
    for (size_t i = 0; i < frames; ++i) out[i] = 0.5f * (in[i * channels] + in[i * channels + 1]);
}

} // namespace sf
