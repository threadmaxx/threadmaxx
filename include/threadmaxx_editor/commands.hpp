#pragma once

/// @file commands.hpp
/// @brief Edit command + undo/redo stack.
///
/// Editor mutations are recipes that record into the engine's
/// `CommandBuffer`, never direct world writes. The stack owns the
/// `IEditCommand`s; an engine-registered pump-system drains pending
/// apply / undo operations on every `preStep` boundary, committing
/// through `ctx.single()`'s buffer so the engine sees them on the
/// normal deterministic-commit path.

#include "types.hpp"

#include <cstddef>
#include <memory>
#include <string_view>

#include <threadmaxx/Engine.hpp>

namespace threadmaxx {
class CommandBuffer;
} // namespace threadmaxx

namespace threadmaxx::editor {

/// @brief One reversible editor action. Subclasses encode both the
/// forward effect and its inverse.
///
/// Both methods receive a `CommandBuffer` belonging to the engine's
/// editor pump-system (a `ctx.single()` slice). The command must not
/// retain the buffer reference past return.
class IEditCommand {
public:
    virtual ~IEditCommand() = default;

    /// @brief Stable identifier for HUD / debugger display.
    virtual std::string_view name() const noexcept = 0;

    /// @brief Record the forward effect.
    virtual void apply(threadmaxx::CommandBuffer& cb) = 0;

    /// @brief Record the inverse effect.
    virtual void undo(threadmaxx::CommandBuffer& cb) = 0;
};

namespace internal {
struct CommandStackState;
} // namespace internal

/// @brief Undo/redo deck. Owns a vector of `IEditCommand`s and a cursor
/// into it. `execute` and `undo` / `redo` queue work for the engine's
/// next `preStep`.
class CommandStack {
public:
    explicit CommandStack(threadmaxx::Engine& engine);
    ~CommandStack();

    CommandStack(const CommandStack&) = delete;
    CommandStack& operator=(const CommandStack&) = delete;
    CommandStack(CommandStack&&) = delete;
    CommandStack& operator=(CommandStack&&) = delete;

    /// @brief Record + queue a new command. Discards any redo history
    /// past the current cursor.
    EditResult execute(std::unique_ptr<IEditCommand> cmd);

    /// @brief Walk the cursor back one slot and queue the corresponding
    /// command's `undo`. No-op when `!canUndo()`.
    EditResult undo();

    /// @brief Walk the cursor forward one slot and queue the
    /// corresponding command's `apply`. No-op when `!canRedo()`.
    EditResult redo();

    /// @brief Drop every command and rewind the cursor. Outstanding
    /// pending operations (queued but not yet drained by the pump) are
    /// kept — they were already accepted by the engine.
    void clear();

    bool canUndo() const noexcept;
    bool canRedo() const noexcept;

    /// @brief Total recorded commands (undo deck + redo deck).
    std::size_t historySize() const noexcept;

    /// @brief Cursor position. Equal to the number of commands that
    /// would be undone if `undo()` were called once per slot back to 0.
    std::size_t cursor() const noexcept;

private:
    std::shared_ptr<internal::CommandStackState> state_;
    threadmaxx::Engine* engine_;
};

} // namespace threadmaxx::editor
