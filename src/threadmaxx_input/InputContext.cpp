#include "threadmaxx_input/context.hpp"

#include <algorithm>
#include <cassert>
#include <variant>

#include "threadmaxx_input/config.hpp"
#include "threadmaxx_input/events.hpp"

namespace threadmaxx::input {

namespace {

inline std::uint8_t mouseBit(MouseButton b) noexcept {
    return static_cast<std::uint8_t>(1u << static_cast<std::uint8_t>(b));
}

inline std::uint16_t padBit(GamepadButton b) noexcept {
    return static_cast<std::uint16_t>(1u << static_cast<std::uint8_t>(b));
}

inline std::uint8_t modifierBitForKey(Key k) noexcept {
    switch (k) {
        case Key::LShift:
        case Key::RShift: return Modifiers::Shift;
        case Key::LCtrl:
        case Key::RCtrl: return Modifiers::Ctrl;
        case Key::LAlt:
        case Key::RAlt: return Modifiers::Alt;
        case Key::LSuper:
        case Key::RSuper: return Modifiers::Super;
        default: return Modifiers::None;
    }
}

inline std::size_t gamepadSlotForDevice(DeviceId device) noexcept {
    if (device < kGamepadDeviceIdBase) return kMaxGamepads;
    const std::size_t slot = static_cast<std::size_t>(device - kGamepadDeviceIdBase);
    return slot < kMaxGamepads ? slot : kMaxGamepads;
}

// True when `b`'s source is currently active in `s`. Modifier mask is an
// exact match — Ctrl+S does NOT fire on Shift+Ctrl+S.
bool evaluateBindingHeld(const Binding& b, const InputState& s) noexcept {
    if (b.modifiers != s.modifiers) return false;
    switch (b.source) {
        case Binding::Source::Key:
            return s.keys.test(static_cast<Key>(b.code));
        case Binding::Source::MouseButton: {
            const auto bit = static_cast<std::uint8_t>(1u << b.code);
            return (s.mouse.buttons & bit) != 0;
        }
        case Binding::Source::GamepadButton: {
            const auto slot = gamepadSlotForDevice(b.device);
            if (slot >= kMaxGamepads) return false;
            const auto bit = static_cast<std::uint16_t>(1u << b.code);
            return (s.gamepads[slot].buttons & bit) != 0;
        }
        case Binding::Source::GamepadAxisPos: {
            const auto slot = gamepadSlotForDevice(b.device);
            if (slot >= kMaxGamepads) return false;
            const auto axisIdx = static_cast<std::size_t>(b.code);
            if (axisIdx >= s.gamepads[slot].axes.size()) return false;
            return s.gamepads[slot].axes[axisIdx] >= b.threshold;
        }
        case Binding::Source::GamepadAxisNeg: {
            const auto slot = gamepadSlotForDevice(b.device);
            if (slot >= kMaxGamepads) return false;
            const auto axisIdx = static_cast<std::size_t>(b.code);
            if (axisIdx >= s.gamepads[slot].axes.size()) return false;
            return s.gamepads[slot].axes[axisIdx] <= -b.threshold;
        }
    }
    return false;
}

float evaluateBindingValue(const Binding& b, const InputState& s) noexcept {
    if (!evaluateBindingHeld(b, s)) return 0.0f;
    switch (b.source) {
        case Binding::Source::GamepadAxisPos: {
            const auto slot = gamepadSlotForDevice(b.device);
            const auto axisIdx = static_cast<std::size_t>(b.code);
            return std::clamp(s.gamepads[slot].axes[axisIdx], 0.0f, 1.0f);
        }
        case Binding::Source::GamepadAxisNeg: {
            const auto slot = gamepadSlotForDevice(b.device);
            const auto axisIdx = static_cast<std::size_t>(b.code);
            return std::clamp(-s.gamepads[slot].axes[axisIdx], 0.0f, 1.0f);
        }
        default:
            return 1.0f;
    }
}

}  // namespace

InputContext::InputContext() {
    eventBuffer_.reserve(kInitialEventBuffer);
}

InputContext::~InputContext() = default;

void InputContext::setBackend(IInputBackend* backend) noexcept {
    backend_ = backend;
}

void InputContext::reserveEvents(std::size_t n) {
    if (eventBuffer_.capacity() < n) {
        eventBuffer_.reserve(n);
    }
}

void InputContext::beginFrame(float deltaTimeSeconds) {
    assert(!frameOpen_ && "InputContext::beginFrame called twice without endFrame");

    frameOpen_ = true;
    lastDeltaTime_ = deltaTimeSeconds;
    simulationTime_ += static_cast<double>(deltaTimeSeconds);

    previousState_ = state_;

    // Reset per-frame deltas + edge bitsets + char queue. Persistent level
    // state (keys held, gamepad axes, modifier mask) carries forward.
    state_.mouse.dx = 0.0f;
    state_.mouse.dy = 0.0f;
    state_.mouse.wheelDx = 0.0f;
    state_.mouse.wheelDy = 0.0f;
    state_.mouse.buttonsPressed = 0;
    state_.mouse.buttonsReleased = 0;
    state_.keysPressed.clear();
    state_.keysReleased.clear();
    state_.charCount = 0;
    for (auto& pad : state_.gamepads) {
        pad.buttonsPressed = 0;
        pad.buttonsReleased = 0;
    }

    drainBackendEvents();
    deriveEdges(previousState_);
}

void InputContext::endFrame() {
    assert(frameOpen_ && "InputContext::endFrame called without matching beginFrame");
    frameOpen_ = false;
    ++frameIndex_;
}

void InputContext::drainBackendEvents() {
    lastFrameEventCount_ = 0;
    if (backend_ == nullptr) return;

    eventBuffer_.clear();

    // Drain in fixed-size batches so kInitialEventBuffer survives steady
    // state with no growth.
    for (;;) {
        const std::size_t prior = eventBuffer_.size();
        if (eventBuffer_.capacity() < prior + kEventDrainBatch) {
            eventBuffer_.reserve(prior + kEventDrainBatch);
        }
        eventBuffer_.resize(prior + kEventDrainBatch);
        const std::size_t got = backend_->poll(eventBuffer_.data() + prior, kEventDrainBatch);
        eventBuffer_.resize(prior + got);
        if (got < kEventDrainBatch) break;
    }

    lastFrameEventCount_ = eventBuffer_.size();
    for (const auto& e : eventBuffer_) {
        applyEvent(e);
    }
}

void InputContext::applyEvent(const InputEvent& e) {
    std::visit(
        [&](auto const& ev) {
            using T = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<T, KeyEvent>) {
                state_.keys.set(ev.key, ev.down);
                const std::uint8_t modBit = modifierBitForKey(ev.key);
                if (modBit != Modifiers::None) {
                    if (ev.down) state_.modifiers = static_cast<std::uint8_t>(state_.modifiers | modBit);
                    else state_.modifiers = static_cast<std::uint8_t>(state_.modifiers & ~modBit);
                }
            } else if constexpr (std::is_same_v<T, CharEvent>) {
                if (state_.charCount < kMaxCharsPerFrame) {
                    state_.chars[state_.charCount++] = ev.codepoint;
                }
            } else if constexpr (std::is_same_v<T, MouseMoveEvent>) {
                state_.mouse.x = ev.x;
                state_.mouse.y = ev.y;
                state_.mouse.dx += ev.dx;
                state_.mouse.dy += ev.dy;
            } else if constexpr (std::is_same_v<T, MouseButtonEvent>) {
                const std::uint8_t bit = mouseBit(ev.button);
                if (ev.down) state_.mouse.buttons = static_cast<std::uint8_t>(state_.mouse.buttons | bit);
                else state_.mouse.buttons = static_cast<std::uint8_t>(state_.mouse.buttons & ~bit);
                state_.mouse.x = ev.x;
                state_.mouse.y = ev.y;
            } else if constexpr (std::is_same_v<T, MouseWheelEvent>) {
                state_.mouse.wheelDx += ev.dx;
                state_.mouse.wheelDy += ev.dy;
            } else if constexpr (std::is_same_v<T, GamepadButtonEvent>) {
                const std::size_t slot = gamepadSlotForDevice(ev.device);
                if (slot < kMaxGamepads) {
                    auto& pad = state_.gamepads[slot];
                    const std::uint16_t bit = padBit(ev.button);
                    if (ev.down) pad.buttons = static_cast<std::uint16_t>(pad.buttons | bit);
                    else pad.buttons = static_cast<std::uint16_t>(pad.buttons & ~bit);
                }
            } else if constexpr (std::is_same_v<T, GamepadAxisEvent>) {
                const std::size_t slot = gamepadSlotForDevice(ev.device);
                if (slot < kMaxGamepads) {
                    auto& pad = state_.gamepads[slot];
                    const auto axisIdx = static_cast<std::size_t>(ev.axis);
                    if (axisIdx < pad.axes.size()) pad.axes[axisIdx] = ev.value;
                }
            } else if constexpr (std::is_same_v<T, DeviceConnectEvent>) {
                if (ev.gamepad) {
                    const std::size_t slot = gamepadSlotForDevice(ev.device);
                    if (slot < kMaxGamepads) {
                        state_.gamepads[slot].connected = true;
                    }
                }
            } else if constexpr (std::is_same_v<T, DeviceDisconnectEvent>) {
                const std::size_t slot = gamepadSlotForDevice(ev.device);
                if (slot < kMaxGamepads) {
                    auto& pad = state_.gamepads[slot];
                    pad = GamepadState{};
                }
            }
        },
        e);
}

