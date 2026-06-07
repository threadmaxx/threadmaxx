#include "Check.hpp"
#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/registry.hpp"

using namespace threadmaxx::animation;

int main() {
    AnimationRegistry reg;

    ClipDesc desc;
    desc.name = "walk_cycle";
    desc.duration = 1.25f;
    desc.looping = true;
    desc.events = {
        EventTrackEvent{0.0f,  "footstep_left"},
        EventTrackEvent{0.3f,  "footstep_right"},
        EventTrackEvent{0.6f,  "footstep_left"},
        EventTrackEvent{0.9f,  "footstep_right"},
    };

    ClipId id = reg.addClip(desc);
    CHECK(reg.isValid(id));

    const ClipDesc* got = reg.getClip(id);
    CHECK(got != nullptr);
    CHECK_EQ(got->name, std::string{"walk_cycle"});
    CHECK(got->duration == 1.25f);
    CHECK_EQ(got->looping, true);
    CHECK_EQ(got->events.size(), std::size_t{4});
    CHECK_EQ(got->events[2].name, std::string{"footstep_left"});
    CHECK(got->events[3].time == 0.9f);

    // Bogus id is rejected.
    CHECK(!reg.isValid(ClipId{9999}));
    CHECK(reg.getClip(ClipId{9999}) == nullptr);

    EXIT_WITH_RESULT();
}
