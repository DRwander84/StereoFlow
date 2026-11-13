#include "TestUtil.h"
#include "audio/AudioMixer.h"
#include <cmath>
#include <vector>
using namespace sf;
// Runtime reassignment: change channel / gain while mixing, no crosstalk afterwards; add/remove sources.
int main() {
    const size_t N = 480;
    AudioMixer m; std::vector<float> out(N * 2), d(N * 2 * 50, 0.5f);
    auto s = std::make_shared<AudioSource>(1, 48000, 1 << 16);
    s->setChannel(Channel::Left); s->pushInterleaved(d.data(), N * 50, 2, 48000);
    m.setSources({s});
    m.mix(out.data(), N);
    CHECK(out[1] == 0.0f && out[0] > 0.4f);
    s->setChannel(Channel::Right);
    m.mix(out.data(), N);
    CHECK(out[N * 2 - 2] == 0.0f && out[N * 2 - 1] > 0.4f);
    s->setGain(0.0f); m.mix(out.data(), N); m.mix(out.data(), N);
    CHECK(std::fabs(out[N * 2 - 1]) < 1e-6f);   // gain ramp reached zero
    m.setSources({});                            // remove source
    m.mix(out.data(), N);
    bool z = true; for (float f : out) if (f != 0) z = false; CHECK(z);
        // ear capacity: 5 apps max per ear; an app already on the ear never counts as new
    CHECK(kMaxAppsPerEar == 5);
    for (int n = 0; n < 5; ++n) CHECK(earHasRoom(n, false));
    CHECK(!earHasRoom(5, false));
    CHECK(earHasRoom(5, true));
    CHECK(!earHasRoom(6, false));
    TEST_MAIN_END();
}
