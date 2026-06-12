#pragma once

/// @file direct_data_source.hpp
/// @brief `DirectDataSource` — the in-process Shape A implementation
/// of `IStudioDataSource`. Direct engine + sibling pointer reads;
/// mutations routed through `editor::CommandStack`. Every M2+ panel
/// consumes the data source through the interface, but the concrete
/// `DirectDataSource` exposes engine-specific accessors
/// (`worldSnapshot`, `submitEditCommand`) that sidestep the cross-
/// attach-mode envelope when the panel knows it's running in-process.
///
/// This header opts the studio into the core engine — pulling it
/// transitively brings `threadmaxx/` headers. Headers that need to
/// stay engine-agnostic (`panel.hpp`, `data_source.hpp`) MUST NOT
/// include this file.

#include "data_source.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace threadmaxx {
class Engine;
struct WorldSnapshot;
} // namespace threadmaxx

namespace threadmaxx::editor {
class CommandStack;
class IEditCommand;
} // namespace threadmaxx::editor

namespace threadmaxx::studio {

class DirectDataSource final : public IStudioDataSource {
public:
    /// @brief Bind to a live engine + a `CommandStack` over the same
    /// engine. Caller owns lifetime; both must outlive the source.
    DirectDataSource(threadmaxx::Engine& engine,
                     editor::CommandStack& stack) noexcept;

    AttachMode mode() const noexcept override { return AttachMode::Direct; }

    /// @brief Copy of the engine's most recent `FrameSnapshot`, lifted
    /// into the studio's wire-stable mirror POD.
    std::optional<EngineFrameSummary> engineSnapshot() const override;

    /// @brief Deep copy of `engine.world().snapshot()`. Direct-only;
    /// the cross-attach-mode interface stays free of engine headers.
    threadmaxx::WorldSnapshot worldSnapshot() const;

    /// @brief Hand a real `editor::IEditCommand` to the command stack.
    /// Direct-only — Shape B serializes commands over the editor v1.2
    /// remote wire instead. Returns `false` if `cmd` is null.
    bool submitEditCommand(std::unique_ptr<editor::IEditCommand> cmd);

    /// @brief The opaque cross-attach-mode envelope is intentionally
    /// rejected here; panels that need to submit edits in Direct
    /// mode call `submitEditCommand` instead.
    bool submitCommand(std::string_view /*label*/) override { return false; }

    /// @brief Non-owning handles. Useful for panels that need engine-
    /// or editor-specific surfaces beyond what the interface exposes.
    threadmaxx::Engine&       engine()       noexcept { return *engine_; }
    const threadmaxx::Engine& engine() const noexcept { return *engine_; }
    editor::CommandStack&     stack()        noexcept { return *stack_; }

private:
    threadmaxx::Engine* engine_{nullptr};
    editor::CommandStack* stack_{nullptr};
};

} // namespace threadmaxx::studio
