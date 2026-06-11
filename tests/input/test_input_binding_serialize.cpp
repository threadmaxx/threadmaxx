/// @file test_input_binding_serialize.cpp
/// @brief BindingSet binary round-trip: serialize then deserialize yields
/// an identical action / binding set; bad magic + wrong version are
/// rejected cleanly.

#include "Check.hpp"

#include "threadmaxx_input/binding.hpp"

int main() {
    using namespace threadmaxx::input;

    BindingSet src;
    src.bind("Jump", Binding::key(Key::Space));
    src.bind("Jump", Binding::gamepadButton(GamepadButton::A));
    src.bind("Save", Binding::key(Key::S, Modifiers::Ctrl));
    src.bind("MoveX", Binding::gamepadAxisPositive(GamepadAxis::LStickX, 0.3f));
    src.bind("MoveX", Binding::gamepadAxisNegative(GamepadAxis::LStickX, 0.3f));
    src.bind("Shoot", Binding::mouseButton(MouseButton::Left));

    const auto bytes = src.serialize();
    CHECK(!bytes.empty());

    BindingSet dst;
    CHECK(dst.deserialize(bytes));
    CHECK_EQ(dst.actionCount(), src.actionCount());

    auto srcEntries = src.entries();
    auto dstEntries = dst.entries();
    CHECK_EQ(dstEntries.size(), srcEntries.size());
    for (std::size_t i = 0; i < srcEntries.size(); ++i) {
        CHECK_EQ(dstEntries[i].id, srcEntries[i].id);
        CHECK_EQ(dstEntries[i].bindings.size(), srcEntries[i].bindings.size());
        for (std::size_t j = 0; j < srcEntries[i].bindings.size(); ++j) {
            const auto& a = srcEntries[i].bindings[j];
            const auto& b = dstEntries[i].bindings[j];
            CHECK_EQ(static_cast<int>(a.source), static_cast<int>(b.source));
            CHECK_EQ(a.modifiers, b.modifiers);
            CHECK_EQ(a.code, b.code);
            CHECK_EQ(a.device, b.device);
            CHECK_EQ(a.threshold, b.threshold);
        }
    }

    // bindingsFor() should find every action by ID.
    CHECK_EQ(dst.bindingsFor(actionId("Jump")).size(), std::size_t{2});
    CHECK_EQ(dst.bindingsFor(actionId("MoveX")).size(), std::size_t{2});
    CHECK_EQ(dst.bindingsFor(actionId("NeverBound")).size(), std::size_t{0});

    // Bad magic — reject.
    auto corruptMagic = bytes;
    corruptMagic[0] = std::byte{0xFF};
    BindingSet bad;
    CHECK(!bad.deserialize(corruptMagic));

    // Wrong version — reject.
    auto wrongVersion = bytes;
    wrongVersion[4] = std::byte{99};
    BindingSet bad2;
    CHECK(!bad2.deserialize(wrongVersion));

    // Truncated — reject (no underflow).
    auto truncated = std::span<const std::byte>(bytes.data(), 6);
    BindingSet bad3;
    CHECK(!bad3.deserialize(truncated));

    // Empty buffer — reject.
    BindingSet bad4;
    CHECK(!bad4.deserialize({}));

    // clearAll / clear behaviour.
    dst.clear(actionId("Jump"));
    CHECK(!dst.contains(actionId("Jump")));
    CHECK(dst.contains(actionId("Save")));
    dst.clearAll();
    CHECK_EQ(dst.actionCount(), std::size_t{0});

    EXIT_WITH_RESULT();
}
