#include "threadmaxx_input/trace.hpp"

#include <cstring>
#include <utility>
#include <variant>

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

namespace threadmaxx::input {

namespace {

// 1-byte tag prefix + the raw payload bytes. The wire format never
// transmits std::variant directly — we tag each event so deserialize knows
// which alternative to reconstruct.
enum class EventTag : std::uint8_t {
    Key = 0,
    Char = 1,
    MouseMove = 2,
    MouseButton = 3,
    MouseWheel = 4,
    GamepadButton = 5,
    GamepadAxis = 6,
    DeviceConnect = 7,
    DeviceDisconnect = 8,
};

template <typename T>
void appendBytes(std::vector<std::byte>& out, const T& value) {
    const auto* src = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), src, src + sizeof(T));
}

template <typename T>
bool readBytes(std::span<const std::byte> bytes, std::size_t& cursor, T& out) {
    if (cursor + sizeof(T) > bytes.size()) return false;
    std::memcpy(&out, bytes.data() + cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

void writeEvent(std::vector<std::byte>& out, const InputEvent& e) {
    std::visit(
        [&](auto const& ev) {
            using T = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<T, KeyEvent>) {
                appendBytes(out, EventTag::Key); appendBytes(out, ev);
            } else if constexpr (std::is_same_v<T, CharEvent>) {
                appendBytes(out, EventTag::Char); appendBytes(out, ev);
            } else if constexpr (std::is_same_v<T, MouseMoveEvent>) {
                appendBytes(out, EventTag::MouseMove); appendBytes(out, ev);
            } else if constexpr (std::is_same_v<T, MouseButtonEvent>) {
                appendBytes(out, EventTag::MouseButton); appendBytes(out, ev);
            } else if constexpr (std::is_same_v<T, MouseWheelEvent>) {
                appendBytes(out, EventTag::MouseWheel); appendBytes(out, ev);
            } else if constexpr (std::is_same_v<T, GamepadButtonEvent>) {
                appendBytes(out, EventTag::GamepadButton); appendBytes(out, ev);
            } else if constexpr (std::is_same_v<T, GamepadAxisEvent>) {
                appendBytes(out, EventTag::GamepadAxis); appendBytes(out, ev);
            } else if constexpr (std::is_same_v<T, DeviceConnectEvent>) {
                appendBytes(out, EventTag::DeviceConnect); appendBytes(out, ev);
            } else if constexpr (std::is_same_v<T, DeviceDisconnectEvent>) {
                appendBytes(out, EventTag::DeviceDisconnect); appendBytes(out, ev);
            }
        },
        e);
}

bool readEvent(std::span<const std::byte> bytes, std::size_t& cursor, InputEvent& out) {
    EventTag tag{};
    if (!readBytes(bytes, cursor, tag)) return false;
    switch (tag) {
        case EventTag::Key: {
            KeyEvent e{}; if (!readBytes(bytes, cursor, e)) return false; out = e; return true;
        }
        case EventTag::Char: {
            CharEvent e{}; if (!readBytes(bytes, cursor, e)) return false; out = e; return true;
        }
        case EventTag::MouseMove: {
            MouseMoveEvent e{}; if (!readBytes(bytes, cursor, e)) return false; out = e; return true;
        }
        case EventTag::MouseButton: {
            MouseButtonEvent e{}; if (!readBytes(bytes, cursor, e)) return false; out = e; return true;
        }
        case EventTag::MouseWheel: {
            MouseWheelEvent e{}; if (!readBytes(bytes, cursor, e)) return false; out = e; return true;
        }
        case EventTag::GamepadButton: {
            GamepadButtonEvent e{}; if (!readBytes(bytes, cursor, e)) return false; out = e; return true;
        }
        case EventTag::GamepadAxis: {
            GamepadAxisEvent e{}; if (!readBytes(bytes, cursor, e)) return false; out = e; return true;
        }
        case EventTag::DeviceConnect: {
            DeviceConnectEvent e{}; if (!readBytes(bytes, cursor, e)) return false; out = e; return true;
        }
        case EventTag::DeviceDisconnect: {
            DeviceDisconnectEvent e{}; if (!readBytes(bytes, cursor, e)) return false; out = e; return true;
        }
    }
    return false;
}

}  // namespace

void InputTrace::recordCurrentFrame(const InputContext& ctx) {
    const auto evs = ctx.lastFrameEvents();
    Frame f;
    f.events.assign(evs.begin(), evs.end());
    frames_.push_back(std::move(f));
}

void InputTrace::appendFrame(std::span<const InputEvent> events) {
    Frame f;
    f.events.assign(events.begin(), events.end());
    frames_.push_back(std::move(f));
}

bool InputTrace::replayTo(NullBackend& backend, std::uint64_t frameIndex) const {
    if (frameIndex >= frames_.size()) return false;
    const auto& f = frames_[static_cast<std::size_t>(frameIndex)];
    if (!f.events.empty()) {
        backend.pushAll(f.events.data(), f.events.size());
    }
    return true;
}

std::span<const InputEvent> InputTrace::frame(std::uint64_t frameIndex) const noexcept {
    if (frameIndex >= frames_.size()) return {};
    const auto& f = frames_[static_cast<std::size_t>(frameIndex)];
    return std::span<const InputEvent>(f.events.data(), f.events.size());
}

void InputTrace::clear() noexcept {
    frames_.clear();
}

void InputTrace::reserve(std::size_t frames) {
    if (frames_.capacity() < frames) frames_.reserve(frames);
}

std::vector<std::byte> InputTrace::serialize() const {
    std::vector<std::byte> out;
    out.reserve(16 + frames_.size() * 16);

    appendBytes(out, kSerializeMagic);
    appendBytes(out, kSerializeVersion);
    appendBytes(out, static_cast<std::uint64_t>(frames_.size()));

    for (const auto& f : frames_) {
        const auto n = static_cast<std::uint32_t>(f.events.size());
        appendBytes(out, n);
        for (const auto& ev : f.events) writeEvent(out, ev);
    }
    return out;
}

bool InputTrace::deserialize(std::span<const std::byte> bytes) {
    std::size_t cursor = 0;
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t frameCount = 0;
    if (!readBytes(bytes, cursor, magic)) return false;
    if (magic != kSerializeMagic) return false;
    if (!readBytes(bytes, cursor, version)) return false;
    if (version != kSerializeVersion) return false;
    if (!readBytes(bytes, cursor, frameCount)) return false;

    clear();
    reserve(frameCount);
    for (std::uint64_t i = 0; i < frameCount; ++i) {
        std::uint32_t evCount = 0;
        if (!readBytes(bytes, cursor, evCount)) return false;
        Frame f;
        f.events.reserve(evCount);
        for (std::uint32_t j = 0; j < evCount; ++j) {
            InputEvent ev;
            if (!readEvent(bytes, cursor, ev)) return false;
            f.events.push_back(std::move(ev));
        }
        frames_.push_back(std::move(f));
    }
    return true;
}

}  // namespace threadmaxx::input
