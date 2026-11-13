#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) // intentional cache-line alignment padding
#endif

namespace sf {

// Lock-free single-producer / single-consumer ring of floats.
// Counters are monotonic; capacity is a power of two. No allocation after construction.
class SpscFloatRing {
public:
    explicit SpscFloatRing(size_t minCapacity) {
        size_t c = 16;
        while (c < minCapacity) c <<= 1;
        buf_.assign(c, 0.0f);
        mask_ = c - 1;
    }

    size_t capacity() const noexcept { return buf_.size(); }

    size_t available() const noexcept {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

    // Producer thread only.
    size_t write(const float* src, size_t n) noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t t = tail_.load(std::memory_order_acquire);
        const size_t freeN = buf_.size() - (h - t);
        if (n > freeN) n = freeN;
        if (n == 0) return 0;
        const size_t idx = h & mask_;
        const size_t first = std::min(n, buf_.size() - idx);
        std::memcpy(&buf_[idx], src, first * sizeof(float));
        if (n > first) std::memcpy(&buf_[0], src + first, (n - first) * sizeof(float));
        head_.store(h + n, std::memory_order_release);
        return n;
    }

    // Consumer thread only.
    size_t read(float* dst, size_t n) noexcept {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_relaxed);
        const size_t avail = h - t;
        if (n > avail) n = avail;
        if (n == 0) return 0;
        const size_t idx = t & mask_;
        const size_t first = std::min(n, buf_.size() - idx);
        std::memcpy(dst, &buf_[idx], first * sizeof(float));
        if (n > first) std::memcpy(dst + first, &buf_[0], (n - first) * sizeof(float));
        tail_.store(t + n, std::memory_order_release);
        return n;
    }

    // Consumer thread only.
    size_t skip(size_t n) noexcept {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_relaxed);
        n = std::min(n, h - t);
        tail_.store(t + n, std::memory_order_release);
        return n;
    }

private:
    std::vector<float> buf_;
    size_t mask_ = 0;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

} // namespace sf

#ifdef _MSC_VER
#pragma warning(pop)
#endif
