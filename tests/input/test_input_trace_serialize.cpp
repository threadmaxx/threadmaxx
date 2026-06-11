/// @file test_input_trace_serialize.cpp
/// @brief InputTrace round-trips through serialize + deserialize.
/// Magic / version mismatches and truncated buffers are rejected cleanly.

#include "Check.hpp"

#include <variant>

#include "threadmaxx_input/trace.hpp"

namespace {

bool eventsEqual(const threadmaxx::input::InputEvent& a,
                 const threadmaxx::input::InputEvent& b) {
    return a.index() == b.index();  // sufficient for the variety check below;
                                    // per-alternative checks compare fields directly.
}

}  // namespace

int main() {
    using namespace threadmaxx::input;

    InputTrace src;

    src.appendFrame(std::vector<InputEvent>{
        KeyEvent{Key::W, Modifiers::None, true},
        CharEvent{'w'},
        MouseMoveEvent{1.0f, 2.0f, 3.0f, 4.0f},
    });
    src.appendFrame(std::vector<InputEvent>{
        MouseButtonEvent{MouseButton::Right, true, 0.0f, 0.0f},
        MouseWheelEvent{0.5f, -1.0f},
    });
    src.appendFrame(std::vector<InputEvent>{
        GamepadButtonEvent{kGamepad0DeviceId, GamepadButton::A, true},
        GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickX, 0.75f},
        DeviceConnectEvent{kGamepad0DeviceId, true},
        DeviceDisconnectEvent{kGamepad0DeviceId},
    });

    const auto bytes = src.serialize();
    CHECK(!bytes.empty());

    InputTrace dst;
    CHECK(dst.deserialize(bytes));
    CHECK_EQ(dst.frameCount(), src.frameCount());

    for (std::uint64_t i = 0; i < src.frameCount(); ++i) {
        const auto a = src.frame(i);
        const auto b = dst.frame(i);
        CHECK_EQ(a.size(), b.size());
        for (std::size_t j = 0; j < a.size(); ++j) {
            CHECK(eventsEqual(a[j], b[j]));
        }
    }

    // Field-level: confirm a few alternatives round-trip their data.
    {
        const auto& mm = std::get<MouseMoveEvent>(dst.frame(0)[2]);
        CHECK_EQ(mm.x, 1.0f);
        CHECK_EQ(mm.dx, 3.0f);
    }
    {
        const auto& mw = std::get<MouseWheelEvent>(dst.frame(1)[1]);
        CHECK_EQ(mw.dy, -1.0f);
    }
    {
        const auto& ga = std::get<GamepadAxisEvent>(dst.frame(2)[1]);
        CHECK_EQ(ga.value, 0.75f);
        CHECK_EQ(static_cast<int>(ga.axis), static_cast<int>(GamepadAxis::LStickX));
    }

    // Bad magic → reject.
    auto corruptMagic = bytes;
    corruptMagic[0] = std::byte{0xFF};
    InputTrace bad;
    CHECK(!bad.deserialize(corruptMagic));

    // Wrong version → reject.
    auto wrongVersion = bytes;
    wrongVersion[4] = std::byte{99};
    InputTrace bad2;
    CHECK(!bad2.deserialize(wrongVersion));

    // Truncated → reject.
    InputTrace bad3;
    CHECK(!bad3.deserialize(std::span<const std::byte>(bytes.data(), 8)));

    // Empty → reject.
    InputTrace bad4;
    CHECK(!bad4.deserialize({}));

    EXIT_WITH_RESULT();
}
