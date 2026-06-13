#pragma once

/// @file panels/replay.hpp
/// @brief ST23 — `ReplayPanel` wraps editor E15's `ReplaySession`.
/// Owns no stream; borrows one from the host. Provides scrub
/// controls (`seek` / `step`) and renders the current frame's
/// header + entity rows.

#include "../panel.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace threadmaxx::editor {
class CaptureStream;
class ReplaySession;
} // namespace threadmaxx::editor

namespace threadmaxx::studio {

class ReplayPanel : public IStudioPanel {
public:
    ReplayPanel() noexcept = default;
    ~ReplayPanel() override;

    ReplayPanel(const ReplayPanel&) = delete;
    ReplayPanel& operator=(const ReplayPanel&) = delete;
    ReplayPanel(ReplayPanel&&) = delete;
    ReplayPanel& operator=(ReplayPanel&&) = delete;

    /// @brief Bind a stream. The panel creates an internal
    /// ReplaySession over it. Pass nullptr to detach.
    void setStream(const editor::CaptureStream* stream);

    [[nodiscard]] const editor::CaptureStream* stream() const noexcept {
        return stream_;
    }

    /// @brief Move the cursor (delegates to the underlying session).
    void seek(std::size_t index);
    void step(std::int64_t delta);

    std::string_view id() const noexcept override {
        return "sibling.replay";
    }
    std::string_view title() const noexcept override { return "Replay"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t cursor() const noexcept;
    [[nodiscard]] std::uint64_t currentTick() const noexcept;
    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }
    /// @brief Max entity rows shown after the header (default 8).
    void setMaxEntityRows(std::size_t n) noexcept { maxRows_ = n; }

private:
    const editor::CaptureStream* stream_{nullptr};
    // ReplaySession is non-owning, so we keep a small heap-owned
    // instance via unique_ptr-shaped storage; raw new + delete to
    // avoid pulling <memory> through the public header.
    editor::ReplaySession*       session_{nullptr};
    std::size_t                  maxRows_{8};
    std::size_t                  lastRows_{0};

    void teardownSession();
};

} // namespace threadmaxx::studio
