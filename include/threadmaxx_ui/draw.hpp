#pragma once

/// @file draw.hpp
/// @brief Flat POD command stream emitted by widgets and consumed by
/// renderer backends. Backends don't reconstruct widget semantics — they
/// translate command-by-command into geometry and draws.
///
/// Storage shape:
///   - `commands_` is the contiguous command buffer.
///   - `textBytes_` is an append-only arena holding all string bodies; each
///     `Text` command stores an `(offset, length)` slice into it.
/// Both vectors reserve `kInitialDrawListCommands` / `kInitialDrawListTextBytes`
/// at construction so steady-state frames don't grow them.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "threadmaxx_ui/config.hpp"
#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

/// One drawable primitive type. The variant lives in `DrawCmd::kind`; the
/// payload union (`rect`, `line`, `text`, `image`, `clip`) tells the backend
/// what to read.
enum class DrawCmdKind : std::uint8_t {
    Rect       = 0,
    Line       = 1,
    Text       = 2,
    Image      = 3,
    ClipPush   = 4,
    ClipPop    = 5,
};

/// Filled or outlined rectangle (`thickness == 0` → filled; positive →
/// outline width in pixels).
struct DrawRect {
    Rect bounds{};
    Color color{};
    std::int32_t thickness = 0;
};

/// Single line segment between two pixel coordinates.
struct DrawLine {
    Vec2i a{};
    Vec2i b{};
    Color color{};
    std::int32_t thickness = 1;
};

/// Text run anchored at `pos`. `textOffset` + `textLength` is the slice into
/// the draw list's text arena.
struct DrawText {
    Vec2i pos{};
    Color color{};
    std::uint32_t textOffset = 0;
    std::uint32_t textLength = 0;
    std::uint32_t fontHandle = 0;
};

/// Textured quad. `imageHandle` is opaque to the UI library; the backend
/// looks it up in its own asset table.
struct DrawImage {
    Rect bounds{};
    Color tint{255, 255, 255, 255};
    std::uint32_t imageHandle = 0;
};

/// Push a clip rect onto the backend's clip stack.
struct DrawClipPush {
    Rect bounds{};
};

/// A single drawable command. Discriminated union; the active member is
/// dictated by `kind`.
struct DrawCmd {
    DrawCmdKind kind = DrawCmdKind::Rect;
    union Payload {
        DrawRect     rect;
        DrawLine     line;
        DrawText     text;
        DrawImage    image;
        DrawClipPush clip;
        Payload() noexcept : rect{} {}
    } payload{};
};

static_assert(sizeof(DrawCmd) <= 64,
              "DrawCmd should fit in a single cache line — pin variant size");

/// Accumulates commands during a frame. Backends consume the
/// `commands()` / `textBytes()` spans after `endFrame()`.
class DrawList {
public:
    DrawList() {
        commands_.reserve(kInitialDrawListCommands);
        textBytes_.reserve(kInitialDrawListTextBytes);
    }

    /// Resets the buffers without releasing capacity. Called at the top of
    /// every `UIContext::beginFrame()`.
    void clear() noexcept {
        commands_.clear();
        textBytes_.clear();
    }

    void emitRect(Rect bounds, Color color, std::int32_t thickness = 0) {
        DrawCmd c;
        c.kind = DrawCmdKind::Rect;
        c.payload.rect = DrawRect{bounds, color, thickness};
        commands_.push_back(c);
    }

    void emitLine(Vec2i a, Vec2i b, Color color, std::int32_t thickness = 1) {
        DrawCmd c;
        c.kind = DrawCmdKind::Line;
        c.payload.line = DrawLine{a, b, color, thickness};
        commands_.push_back(c);
    }

    void emitText(Vec2i pos, Color color, std::string_view text, std::uint32_t fontHandle = 0) {
        const std::size_t oldSize = textBytes_.size();
        const std::uint32_t offset = static_cast<std::uint32_t>(oldSize);
        const std::uint32_t length = static_cast<std::uint32_t>(text.size());
        // Use resize + memcpy rather than `insert(end, begin, end)` — GCC's
        // -Wstringop-overflow gets confused by the iterator-pair insert when
        // it inlines into a TU it can't see the reserved capacity for.
        textBytes_.resize(oldSize + text.size());
        if (length > 0) {
            std::memcpy(textBytes_.data() + oldSize, text.data(), text.size());
        }
        DrawCmd c;
        c.kind = DrawCmdKind::Text;
        c.payload.text = DrawText{pos, color, offset, length, fontHandle};
        commands_.push_back(c);
    }

    void emitImage(Rect bounds, std::uint32_t imageHandle, Color tint = Color{255, 255, 255, 255}) {
        DrawCmd c;
        c.kind = DrawCmdKind::Image;
        c.payload.image = DrawImage{bounds, tint, imageHandle};
        commands_.push_back(c);
    }

    void emitClipPush(Rect bounds) {
        DrawCmd c;
        c.kind = DrawCmdKind::ClipPush;
        c.payload.clip = DrawClipPush{bounds};
        commands_.push_back(c);
    }

    void emitClipPop() {
        DrawCmd c;
        c.kind = DrawCmdKind::ClipPop;
        commands_.push_back(c);
    }

    [[nodiscard]] const std::vector<DrawCmd>& commands() const noexcept { return commands_; }
    [[nodiscard]] const std::vector<char>& textBytes() const noexcept { return textBytes_; }

    /// Returns a view into the text arena for `cmd` if its kind is `Text`,
    /// otherwise an empty view. Backends use this to skip the
    /// `(offset, length)` indirection.
    [[nodiscard]] std::string_view textOf(const DrawCmd& cmd) const noexcept {
        if (cmd.kind != DrawCmdKind::Text) return {};
        const auto& t = cmd.payload.text;
        if (t.textOffset + t.textLength > textBytes_.size()) return {};
        return std::string_view{textBytes_.data() + t.textOffset, t.textLength};
    }

private:
    std::vector<DrawCmd> commands_;
    std::vector<char> textBytes_;
};

} // namespace threadmaxx::ui
