#pragma once

/// @file console.hpp
/// @brief Editor command-line console — typed commands dispatch to
/// registered handlers, optionally producing an IEditCommand to feed
/// into the editor's CommandStack.

#include "commands.hpp"
#include "types.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace threadmaxx::editor {

/// @brief One registered console command.
struct ConsoleCommand {
    /// @brief Command name as the user types it.
    std::string verb;
    /// @brief Handler — receives the rest of the line (whitespace-
    /// separated tokens parsed by the caller) and returns either a
    /// reversible `IEditCommand` to queue, or nullptr for built-in
    /// commands that have no engine-side effect.
    std::function<std::unique_ptr<IEditCommand>(
        std::span<const std::string>)> handler;
};

class Console {
public:
    /// @brief Register a command. Re-registering overwrites.
    void registerCommand(ConsoleCommand cmd);

    /// @brief Exec `line` — tokenize, look up the verb, dispatch.
    /// Returns the result. Empty / unknown / handler-rejected lines
    /// return `Rejected`. Lines that produced an `IEditCommand` are
    /// queued via `stack.execute(...)`.
    EditResult exec(CommandStack& stack, std::string_view line);

    /// @brief Newest-first command history (capped at 64 entries).
    std::vector<std::string> history() const { return history_; }

    /// @brief Walk history backward; `index == 0` is the most recently
    /// executed line. Returns empty when out of range.
    std::string historyAt(std::size_t index) const noexcept {
        return index < history_.size() ? history_[index] : std::string{};
    }

    std::size_t commandCount() const noexcept { return commands_.size(); }

private:
    std::vector<ConsoleCommand> commands_;
    std::vector<std::string> history_;
};

} // namespace threadmaxx::editor
