/// @file Console.cpp
/// @brief Editor command-line console.

#include "threadmaxx_editor/console.hpp"

#include <algorithm>
#include <sstream>
#include <string>

namespace threadmaxx::editor {

namespace {

std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) { out.push_back(std::move(cur)); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

constexpr std::size_t kHistoryCap = 64;

} // namespace

void Console::registerCommand(ConsoleCommand cmd) {
    for (auto& existing : commands_) {
        if (existing.verb == cmd.verb) {
            existing = std::move(cmd);
            return;
        }
    }
    commands_.push_back(std::move(cmd));
}

EditResult Console::exec(CommandStack& stack, std::string_view line) {
    history_.insert(history_.begin(), std::string(line));
    if (history_.size() > kHistoryCap) history_.resize(kHistoryCap);

    auto tokens = tokenize(line);
    if (tokens.empty()) return EditResult::Rejected;
    const auto& verb = tokens[0];
    for (auto& cmd : commands_) {
        if (cmd.verb != verb) continue;
        std::span<const std::string> args{tokens.data() + 1,
                                          tokens.size() - 1};
        auto out = cmd.handler ? cmd.handler(args) : nullptr;
        if (!out) return EditResult::Applied;
        return stack.execute(std::move(out));
    }
    return EditResult::Rejected;
}

} // namespace threadmaxx::editor
