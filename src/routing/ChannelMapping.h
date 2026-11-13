#pragma once
#include <cstdint>

namespace sf {

enum class Channel : uint8_t { Left = 0, Right = 1 };

// Gains applied to a mono source for each physical output channel.
// Left source  -> (1, 0): Right output receives exactly 0.
// Right source -> (0, 1): Left output receives exactly 0.
struct ChannelGains {
    float left;
    float right;
};

constexpr ChannelGains gainsFor(Channel c) noexcept {
    return c == Channel::Left ? ChannelGains{1.0f, 0.0f} : ChannelGains{0.0f, 1.0f};
}

constexpr Channel opposite(Channel c) noexcept {
    return c == Channel::Left ? Channel::Right : Channel::Left;
}

// At most this many apps can be routed to ONE ear. Every routed app costs a capture thread, a ring, a resampler and
// a hush/boost state; beyond 5 per ear the mix is also unusable.
inline constexpr int kMaxAppsPerEar = 5;
// `onEar` = apps already on the target ear, not counting the app being placed. An app that is already on that ear
// is never "new" there, so re-assigning it is always allowed.
constexpr bool earHasRoom(int onEar, bool alreadyOnThisEar) noexcept {
    return alreadyOnThisEar || onEar < kMaxAppsPerEar;
}

const char* channelName(Channel c) noexcept;
bool parseChannel(const char* text, Channel& out) noexcept;

} // namespace sf
