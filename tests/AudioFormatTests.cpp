#include "TestUtil.h"
#include "audio/AudioFormat.h"
#include "audio/AudioResampler.h"
#include "audio/AudioSource.h"
#include "utils/RingBuffer.h"
#include <cmath>
#include <thread>
#include <vector>
using namespace sf;

static int zeroCrossings(const std::vector<float>& v) {
    int z = 0; for (size_t i = 1; i < v.size(); ++i) if ((v[i-1] < 0) != (v[i] < 0)) ++z; return z;
}

int main() {
    { // PCM16 -> float
        int16_t s[4] = {0, 16384, -32768, 32767}; float o[4];
        CHECK(convertToFloat(s, 2, 2, 16, false, o));
        CHECK(o[1] == 0.5f && o[2] == -1.0f && o[3] < 1.0f);
    }
    { // PCM24 -> float
        uint8_t s[3] = {0x00, 0x00, 0x40}; float o[1]; // 0x400000 = +0.5
        CHECK(convertToFloat(s, 1, 1, 24, false, o)); CHECK(std::fabs(o[0] - 0.5f) < 1e-6f);
        uint8_t n[3] = {0x00, 0x00, 0x80}; convertToFloat(n, 1, 1, 24, false, o); CHECK(o[0] == -1.0f);
    }
    { float f[2] = {0.25f, -0.5f}, o[2]; CHECK(convertToFloat(f, 1, 2, 32, true, o)); CHECK(o[1] == -0.5f); }
    { float o[1]; char x[8]; CHECK(!convertToFloat(x, 1, 1, 8, false, o)); }
    { // downmix
        float st[4] = {1, 0, 0.5f, 0.5f}, m[2]; downmixToMono(st, 2, 2, m);
        CHECK(m[0] == 0.5f && m[1] == 0.5f);
    }
    { // passthrough resampler exact
        StreamResampler r; r.configure(48000, 48000); float in[4] = {1,2,3,4}, out[4];
        CHECK(r.process(in, 4, out, 4) == 4 && out[3] == 4.0f);
    }
    for (auto rates : {std::pair<uint32_t,uint32_t>{44100,48000}, {48000,44100}, {48000,96000}}) {
        StreamResampler r; r.configure(rates.first, rates.second);
        const double f = 1000.0; const size_t N = rates.first; // 1 second
        std::vector<float> in(N), out; std::vector<float> blk(20000);
        for (size_t i = 0; i < N; ++i) in[i] = static_cast<float>(0.5 * std::sin(2 * 3.14159265358979323846 * f * double(i) / rates.first));
        for (size_t p = 0; p < N; p += 480) {
            size_t n = std::min<size_t>(480, N - p);
            size_t k = r.process(in.data() + p, n, blk.data(), blk.size());
            out.insert(out.end(), blk.begin(), blk.begin() + k);
        }
        CHECK(std::llabs((long long)out.size() - (long long)rates.second) < 8);
        CHECK(std::abs(zeroCrossings(out) - 2000) <= 3);          // pitch preserved
        float mx = 0; for (size_t i = 100; i < out.size(); ++i) mx = std::fmax(mx, std::fabs(out[i]));
        CHECK(std::fabs(mx - 0.5f) < 0.01f);                       // amplitude preserved
    }
    { // ring buffer: threaded integrity
        SpscFloatRing ring(1024); const size_t total = 2'000'000; bool ok = true;
        std::thread prod([&]{ float v = 0; size_t sent = 0; float b[100];
            while (sent < total) { size_t n = std::min<size_t>(100, total - sent);
                for (size_t i = 0; i < n; ++i) b[i] = v + i;
                size_t w = ring.write(b, n); v += w; sent += w; if (w < n) { /* partial: re-generate from v */ }
                // keep sequence consistent for partial writes
            } });
        size_t got = 0; float expect = 0; float b[64];
        while (got < total) { size_t r = ring.read(b, 64); for (size_t i = 0; i < r; ++i) { if (b[i] != expect) ok = false; expect += 1; } got += r; }
        prod.join(); CHECK(ok);
    }
    { // source: overflow counted, flush works, underrun detection
        AudioSource s(1, 48000, 1024);
        std::vector<float> d(8000 * 2, 0.1f); s.pushInterleaved(d.data(), 8000, 2, 48000);
        CHECK(s.overflows() > 0);
        s.requestFlush(); float o[256]; CHECK(s.pullMono(o, 256) == 0);
    }
    { // mono 44.1k source into 48k output yields ~correct frame count
        AudioSource s(1, 48000, 1 << 16); std::vector<float> d(44100, 0.3f);
        s.pushInterleaved(d.data(), d.size(), 1, 44100);
        std::vector<float> o(60000); size_t g = s.pullMono(o.data(), o.size());
        CHECK(std::llabs((long long)g - 48000) < 8);
    }
    TEST_MAIN_END();
}
