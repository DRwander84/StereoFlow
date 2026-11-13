#include <string>
#include "TestUtil.h"
#include "routing/ChannelMapping.h"
using namespace sf;
int main() {
    CHECK(gainsFor(Channel::Left).left == 1.0f && gainsFor(Channel::Left).right == 0.0f);
    CHECK(gainsFor(Channel::Right).left == 0.0f && gainsFor(Channel::Right).right == 1.0f);
    CHECK(opposite(Channel::Left) == Channel::Right);
    Channel c{};
    CHECK(parseChannel("Right", c) && c == Channel::Right);
    CHECK(!parseChannel("Center", c));
    CHECK(std::string(channelName(Channel::Left)) == "Left");
    TEST_MAIN_END();
}
