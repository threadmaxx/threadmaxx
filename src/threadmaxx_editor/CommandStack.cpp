/// @file CommandStack.cpp
/// @brief CommandStack + pump-system that drains pending edits each
/// preStep through the engine's CommandBuffer path.

#include "threadmaxx_editor/commands.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/System.hpp>

#include <deque>
#include <mutex>
#include <vector>

namespace threadmaxx::editor::internal {

enum class PendingKind : std::uint8_t { Apply, Undo };

struct PendingOp {
    PendingKind kind;
    IEditCommand* cmd;
};

struct CommandStackState {
    std::mutex mtx;
    std::vector<std::unique_ptr<IEditCommand>> history;
    std::size_t cursor{0}; // points one past the most-recently-applied
    std::deque<PendingOp> pending;
};

namespace {

class EditorPumpSystem final : public ::threadmaxx::ISystem {
public:
    explicit EditorPumpSystem(std::shared_ptr<CommandStackState> state)
        : state_(std::move(state)) {}

    const char* name() const noexcept override {
        return "threadmaxx_editor::CommandStack";
    }

    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::all();
    }

    void preStep(threadmaxx::SystemContext& ctx) override {
        std::deque<PendingOp> drained;
        {
            std::lock_guard<std::mutex> lk(state_->mtx);
            drained.swap(state_->pending);
        }
        if (drained.empty()) return;
        ctx.single([&drained](threadmaxx::Range,
                              threadmaxx::CommandBuffer& cb) {
            for (auto& op : drained) {
                if (!op.cmd) continue;
                if (op.kind == PendingKind::Apply) op.cmd->apply(cb);
                else                                op.cmd->undo(cb);
            }
        });
    }

    void update(threadmaxx::SystemContext&) override {}

private:
    std::shared_ptr<CommandStackState> state_;
};

} // namespace

} // namespace threadmaxx::editor::internal

namespace threadmaxx::editor {

CommandStack::CommandStack(threadmaxx::Engine& engine)
    : state_(std::make_shared<internal::CommandStackState>()),
      engine_(&engine) {
    engine.registerSystem(
        std::make_unique<internal::EditorPumpSystem>(state_));
}

CommandStack::~CommandStack() = default;

EditResult CommandStack::execute(std::unique_ptr<IEditCommand> cmd) {
    if (!cmd) return EditResult::Rejected;
    IEditCommand* raw = cmd.get();
    {
        std::lock_guard<std::mutex> lk(state_->mtx);
        // Discard the redo deck past the cursor.
        if (state_->cursor < state_->history.size()) {
            state_->history.erase(state_->history.begin() +
                                  static_cast<std::ptrdiff_t>(state_->cursor),
                                  state_->history.end());
        }
        state_->history.push_back(std::move(cmd));
        ++state_->cursor;
        state_->pending.push_back({internal::PendingKind::Apply, raw});
    }
    return EditResult::Deferred;
}

EditResult CommandStack::undo() {
    std::lock_guard<std::mutex> lk(state_->mtx);
    if (state_->cursor == 0) return EditResult::Rejected;
    --state_->cursor;
    auto* raw = state_->history[state_->cursor].get();
    state_->pending.push_back({internal::PendingKind::Undo, raw});
    return EditResult::Deferred;
}

EditResult CommandStack::redo() {
    std::lock_guard<std::mutex> lk(state_->mtx);
    if (state_->cursor >= state_->history.size()) return EditResult::Rejected;
    auto* raw = state_->history[state_->cursor].get();
    ++state_->cursor;
    state_->pending.push_back({internal::PendingKind::Apply, raw});
    return EditResult::Deferred;
}

void CommandStack::clear() {
    std::lock_guard<std::mutex> lk(state_->mtx);
    state_->history.clear();
    state_->cursor = 0;
}

bool CommandStack::canUndo() const noexcept {
    std::lock_guard<std::mutex> lk(state_->mtx);
    return state_->cursor > 0;
}

bool CommandStack::canRedo() const noexcept {
    std::lock_guard<std::mutex> lk(state_->mtx);
    return state_->cursor < state_->history.size();
}

std::size_t CommandStack::historySize() const noexcept {
    std::lock_guard<std::mutex> lk(state_->mtx);
    return state_->history.size();
}

std::size_t CommandStack::cursor() const noexcept {
    std::lock_guard<std::mutex> lk(state_->mtx);
    return state_->cursor;
}

} // namespace threadmaxx::editor