void InputContext::deriveEdges(const InputState& previous) {
    state_.keysPressed = detail::KeyBitset::transitionsToHigh(previous.keys, state_.keys);
    state_.keysReleased = detail::KeyBitset::transitionsToLow(previous.keys, state_.keys);

    state_.mouse.buttonsPressed =
        static_cast<std::uint8_t>(state_.mouse.buttons & ~previous.mouse.buttons);
    state_.mouse.buttonsReleased =
        static_cast<std::uint8_t>(previous.mouse.buttons & ~state_.mouse.buttons);

    for (std::size_t i = 0; i < kMaxGamepads; ++i) {
        const auto& prev = previous.gamepads[i];
        auto& cur = state_.gamepads[i];
        cur.buttonsPressed = static_cast<std::uint16_t>(cur.buttons & ~prev.buttons);
        cur.buttonsReleased = static_cast<std::uint16_t>(prev.buttons & ~cur.buttons);
    }
}

bool InputContext::isHeld(MouseButton b) const noexcept {
    return (state_.mouse.buttons & mouseBit(b)) != 0;
}

bool InputContext::wasPressed(MouseButton b) const noexcept {
    return (state_.mouse.buttonsPressed & mouseBit(b)) != 0;
}

bool InputContext::wasReleased(MouseButton b) const noexcept {
    return (state_.mouse.buttonsReleased & mouseBit(b)) != 0;
}

