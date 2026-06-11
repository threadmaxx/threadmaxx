#pragma once

/// @file backends/NullBackend.hpp
/// @brief Sink backend used by tests + headless hosts. Drops every frame but
/// keeps counters so tests can assert that `endFrame()` actually fired.

#include <cstddef>
#include <cstdint>

#include "threadmaxx_ui/backend.hpp"

namespace threadmaxx::ui {

class DrawList;

/// Implements `IUIBackend` by recording byte-counts but not drawing. Use
/// `submitCount()` to verify a frame round-tripped through the backend, and
/// `lastCommands()` / `lastTextBytes()` to verify the draw list shape
/// without parsing it.
class NullBackend final : public IUIBackend {
public:
    void submit(const DrawList& list) override;

    [[nodiscard]] std::uint64_t submitCount() const noexcept { return submitCount_; }
    [[nodiscard]] std::size_t lastCommands() const noexcept { return lastCommands_; }
    [[nodiscard]] std::size_t lastTextBytes() const noexcept { return lastTextBytes_; }

    void reset() noexcept {
        submitCount_ = 0;
        lastCommands_ = 0;
        lastTextBytes_ = 0;
    }

private:
    std::uint64_t submitCount_ = 0;
    std::size_t lastCommands_ = 0;
    std::size_t lastTextBytes_ = 0;
};

} // namespace threadmaxx::ui
