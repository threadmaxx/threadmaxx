#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/System.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace threadmaxx { class Engine; }

namespace tou2d {

class CameraSystem;

/// M6.8 — drains the typed `UIToast` channel and renders short-lived
/// per-slot notifications as severity-tinted strips.
///
/// The drain runs through a `Subscription` registered at construction:
/// the engine fires it on the sim thread at every tick-end drain (after
/// postStep, BEFORE the front/back render swap). The callback appends
/// directly to `stacks_` so toasts emitted on tick N are visible from
/// tick N's `buildRenderFrame` onwards.
///
/// `preStep` ages every active toast (decrement remainingTicks; drop on
/// zero). `update()` is empty — the system does no parallel work.
/// `buildRenderFrame` walks each slot's stack and emits one
/// severity-coloured `DebugLine` strip per active toast, stacked
/// downwards from the slot's viewport top-edge.
///
/// reads()  = none.
/// writes() = none.
///
/// The renderer doesn't yet draw `DebugText`, so the v1 visual signal
/// is the coloured strip + the per-slot stack height. The `message`
/// string is preserved in `stacks_` for tests and for a future text-
/// rendering pass without re-shipping the channel.
class ToastRenderSystem : public threadmaxx::ISystem {
public:
    ToastRenderSystem(threadmaxx::Engine* eng,
                      const CameraSystem* cam) noexcept;
    ~ToastRenderSystem() override;

    const char*              name()   const noexcept override { return "tou2d.toast"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

    void onRegister      (threadmaxx::World&)              override;
    void preStep         (threadmaxx::SystemContext& ctx)  override;
    void update          (threadmaxx::SystemContext& ctx)  override;
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

    /// Max visible toasts per slot. Oldest entries are dropped when a
    /// new toast pushes the stack past this cap.
    static constexpr std::size_t kMaxVisiblePerSlot = 4;

    /// Number of per-viewport slots; matches kMaxHumans.
    static constexpr std::size_t kSlotCount = 4;

    /// Test / inspection — one entry per visible toast on the slot's
    /// stack. Order: index 0 is the oldest (bottom of the strip),
    /// index size()-1 is the newest. Empty span for an empty slot.
    struct ActiveToast {
        UIToast       toast;
        std::uint16_t remainingTicks;
    };
    const std::vector<ActiveToast>& activeForSlot(std::uint8_t slot) const noexcept {
        return slot < kSlotCount ? stacks_[slot] : empty_;
    }

    /// Test hook — feed a UIToast straight into the stack pipeline,
    /// bypassing the engine event channel. Used by the unit test so it
    /// doesn't need to stand up a real engine drain.
    void pushForTest(const UIToast& t);

    /// Test hook — advance every per-slot stack by one tick of ageing.
    /// Mirrors the body of `preStep` without requiring a real engine
    /// SystemContext. Production code goes through `preStep`.
    void ageOnceForTest();

private:
    void pushOne(const UIToast& t);
    void ageOne(std::vector<ActiveToast>& stack);

    threadmaxx::Engine* engine_ = nullptr;
    const CameraSystem* camera_ = nullptr;

    /// Subscription to the engine's typed `UIToast` channel. The
    /// callback pushes directly into stacks_ on the sim thread during
    /// tick-end drain (single producer, single consumer — no lock).
    threadmaxx::Subscription sub_;

    /// Per-slot LIFO of active toasts (newest at the back, oldest at
    /// the front). Cap-enforced at kMaxVisiblePerSlot.
    std::array<std::vector<ActiveToast>, kSlotCount> stacks_{};

    /// Returned-by-ref empty slot for out-of-range queries — keeps
    /// `activeForSlot` non-throwing and return-by-ref.
    std::vector<ActiveToast> empty_{};
};

} // namespace tou2d
