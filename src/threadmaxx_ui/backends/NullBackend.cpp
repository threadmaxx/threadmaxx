/// @file backends/NullBackend.cpp
/// @brief Sink backend that drops every submitted frame. Used by the test
/// suite (so the no-alloc and command-stream tests don't have to spin up a
/// renderer) and by hosts that want the library compiled in but currently
/// have no UI to draw.

#include "threadmaxx_ui/backends/NullBackend.hpp"

#include "threadmaxx_ui/draw.hpp"

namespace threadmaxx::ui {

void NullBackend::submit(const DrawList& list) {
    lastCommands_ = list.commands().size();
    lastTextBytes_ = list.textBytes().size();
    ++submitCount_;
}

} // namespace threadmaxx::ui
