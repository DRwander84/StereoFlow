#include "routing/ChannelMapping.h"
#include <cstring>

namespace sf {

const char* channelName(Channel c) noexcept { return c == Channel::Left ? "Left" : "Right"; }

bool parseChannel(const char* text, Channel& out) noexcept {
    if (!text) return false;
    if (std::strcmp(text, "Left") == 0) { out = Channel::Left; return true; }
    if (std::strcmp(text, "Right") == 0) { out = Channel::Right; return true; }
    return false;
}

} // namespace sf
