#pragma once
#include <cmath>
namespace sf {

// Windows applies an app's session volume/mute BEFORE the process-loopback tap, so muting an app
// also silences its captured copy. Instead of muting, the original is "hushed" to a tiny session
// volume and the captured copy is boosted by the exact inverse (float math, lossless).
//
// The hushed original still plays through BOTH ears, only very quietly, so the depth matters. Depths, deepest first:
//   -96 dB  (default) below the 16-bit floor of the output endpoint: nothing is left to hear in the other ear;
//   -72 dB, -48 dB   fallbacks, used only if Windows does not keep the deeper volume (checked by read-back).
// The 32-bit float capture keeps the round trip lossless: float32 precision is relative, so a 62500x boost loses nothing.
inline constexpr float kHushLevels[] = {0.000016f, 0.00025f, 0.004f};
inline constexpr int kHushLevelCount = 3;
constexpr float kHushVolume = kHushLevels[0];     // deepest level (-96 dB)
constexpr float kHushBoost = 62500.0f;            // 1 / kHushVolume
constexpr float kHushedCeiling = 5.0f * kHushVolume;   // a correctly hushed capture stays far below this
constexpr float kHushTolerance = 0.1f;            // RELATIVE: "volume is still the one we set"

// True when `v` is (within tolerance) one of the hush depths, i.e. a volume that StereoFlow put there.
inline bool isHushLevel(float v) noexcept {
    for (float l : kHushLevels) if (std::fabs(v - l) <= kHushTolerance * l) return true;
    return false;
}

} // namespace sf
