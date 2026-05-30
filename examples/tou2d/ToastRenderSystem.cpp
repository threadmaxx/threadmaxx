#include "ToastRenderSystem.hpp"

#include "CameraSystem.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/render/DebugGeometry.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

namespace tou2d {

namespace {

constexpr std::uint32_t kSeverityColorInfo     = 0xFFE0E0E0u; // pale grey
constexpr std::uint32_t kSeverityColorWarn     = 0xFFE0E040u; // amber
constexpr std::uint32_t kSeverityColorCritical = 0xFFFF4040u; // red

/// Visual layout — world-unit dimensions of a single toast strip.
/// Picked so a stack of four at the slot's top-edge spans roughly the
/// upper 12% of the viewport without overlapping the HUD score row.
constexpr float kToastStripHalfWidthWU  = 48.0f;
constexpr float kToastStripHeightWU     = 5.0f;
constexpr float kToastStripGapWU        = 2.0f;
/// Top-edge inset from the viewport's followCenter to the top of the
/// newest strip. Anchored against `orthoHalfH * viewportH` to scale
/// with the split-screen layout.
constexpr float kToastTopInsetFracOfHalfH = 0.85f;

std::uint32_t colorForSeverity(std::uint8_t severity) noexcept {
    switch (severity) {
        case 2: return kSeverityColorCritical;
        case 1: return kSeverityColorWarn;
        default: return kSeverityColorInfo;
    }
}

} // namespace

ToastRenderSystem::ToastRenderSystem(threadmaxx::Engine* eng,
                                     const CameraSystem* cam) noexcept
    : engine_(eng), camera_(cam) {}

ToastRenderSystem::~ToastRenderSystem() = default;

void ToastRenderSystem::onRegister(threadmaxx::World&) {
    if (!engine_) return;
    sub_ = engine_->events<UIToast>().subscribeScoped(
        [this](const UIToast& ev) { pushOne(ev); });
}

void ToastRenderSystem::pushForTest(const UIToast& t) {
    pushOne(t);
}

void ToastRenderSystem::ageOnceForTest() {
    for (auto& s : stacks_) ageOne(s);
}

void ToastRenderSystem::pushOne(const UIToast& t) {
    if (t.durationTicks == 0) return;
    const auto append = [&](std::vector<ActiveToast>& stack) {
        stack.push_back(ActiveToast{t, t.durationTicks});
        while (stack.size() > kMaxVisiblePerSlot) {
            stack.erase(stack.begin());
        }
    };
    if (t.slot == kToastSlotBroadcast) {
        for (auto& s : stacks_) append(s);
    } else if (t.slot < kSlotCount) {
        append(stacks_[t.slot]);
    }
}

void ToastRenderSystem::ageOne(std::vector<ActiveToast>& stack) {
    auto it = stack.begin();
    while (it != stack.end()) {
        if (it->remainingTicks <= 1) {
            it = stack.erase(it);
        } else {
            --it->remainingTicks;
            ++it;
        }
    }
}

void ToastRenderSystem::preStep(threadmaxx::SystemContext& ctx) {
    ctx.single([this](threadmaxx::Range, threadmaxx::CommandBuffer&) {
        for (auto& s : stacks_) ageOne(s);
    });
}

void ToastRenderSystem::update(threadmaxx::SystemContext&) {
    // No parallel work — drain runs through the engine subscription;
    // ageing runs in preStep.
}

void ToastRenderSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    if (!camera_) return;
    const float halfH = camera_->orthoHalfH();
    if (halfH <= 0.0f) return;

    const std::uint8_t humans = camera_->numHumans();
    for (std::uint8_t slot = 0; slot < kSlotCount; ++slot) {
        if (slot >= humans) continue;
        const auto& stack = stacks_[slot];
        if (stack.empty()) continue;
        const threadmaxx::Vec3 c = camera_->followCenter(slot);
        const float xMin = c.x - kToastStripHalfWidthWU;
        const float xMax = c.x + kToastStripHalfWidthWU;
        const float yTop = c.y + halfH * kToastTopInsetFracOfHalfH;
        // Newest at the top; stack.back() = newest → row 0.
        for (std::size_t i = 0; i < stack.size(); ++i) {
            const ActiveToast& a =
                stack[stack.size() - 1 - i];
            const std::uint32_t color = colorForSeverity(a.toast.severity);
            const float rowY = yTop -
                static_cast<float>(i) * (kToastStripHeightWU + kToastStripGapWU);
            threadmaxx::DebugLine top{};
            top.a         = {xMin, rowY, 0.0f};
            top.b         = {xMax, rowY, 0.0f};
            top.colorRGBA = color;
            b.addDebugLine(top);
            threadmaxx::DebugLine bot{};
            bot.a         = {xMin, rowY - kToastStripHeightWU, 0.0f};
            bot.b         = {xMax, rowY - kToastStripHeightWU, 0.0f};
            bot.colorRGBA = color;
            b.addDebugLine(bot);
        }
    }
}

} // namespace tou2d
