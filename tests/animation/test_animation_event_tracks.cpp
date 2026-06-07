#include "Check.hpp"
#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/eval.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace threadmaxx::animation;

namespace {

ClipDesc makeFootstepClip(bool looping) {
    ClipDesc c;
    c.name = "walk";
    c.duration = 1.0f;
    c.looping = looping;
    c.jointCount = 1;
    c.keyframeTimes = {0.0f, 1.0f};
    c.keyframes.resize(2);
    c.events = {
        EventTrackEvent{0.25f, "left"},
        EventTrackEvent{0.75f, "right"},
    };
    return c;
}

std::size_t countNamed(const std::vector<EventTrackEvent>& v, const std::string& name) {
    return static_cast<std::size_t>(
        std::count_if(v.begin(), v.end(),
                      [&](const EventTrackEvent& e) { return e.name == name; }));
}

} // namespace

int main() {
    // 1. Non-looping: each event fires exactly once on its crossing.
    {
        ClipDesc clip = makeFootstepClip(false);
        Animator a;
        a.setClip(&clip);
        std::vector<EventTrackEvent> drained;

        a.advance(0.2f); // t=0.2, neither event crossed
        a.drainEvents(drained);
        CHECK_EQ(drained.size(), std::size_t{0});

        a.advance(0.1f); // t=0.3, "left" at 0.25 crossed
        a.drainEvents(drained);
        CHECK_EQ(drained.size(), std::size_t{1});
        CHECK_EQ(drained[0].name, std::string{"left"});

        drained.clear();
        a.advance(0.5f); // t=0.8, "right" at 0.75 crossed
        a.drainEvents(drained);
        CHECK_EQ(drained.size(), std::size_t{1});
        CHECK_EQ(drained[0].name, std::string{"right"});

        drained.clear();
        a.advance(1.0f); // clamps at t=1.0, no further events
        a.drainEvents(drained);
        CHECK_EQ(drained.size(), std::size_t{0});
    }

    // 2. Single big advance past both events fires both in time order.
    {
        ClipDesc clip = makeFootstepClip(false);
        Animator a;
        a.setClip(&clip);
        std::vector<EventTrackEvent> drained;

        a.advance(0.9f); // t=0.9, crosses both
        a.drainEvents(drained);
        CHECK_EQ(drained.size(), std::size_t{2});
        CHECK_EQ(drained[0].name, std::string{"left"});
        CHECK_EQ(drained[1].name, std::string{"right"});
    }

    // 3. Looping: each loop fires every event exactly once.
    {
        ClipDesc clip = makeFootstepClip(true);
        Animator a;
        a.setClip(&clip);
        std::vector<EventTrackEvent> drained;

        // Loop 1: advance 1.0 → wraps from t=0 → t=0.0. The wrap fires
        // events in (0, 1.0] (the previous-loop tail) so both events
        // fire once.
        a.advance(1.0f);
        a.drainEvents(drained);
        CHECK_EQ(countNamed(drained, "left"), std::size_t{1});
        CHECK_EQ(countNamed(drained, "right"), std::size_t{1});

        drained.clear();
        // Loop 2: advance 1.0 again → same firing pattern.
        a.advance(1.0f);
        a.drainEvents(drained);
        CHECK_EQ(countNamed(drained, "left"), std::size_t{1});
        CHECK_EQ(countNamed(drained, "right"), std::size_t{1});

        drained.clear();
        // Looped wrap crossing only one event: from t=0 → t=0.6 hits
        // "left" but not "right".
        a.advance(0.6f);
        a.drainEvents(drained);
        CHECK_EQ(countNamed(drained, "left"), std::size_t{1});
        CHECK_EQ(countNamed(drained, "right"), std::size_t{0});

        drained.clear();
        // From t=0.6 → t=1.4 (wraps to 0.4): "right" fires from the
        // tail of the old loop, "left" fires from the head of the
        // new loop (event at 0.25 ≤ 0.4).
        a.advance(0.8f);
        a.drainEvents(drained);
        CHECK_EQ(countNamed(drained, "left"), std::size_t{1});
        CHECK_EQ(countNamed(drained, "right"), std::size_t{1});
    }

    // 4. Rewinding via setTime does not fire events.
    {
        ClipDesc clip = makeFootstepClip(false);
        Animator a;
        a.setClip(&clip);
        std::vector<EventTrackEvent> drained;

        a.advance(0.8f); // both events crossed
        a.drainEvents(drained);
        CHECK_EQ(drained.size(), std::size_t{2});

        drained.clear();
        a.setTime(0.1f); // rewind — no events
        a.drainEvents(drained);
        CHECK_EQ(drained.size(), std::size_t{0});

        // After rewind, forward motion fires events that haven't
        // been crossed since the rewind.
        a.advance(0.5f); // t=0.6, "left" at 0.25 fires
        a.drainEvents(drained);
        CHECK_EQ(countNamed(drained, "left"), std::size_t{1});
        CHECK_EQ(countNamed(drained, "right"), std::size_t{0});
    }

    // 5. Rewinding via advance(-dt) does not fire events.
    {
        ClipDesc clip = makeFootstepClip(false);
        Animator a;
        a.setClip(&clip);
        std::vector<EventTrackEvent> drained;

        a.advance(0.8f);
        a.drainEvents(drained);
        CHECK_EQ(drained.size(), std::size_t{2});

        drained.clear();
        a.advance(-0.5f); // backward, t = 0.3
        a.drainEvents(drained);
        CHECK_EQ(drained.size(), std::size_t{0});
    }

    // 6. Event at exact t=0 fires once per loop in looping mode.
    {
        ClipDesc clip = makeFootstepClip(true);
        clip.events.push_back(EventTrackEvent{0.0f, "loop_start"});

        Animator a;
        a.setClip(&clip);
        std::vector<EventTrackEvent> drained;

        a.advance(1.0f); // first wrap fires loop_start once
        a.drainEvents(drained);
        CHECK_EQ(countNamed(drained, "loop_start"), std::size_t{1});

        drained.clear();
        a.advance(1.0f); // second wrap fires loop_start once
        a.drainEvents(drained);
        CHECK_EQ(countNamed(drained, "loop_start"), std::size_t{1});
    }

    // 7. setClip resets pending events.
    {
        ClipDesc clipA = makeFootstepClip(false);
        ClipDesc clipB = makeFootstepClip(false);
        clipB.name = "clipB";

        Animator a;
        a.setClip(&clipA);
        a.advance(0.9f);
        CHECK(a.hasPendingEvents());

        a.setClip(&clipB);
        CHECK(!a.hasPendingEvents());

        std::vector<EventTrackEvent> drained;
        a.drainEvents(drained);
        CHECK_EQ(drained.size(), std::size_t{0});
    }

    EXIT_WITH_RESULT();
}