bool InputContext::isHeld(DeviceId pad, GamepadButton b) const noexcept {
    const std::size_t slot = gamepadSlotForDevice(pad);
    if (slot >= kMaxGamepads) return false;
    return (state_.gamepads[slot].buttons & padBit(b)) != 0;
}

float InputContext::axis(DeviceId pad, GamepadAxis a) const noexcept {
    const std::size_t slot = gamepadSlotForDevice(pad);
    if (slot >= kMaxGamepads) return 0.0f;
    const auto idx = static_cast<std::size_t>(a);
    const auto& padState = state_.gamepads[slot];
    if (idx >= padState.axes.size()) return 0.0f;
    return padState.axes[idx];
}

void InputContext::setBindings(const BindingSet& bindings) {
    bindings_ = bindings;
}

ActionTrigger InputContext::action(ActionId id) const noexcept {
    const auto bs = bindings_.bindingsFor(id);
    if (bs.empty()) return ActionTrigger{};

    bool prevAny = false;
    bool curAny = false;
    float bestValue = 0.0f;
    for (const Binding& b : bs) {
        if (evaluateBindingHeld(b, previousState_)) prevAny = true;
        if (evaluateBindingHeld(b, state_)) curAny = true;
        const float v = evaluateBindingValue(b, state_);
        if (v > bestValue) bestValue = v;
    }
    ActionTrigger t{};
    t.held = curAny;
    t.pressed = curAny && !prevAny;
    t.released = !curAny && prevAny;
    t.value = bestValue;
    return t;
}

}  // namespace threadmaxx::input
