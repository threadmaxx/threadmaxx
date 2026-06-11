#include "threadmaxx_input/ui_bridge.hpp"

#include <cmath>

namespace threadmaxx::input {

namespace {

inline std::uint16_t mapModifiers(std::uint8_t inMods) noexcept {
    // Bit positions match by design: Shift=0, Ctrl=1, Alt=2, Super=3.
    return static_cast<std::uint16_t>(inMods & 0x0Fu);
}

inline std::uint16_t mapNavKeys(const InputState& s) noexcept {
    std::uint16_t bits = 0;
    using K = Key;
    namespace U = threadmaxx::ui::NavKey;

    const bool shiftHeld = (s.modifiers & Modifiers::Shift) != 0;
    if (s.keysPressed.test(K::Tab))      bits |= shiftHeld ? U::ShiftTab : U::Tab;
    if (s.keysPressed.test(K::Enter))    bits |= U::Enter;
    if (s.keysPressed.test(K::Escape))   bits |= U::Escape;
    if (s.keysPressed.test(K::Left))     bits |= U::Left;
    if (s.keysPressed.test(K::Right))    bits |= U::Right;
    if (s.keysPressed.test(K::Up))       bits |= U::Up;
    if (s.keysPressed.test(K::Down))     bits |= U::Down;
    if (s.keysPressed.test(K::Backspace)) bits |= U::Backspace;
    if (s.keysPressed.test(K::Delete))   bits |= U::Delete;
    if (s.keysPressed.test(K::Home))     bits |= U::Home;
    if (s.keysPressed.test(K::End))      bits |= U::End;
    return bits;
}

}  // namespace

threadmaxx::ui::UIInput toUIInput(const InputContext& ctx) noexcept {
    const auto& s = ctx.state();
    threadmaxx::ui::UIInput out;

    out.mousePos = threadmaxx::ui::Vec2i{static_cast<std::int32_t>(s.mouse.x),
                                         static_cast<std::int32_t>(s.mouse.y)};
    out.mouseDelta = threadmaxx::ui::Vec2i{static_cast<std::int32_t>(s.mouse.dx),
                                            static_cast<std::int32_t>(s.mouse.dy)};
    out.scrollY = static_cast<std::int32_t>(std::lround(s.mouse.wheelDy));
    out.mouseButtons = s.mouse.buttons;
    out.mouseButtonsPressed = s.mouse.buttonsPressed;
    out.mouseButtonsReleased = s.mouse.buttonsReleased;
    out.modifiers = mapModifiers(s.modifiers);
    out.navKeysPressed = mapNavKeys(s);

    const std::uint8_t copyCount = static_cast<std::uint8_t>(
        std::min<std::size_t>(s.charCount, threadmaxx::ui::kMaxFrameChars));
    for (std::size_t i = 0; i < copyCount; ++i) {
        const std::uint32_t cp = s.chars[i];
        out.chars[i] = cp <= 0x7Fu ? static_cast<char>(cp) : '?';
    }
    out.charsCount = copyCount;
    out.deltaTimeSeconds = ctx.lastDeltaTime();
    return out;
}

}  // namespace threadmaxx::input
