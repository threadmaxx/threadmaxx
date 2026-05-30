// tou2d_toast_render_test — M6.8 toast / notification layer contract.
//
// Pins:
//   * UIToast POD: 32 bytes (slot+severity+durationTicks+message[28]).
//   * ToastRenderSystem::pushForTest with slot 0..3 lands on that
//     slot's stack only; broadcast (kToastSlotBroadcast = 0xFF) fans
//     into every slot.
//   * The per-slot stack is LIFO with `kMaxVisiblePerSlot = 4` cap.
//     The 5th push drops the oldest entry from the front (FIFO drain
//     on overflow so the newest survives — "max 4 visible" contract).
//   * ageOnceForTest decrements every remainingTicks; entries reach
//     zero are removed.
//   * durationTicks == 0 push is a no-op (otherwise a zero-life toast
//     would briefly flash for one tick which the contract disallows).
//   * Out-of-range slots return the empty span — never throw, never
//     UB on activeForSlot.

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/ToastRenderSystem.hpp"

#include <cstring>
#include <string>

int main() {
    using tou2d::UIToast;
    using tou2d::ToastRenderSystem;
    using tou2d::kToastSlotBroadcast;

    // ---- 1. POD layout pin ----------------------------------------------
    CHECK_EQ(sizeof(UIToast), std::size_t{32});
    CHECK_EQ(kToastSlotBroadcast, std::uint8_t{0xFFu});
    CHECK_EQ(ToastRenderSystem::kMaxVisiblePerSlot, std::size_t{4});
    CHECK_EQ(ToastRenderSystem::kSlotCount, std::size_t{4});

    // ---- 2. Single-slot push lands on that slot only --------------------
    {
        ToastRenderSystem sys(nullptr, nullptr);
        UIToast t{};
        t.slot          = 1;
        t.severity      = 1;
        t.durationTicks = 60;
        std::strncpy(t.message.data(), "hello", t.message.size() - 1);
        sys.pushForTest(t);
        CHECK_EQ(sys.activeForSlot(0).size(), std::size_t{0});
        CHECK_EQ(sys.activeForSlot(1).size(), std::size_t{1});
        CHECK_EQ(sys.activeForSlot(2).size(), std::size_t{0});
        CHECK_EQ(sys.activeForSlot(3).size(), std::size_t{0});
        const auto& slot1 = sys.activeForSlot(1);
        CHECK_EQ(slot1[0].toast.severity, std::uint8_t{1});
        CHECK_EQ(slot1[0].remainingTicks, std::uint16_t{60});
        CHECK_EQ(std::string{slot1[0].toast.message.data()},
                 std::string{"hello"});
    }

    // ---- 3. Broadcast fans to every slot --------------------------------
    {
        ToastRenderSystem sys(nullptr, nullptr);
        UIToast t{};
        t.slot          = kToastSlotBroadcast;
        t.durationTicks = 30;
        std::strncpy(t.message.data(), "all", t.message.size() - 1);
        sys.pushForTest(t);
        for (std::uint8_t s = 0; s < 4; ++s) {
            CHECK_EQ(sys.activeForSlot(s).size(), std::size_t{1});
            CHECK_EQ(sys.activeForSlot(s)[0].remainingTicks,
                     std::uint16_t{30});
        }
    }

    // ---- 4. LIFO order; oldest at front, newest at back -----------------
    {
        ToastRenderSystem sys(nullptr, nullptr);
        for (std::uint16_t i = 0; i < 3; ++i) {
            UIToast t{};
            t.slot          = 2;
            t.durationTicks = static_cast<std::uint16_t>(10 + i);
            sys.pushForTest(t);
        }
        const auto& s2 = sys.activeForSlot(2);
        CHECK_EQ(s2.size(), std::size_t{3});
        // Oldest at front (durationTicks 10), newest at back (12).
        CHECK_EQ(s2[0].remainingTicks, std::uint16_t{10});
        CHECK_EQ(s2[1].remainingTicks, std::uint16_t{11});
        CHECK_EQ(s2[2].remainingTicks, std::uint16_t{12});
    }

    // ---- 5. Visible cap — 5th push drops the oldest ---------------------
    {
        ToastRenderSystem sys(nullptr, nullptr);
        for (std::uint16_t i = 0; i < 5; ++i) {
            UIToast t{};
            t.slot          = 0;
            t.durationTicks = static_cast<std::uint16_t>(100 + i);
            sys.pushForTest(t);
        }
        const auto& s0 = sys.activeForSlot(0);
        CHECK_EQ(s0.size(), ToastRenderSystem::kMaxVisiblePerSlot);
        // Oldest dropped (originally durationTicks 100); front is now 101.
        CHECK_EQ(s0[0].remainingTicks, std::uint16_t{101});
        CHECK_EQ(s0[3].remainingTicks, std::uint16_t{104});
    }

    // ---- 6. ageOnceForTest decrements; reaching zero removes ------------
    {
        ToastRenderSystem sys(nullptr, nullptr);
        UIToast a{};
        a.slot          = 3;
        a.durationTicks = 2;
        sys.pushForTest(a);
        UIToast b{};
        b.slot          = 3;
        b.durationTicks = 1;
        sys.pushForTest(b);
        CHECK_EQ(sys.activeForSlot(3).size(), std::size_t{2});

        sys.ageOnceForTest();
        // `b` had 1 tick → removed; `a` had 2 → now 1.
        const auto& s3 = sys.activeForSlot(3);
        CHECK_EQ(s3.size(), std::size_t{1});
        CHECK_EQ(s3[0].remainingTicks, std::uint16_t{1});

        sys.ageOnceForTest();
        // `a` ages out.
        CHECK_EQ(sys.activeForSlot(3).size(), std::size_t{0});
    }

    // ---- 7. durationTicks == 0 is a no-op push --------------------------
    {
        ToastRenderSystem sys(nullptr, nullptr);
        UIToast t{};
        t.slot          = 0;
        t.durationTicks = 0;
        sys.pushForTest(t);
        CHECK_EQ(sys.activeForSlot(0).size(), std::size_t{0});
    }

    // ---- 8. Out-of-range slot push is silently dropped ------------------
    {
        ToastRenderSystem sys(nullptr, nullptr);
        UIToast t{};
        t.slot          = 5;  // > kSlotCount and != kToastSlotBroadcast
        t.durationTicks = 50;
        sys.pushForTest(t);
        for (std::uint8_t s = 0; s < 4; ++s) {
            CHECK_EQ(sys.activeForSlot(s).size(), std::size_t{0});
        }
        // Out-of-range query returns the empty span.
        CHECK_EQ(sys.activeForSlot(99).size(), std::size_t{0});
    }

    // ---- 9. Broadcast respects per-slot cap independently ---------------
    {
        ToastRenderSystem sys(nullptr, nullptr);
        // Prime slot 2 with 3 entries.
        for (std::uint16_t i = 0; i < 3; ++i) {
            UIToast t{};
            t.slot          = 2;
            t.durationTicks = static_cast<std::uint16_t>(200 + i);
            sys.pushForTest(t);
        }
        // Now broadcast 3 — slot 2 ends at 4 (cap), others at 3.
        for (std::uint16_t i = 0; i < 3; ++i) {
            UIToast t{};
            t.slot          = kToastSlotBroadcast;
            t.durationTicks = static_cast<std::uint16_t>(300 + i);
            sys.pushForTest(t);
        }
        CHECK_EQ(sys.activeForSlot(0).size(), std::size_t{3});
        CHECK_EQ(sys.activeForSlot(1).size(), std::size_t{3});
        CHECK_EQ(sys.activeForSlot(2).size(),
                 ToastRenderSystem::kMaxVisiblePerSlot);
        CHECK_EQ(sys.activeForSlot(3).size(), std::size_t{3});
        // Slot 2's newest is the last broadcast (302).
        const auto& s2 = sys.activeForSlot(2);
        CHECK_EQ(s2[s2.size() - 1].remainingTicks, std::uint16_t{302});
    }

    EXIT_WITH_RESULT();
}
