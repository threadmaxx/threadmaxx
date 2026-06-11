#include "threadmaxx_input/backends/GlfwBackend.hpp"

#include <algorithm>
#include <utility>

namespace threadmaxx::input {

namespace {

// GLFW key code → input::Key. GLFW codes are ASCII for letters / digits
// and known ranges for the rest. We don't include <GLFW/glfw3.h> — the
// numeric constants below are stable per GLFW's documented values.
//
// Coverage is conservative: anything not mapped falls through to
// Key::Unknown. Hosts that need a fuller table can extend.
Key keyFromGlfw(int g) noexcept {
    if (g >= 'A' && g <= 'Z') {
        return static_cast<Key>(static_cast<std::uint16_t>(Key::A) + (g - 'A'));
    }
    if (g >= '0' && g <= '9') {
        return static_cast<Key>(static_cast<std::uint16_t>(Key::Num0) + (g - '0'));
    }
    switch (g) {
        case 32:  return Key::Space;
        case 39:  return Key::Quote;
        case 44:  return Key::Comma;
        case 45:  return Key::Minus;
        case 46:  return Key::Period;
        case 47:  return Key::Slash;
        case 59:  return Key::Semicolon;
        case 61:  return Key::Equal;
        case 91:  return Key::LeftBracket;
        case 92:  return Key::Backslash;
        case 93:  return Key::RightBracket;
        case 96:  return Key::Grave;
        case 256: return Key::Escape;
        case 257: return Key::Enter;
        case 258: return Key::Tab;
        case 259: return Key::Backspace;
        case 260: return Key::Insert;
        case 261: return Key::Delete;
        case 262: return Key::Right;
        case 263: return Key::Left;
        case 264: return Key::Down;
        case 265: return Key::Up;
        case 266: return Key::PageUp;
        case 267: return Key::PageDown;
        case 268: return Key::Home;
        case 269: return Key::End;
        case 290: return Key::F1;  case 291: return Key::F2;
        case 292: return Key::F3;  case 293: return Key::F4;
        case 294: return Key::F5;  case 295: return Key::F6;
        case 296: return Key::F7;  case 297: return Key::F8;
        case 298: return Key::F9;  case 299: return Key::F10;
        case 300: return Key::F11; case 301: return Key::F12;
        case 320: return Key::Numpad0; case 321: return Key::Numpad1;
        case 322: return Key::Numpad2; case 323: return Key::Numpad3;
        case 324: return Key::Numpad4; case 325: return Key::Numpad5;
        case 326: return Key::Numpad6; case 327: return Key::Numpad7;
        case 328: return Key::Numpad8; case 329: return Key::Numpad9;
        case 330: return Key::NumpadDecimal;
        case 331: return Key::NumpadDivide;
        case 332: return Key::NumpadMultiply;
        case 333: return Key::NumpadSubtract;
        case 334: return Key::NumpadAdd;
        case 335: return Key::NumpadEnter;
        case 340: return Key::LShift; case 344: return Key::RShift;
        case 341: return Key::LCtrl;  case 345: return Key::RCtrl;
        case 342: return Key::LAlt;   case 346: return Key::RAlt;
        case 343: return Key::LSuper; case 347: return Key::RSuper;
        default: return Key::Unknown;
    }
}

std::uint8_t modifiersFromGlfw(int glfwMods) noexcept {
    // GLFW modifier bits: SHIFT=1, CONTROL=2, ALT=4, SUPER=8. Match our
    // own Modifiers:: layout exactly.
    return static_cast<std::uint8_t>(glfwMods & 0x0F);
}

MouseButton mouseButtonFromGlfw(int g) noexcept {
    switch (g) {
        case 0: return MouseButton::Left;
        case 1: return MouseButton::Right;
        case 2: return MouseButton::Middle;
        case 3: return MouseButton::X1;
        case 4: return MouseButton::X2;
        default: return MouseButton::Left;
    }
}

GamepadButton gamepadButtonFromGlfw(int g) noexcept {
    // GLFW gamepad button order matches Xbox layout; clip to known range.
    if (g < 0 || g >= static_cast<int>(GamepadButton::Count)) return GamepadButton::A;
    return static_cast<GamepadButton>(g);
}

GamepadAxis gamepadAxisFromGlfw(int g) noexcept {
    if (g < 0 || g >= static_cast<int>(GamepadAxis::Count)) return GamepadAxis::LStickX;
    return static_cast<GamepadAxis>(g);
}

}  // namespace

void GlfwBackend::pushGlfwKey(int glfwKey, int /*scancode*/, int glfwAction, int glfwMods) {
    // 0 = release, 1 = press, 2 = repeat. Skip repeats — the engine drives
    // autorepeat behavior from `isHeld`.
    if (glfwAction == 2) return;
    const Key k = keyFromGlfw(glfwKey);
    if (k == Key::Unknown) return;
    queue_.push_back(KeyEvent{k, modifiersFromGlfw(glfwMods), glfwAction == 1});
}

void GlfwBackend::pushGlfwChar(std::uint32_t codepoint) {
    queue_.push_back(CharEvent{codepoint});
}

void GlfwBackend::pushGlfwCursorPos(double xpos, double ypos) {
    float dx = 0.0f, dy = 0.0f;
    if (cursorPosInitialized_) {
        dx = static_cast<float>(xpos - prevCursorX_);
        dy = static_cast<float>(ypos - prevCursorY_);
    }
    cursorPosInitialized_ = true;
    prevCursorX_ = xpos;
    prevCursorY_ = ypos;
    queue_.push_back(MouseMoveEvent{static_cast<float>(xpos), static_cast<float>(ypos), dx, dy});
}

void GlfwBackend::pushGlfwMouseButton(int glfwButton, int glfwAction, int /*glfwMods*/) {
    if (glfwAction == 2) return;
    queue_.push_back(MouseButtonEvent{mouseButtonFromGlfw(glfwButton), glfwAction == 1,
                                      static_cast<float>(prevCursorX_),
                                      static_cast<float>(prevCursorY_)});
}

void GlfwBackend::pushGlfwScroll(double xoffset, double yoffset) {
    queue_.push_back(MouseWheelEvent{static_cast<float>(xoffset), static_cast<float>(yoffset)});
}

void GlfwBackend::pushGlfwGamepadButton(DeviceId pad, int glfwButton, int glfwAction) {
    if (glfwAction == 2) return;
    queue_.push_back(GamepadButtonEvent{pad, gamepadButtonFromGlfw(glfwButton), glfwAction == 1});
}

void GlfwBackend::pushGlfwGamepadAxis(DeviceId pad, int glfwAxis, float value) {
    queue_.push_back(GamepadAxisEvent{pad, gamepadAxisFromGlfw(glfwAxis), value});
}

void GlfwBackend::pushDeviceConnect(DeviceId pad, bool gamepad) {
    queue_.push_back(DeviceConnectEvent{pad, gamepad});
    if (gamepad &&
        std::find(connectedGamepads_.begin(), connectedGamepads_.end(), pad) ==
            connectedGamepads_.end()) {
        connectedGamepads_.push_back(pad);
    }
}

void GlfwBackend::pushDeviceDisconnect(DeviceId pad) {
    queue_.push_back(DeviceDisconnectEvent{pad});
    auto it = std::find(connectedGamepads_.begin(), connectedGamepads_.end(), pad);
    if (it != connectedGamepads_.end()) connectedGamepads_.erase(it);
}

std::size_t GlfwBackend::poll(InputEvent* out, std::size_t cap) {
    const std::size_t pending = queue_.size() - head_;
    const std::size_t n = std::min(cap, pending);
    for (std::size_t i = 0; i < n; ++i) out[i] = queue_[head_ + i];
    head_ += n;
    if (head_ == queue_.size()) {
        queue_.clear();
        head_ = 0;
    }
    return n;
}

void GlfwBackend::setCursorMode(CursorMode mode) {
    if (cursorMode_ != mode) {
        cursorMode_ = mode;
        ++cursorChangeCount_;
    }
}

void GlfwBackend::setConnectedGamepads(std::vector<DeviceId> ids) {
    connectedGamepads_ = std::move(ids);
}

std::span<const DeviceId> GlfwBackend::connectedGamepads() const {
    return connectedGamepads_;
}

void GlfwBackend::reserve(std::size_t n) {
    if (queue_.capacity() < n) queue_.reserve(n);
}

void GlfwBackend::clear() noexcept {
    queue_.clear();
    head_ = 0;
}

}  // namespace threadmaxx::input
