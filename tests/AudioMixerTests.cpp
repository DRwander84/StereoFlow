#include "TestUtil.h"
#include "audio/AudioMixer.h"
#include "audio/Suppression.h"
#include <cmath>
#include <memory>
#include <vector>
using namespace sf;

static std::shared_ptr<AudioSource> makeSource(uint32_t id, Channel ch, float gain, float value, size_t frames) {
    auto s = std::make_shared<AudioSource>(id, 48000, 1 << 16);
    s->setChannel(ch); s->setGain(gain);
    std::vector<float> d(frames * 2, value);   // stereo DC signal
    s->pushInterleaved(d.data(), frames, 2, 48000);
    return s;
}

int main() {
    const size_t N = 2048;
    std::vector<float> out(N * 2);

    { // Left-only isolation: right bus must be EXACTLY zero
        AudioMixer m; auto s = makeSource(1, Channel::Left, 1.0f, 0.5f, N * 2);
        m.setSources({s});
        m.mix(out.data(), N);   // first call may prime
        m.mix(out.data(), N);
        bool rightZero = true, leftOk = true;
        for (size_t i = 0; i < N; ++i) { if (out[2*i+1] != 0.0f) rightZero = false; if (std::fabs(out[2*i] - 0.5f) > 1e-5f) leftOk = false; }
        CHECK(rightZero); CHECK(leftOk);
    }
    { // Right-only isolation
        AudioMixer m; auto s = makeSource(1, Channel::Right, 1.0f, 0.25f, N * 2);
        m.setSources({s});
        m.mix(out.data(), N);
        bool leftZero = true;
        for (size_t i = 0; i < N; ++i) if (out[2*i] != 0.0f) leftZero = false;
        CHECK(leftZero); CHECK(std::fabs(out[1] - 0.25f) < 1e-5f);
    }
    { // Discord right + game left simultaneously, no crosstalk
        AudioMixer m;
        auto a = makeSource(1, Channel::Left, 1.0f, 0.4f, N);
        auto b = makeSource(2, Channel::Right, 1.0f, -0.3f, N);
        m.setSources({a, b}); m.mix(out.data(), N);
        bool ok = true;
        for (size_t i = 0; i < N; ++i) if (std::fabs(out[2*i] - 0.4f) > 1e-5f || std::fabs(out[2*i+1] + 0.3f) > 1e-5f) ok = false;
        CHECK(ok);
    }
    { // Two sources on one channel are summed
        AudioMixer m;
        auto a = makeSource(1, Channel::Left, 1.0f, 0.2f, N);
        auto b = makeSource(2, Channel::Left, 1.0f, 0.3f, N);
        m.setSources({a, b}); m.mix(out.data(), N);
        CHECK(std::fabs(out[2 * 100] - 0.5f) < 1e-5f);
    }
    { // Gain
        AudioMixer m; auto s = makeSource(1, Channel::Left, 0.5f, 0.8f, N);
        m.setSources({s}); m.mix(out.data(), N);
        CHECK(std::fabs(out[2 * 500] - 0.4f) < 1e-5f);
    }
    { // Mute
        AudioMixer m; auto s = makeSource(1, Channel::Left, 1.0f, 0.8f, N); s->setMuted(true);
        m.setSources({s}); m.mix(out.data(), N);
        CHECK(std::fabs(out[2 * 500]) < 1e-6f);
    }
    { // Clipping prevention: 4 loud sources -> never above 1.0, limiter engaged
        AudioMixer m;
        std::vector<std::shared_ptr<AudioSource>> v;
        for (uint32_t i = 0; i < 4; ++i) v.push_back(makeSource(i, Channel::Left, 1.0f, 0.9f, N));
        m.setSources(v); m.mix(out.data(), N);
        float mx = 0; for (size_t i = 0; i < N; ++i) mx = std::fmax(mx, std::fabs(out[2*i]));
        CHECK(mx <= 1.0f); CHECK(mx > 0.5f); CHECK(m.limiterEngaged());
        CHECK(m.outputPeak(Channel::Right) == 0.0f);
    }
    { // Limiter is transparent below threshold and meter is real
        AudioMixer m; auto s = makeSource(1, Channel::Left, 1.0f, 0.7f, N);
        m.setSources({s}); m.mix(out.data(), N);
        CHECK(!m.limiterEngaged()); CHECK(std::fabs(s->peak() - 0.7f) < 1e-4f);
    }
    { // Underrun / empty source yields silence, no crash
        AudioMixer m; auto s = std::make_shared<AudioSource>(1, 48000, 4096);
        m.setSources({s}); m.mix(out.data(), N);
        bool z = true; for (float f : out) if (f != 0.0f) z = false; CHECK(z);
    }
    { // Block larger than kMaxBlock
        AudioMixer m; const size_t big = AudioMixer::kMaxBlock * 2 + 17;
        auto s = makeSource(1, Channel::Right, 1.0f, 0.1f, big);
        std::vector<float> o(big * 2); m.setSources({s}); m.mix(o.data(), big);
        CHECK(std::fabs(o[2 * (big - 1) + 1] - 0.1f) < 1e-5f);
    }
    { // hushed capture is boosted back to full level
        AudioMixer m; auto s = makeSource(1, Channel::Left, 1.0f, kHushVolume * 0.5f, N * 3);
        s->setBoost(kHushBoost);
        m.setSources({s});
        m.mix(out.data(), N); m.mix(out.data(), N);
        CHECK(std::fabs(out[2 * (N - 1)] - 0.5f) < 1e-3f);
        CHECK(!s->consumeTrip());
    }
    { // an un-hushed (loud) capture trips the boost and is NOT amplified
        AudioMixer m; auto s = makeSource(1, Channel::Left, 1.0f, 0.3f, N * 4);
        s->setBoost(kHushBoost);
        m.setSources({s});
        m.mix(out.data(), N);
        bool ok = true;
        for (size_t i = 0; i < N; ++i) if (std::fabs(out[2 * i]) > 0.31f) ok = false;
        CHECK(ok); CHECK(s->boost() == 1.0f); CHECK(s->consumeTrip());
    }
    { // several hushed sessions sum inside one capture: a raised ceiling must NOT trip the boost
        AudioMixer m; auto s = makeSource(1, Channel::Left, 1.0f, 0.0015f, N * 4);
        s->setBoost(kHushBoost); s->setCeiling(0.002f);
        m.setSources({s}); m.mix(out.data(), N);
        CHECK(!s->consumeTrip()); CHECK(s->boost() == kHushBoost);
    }
    { // jitter buffer: playback waits for a full buffer, then a trickling stream does not cut out repeatedly
        AudioSource s(1, 48000, 1 << 16);
        std::vector<float> d(480 * 2, 0.2f), o(480);
        s.pushInterleaved(d.data(), 480, 2, 48000);                 // only 10 ms: below the 25 ms start threshold
        CHECK(s.pullMono(o.data(), 480) == 0);
        for (int i = 0; i < 3; ++i) s.pushInterleaved(d.data(), 480, 2, 48000);   // 40 ms now
        CHECK(s.pullMono(o.data(), 480) == 480);
    }
    { // a real underrun (stream still arriving) deepens the buffer; a stream that ended does not
        AudioSource s(1, 48000, 1 << 16);
        std::vector<float> d(480 * 2, 0.2f), o(960);
        for (int i = 0; i < 4; ++i) s.pushInterleaved(d.data(), 480, 2, 48000);
        const float before = s.jitterMs();
        s.pullMono(o.data(), 960); s.pullMono(o.data(), 960); s.pullMono(o.data(), 960);   // drains, push was just now
        CHECK(s.underruns() > 0); CHECK(s.jitterMs() > before);
    }
    { // clock drift: a large backlog is trimmed back near the target instead of growing latency
        AudioSource s(1, 48000, 1 << 16);
        std::vector<float> d(48000 * 2, 0.2f), o(480);              // 1 s backlog
        s.pushInterleaved(d.data(), 48000, 2, 48000);
        s.pullMono(o.data(), 480);
        float tmp[48000]; const size_t rest = s.pullMono(tmp, 48000);
        CHECK(rest < 48000 * 150 / 1000);
    }
    { // moving a source to the other ear: the old bus fades to EXACT zero, the other source on the old bus is untouched
        AudioMixer m;
        auto a = makeSource(1, Channel::Left, 1.0f, 0.4f, N * 3);
        auto b = makeSource(2, Channel::Right, 1.0f, 0.3f, N * 3);
        m.setSources({a, b}); m.mix(out.data(), N);
        a->setChannel(Channel::Right);
        m.mix(out.data(), N);                       // transition block
        m.mix(out.data(), N);                       // settled
        bool leftZero = true, rightOk = true;
        for (size_t i = 0; i < N; ++i) {
            if (out[2 * i] != 0.0f) leftZero = false;
            if (std::fabs(out[2 * i + 1] - 0.7f) > 1e-4f) rightOk = false;
        }
        CHECK(leftZero); CHECK(rightOk);
    }
    { // the ramp is short and monotonic: no step, no drop of the destination bus
        AudioMixer m; auto a = makeSource(1, Channel::Left, 1.0f, 0.5f, N * 3);
        m.setSources({a}); m.mix(out.data(), N);
        a->setChannel(Channel::Right); m.mix(out.data(), N);
        bool mono = true; float prevL = 0.5f, prevR = 0.0f;
        for (size_t i = 0; i < N; ++i) {
            const float l = out[2 * i], r = out[2 * i + 1];
            if (l > prevL + 1e-6f || r < prevR - 1e-6f) mono = false;
            if (std::fabs((l + r) - 0.5f) > 1e-4f) mono = false;    // constant power-sum: never a gap
            prevL = l; prevR = r;
        }
        CHECK(mono);
    }
    { // hush depth: the hushed original is at least 60 dB down and boost is its exact inverse
        CHECK(kHushVolume <= 0.00002f);                        // -94 dB or deeper
        CHECK(isHushLevel(kHushVolume) && isHushLevel(0.00025f) && isHushLevel(0.004f));
        CHECK(!isHushLevel(0.0f) && !isHushLevel(0.5f) && !isHushLevel(0.001f));
        CHECK(std::fabs(kHushVolume * kHushBoost - 1.0f) < 1e-6f);
    }
    TEST_MAIN_END();
}
