#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "threadmaxx_input/action.hpp"
#include "threadmaxx_input/types.hpp"

namespace threadmaxx::input {

// A single binding maps a raw input source to an action. Multi-source
// actions register multiple bindings under the same ActionId; queries OR
// across them.
struct Binding {
    enum class Source : std::uint8_t {
        Key = 0,
        MouseButton = 1,
        GamepadButton = 2,
        GamepadAxisPos = 3,
        GamepadAxisNeg = 4,
    };

    Source source{Source::Key};
    std::uint8_t modifiers{};  // exact match — Ctrl+S does NOT fire on Shift+Ctrl+S
    std::uint16_t code{};      // Key / MouseButton / GamepadButton / GamepadAxis (cast)
    DeviceId device{};         // gamepad slot for pad sources; ignored for KB+mouse
    float threshold{0.5f};     // axis sources only

    static Binding key(Key k, std::uint8_t mods = Modifiers::None) noexcept {
        Binding b{};
        b.source = Source::Key;
        b.code = static_cast<std::uint16_t>(k);
        b.modifiers = mods;
        return b;
    }
    static Binding mouseButton(MouseButton mb, std::uint8_t mods = Modifiers::None) noexcept {
        Binding b{};
        b.source = Source::MouseButton;
        b.code = static_cast<std::uint16_t>(mb);
        b.modifiers = mods;
        return b;
    }
    static Binding gamepadButton(GamepadButton gb, DeviceId pad = kGamepad0DeviceId) noexcept {
        Binding b{};
        b.source = Source::GamepadButton;
        b.code = static_cast<std::uint16_t>(gb);
        b.device = pad;
        return b;
    }
    static Binding gamepadAxisPositive(GamepadAxis ax, float threshold = 0.5f,
                                       DeviceId pad = kGamepad0DeviceId) noexcept {
        Binding b{};
        b.source = Source::GamepadAxisPos;
        b.code = static_cast<std::uint16_t>(ax);
        b.threshold = threshold;
        b.device = pad;
        return b;
    }
    static Binding gamepadAxisNegative(GamepadAxis ax, float threshold = 0.5f,
                                       DeviceId pad = kGamepad0DeviceId) noexcept {
        Binding b{};
        b.source = Source::GamepadAxisNeg;
        b.code = static_cast<std::uint16_t>(ax);
        b.threshold = threshold;
        b.device = pad;
        return b;
    }
};

// Action → list-of-bindings registry. Owns its storage; bound-time
// mutation is OK, query-time reads are pure. Designed for setup-time
// configuration (loader reads from disk, calls bind() N times, hands the
// set to the context).
class BindingSet {
public:
    void bind(ActionId id, Binding b);
    void bind(std::string_view name, Binding b);
    void clear(ActionId id);
    void clearAll() noexcept;

    std::span<const Binding> bindingsFor(ActionId id) const noexcept;
    std::size_t actionCount() const noexcept { return entries_.size(); }
    bool contains(ActionId id) const noexcept;

    // Iteration in registration order. Returns spans into internal storage;
    // do not retain across mutating calls.
    struct EntryView {
        ActionId id;
        std::span<const Binding> bindings;
    };
    std::vector<EntryView> entries() const;

    // Binary wire format. Host-endian POD, gated by a magic + version
    // header. See DESIGN_NOTES § 5.6 for the contract.
    std::vector<std::byte> serialize() const;
    bool deserialize(std::span<const std::byte> bytes);

    static constexpr std::uint32_t kSerializeMagic = 0x4249'4D54u;  // 'TMIB' LE
    static constexpr std::uint32_t kSerializeVersion = 1u;

private:
    struct Entry {
        ActionId id{};
        std::vector<Binding> bindings;
    };
    std::vector<Entry> entries_;
    std::unordered_map<ActionId, std::size_t> index_;
};

}  // namespace threadmaxx::input
