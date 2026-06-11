/// @file test_input_no_engine_link.cpp
/// @brief Pins the engine-decoupling invariant: every public
/// `threadmaxx_input` header compiles without dragging in any
/// `threadmaxx/` core header. The header-cycle check is enforced via
/// `#error` if `THREADMAXX_VERSION_HPP` or any other core sentinel
/// gets defined transitively.
///
/// This test is the single-file canary the spec promises (§7.7).

#include "Check.hpp"

// Pull the umbrella + every public header individually. If any of these
// transitively included `threadmaxx/Engine.hpp` or another core header,
// the standard guards it ships would define the sentinels below.
#include "threadmaxx_input/threadmaxx_input.hpp"
#include "threadmaxx_input/backend.hpp"
#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/config.hpp"
#include "threadmaxx_input/context.hpp"
#include "threadmaxx_input/events.hpp"
#include "threadmaxx_input/state.hpp"
#include "threadmaxx_input/types.hpp"
#include "threadmaxx_input/trace.hpp"
#include "threadmaxx_input/version.hpp"
#include "threadmaxx_input/backends/GlfwBackend.hpp"
#include "threadmaxx_input/detail/edge_buffer.hpp"

#if defined(THREADMAXX_VERSION_HPP)               \
    || defined(THREADMAXX_ENGINE_HPP)             \
    || defined(THREADMAXX_WORLD_HPP)              \
    || defined(THREADMAXX_COMPONENTS_HPP)         \
    || defined(THREADMAXX_GAME_HPP)               \
    || defined(THREADMAXX_SYSTEM_HPP)
#  error "threadmaxx_input must not transitively include core engine headers"
#endif

int main() {
    // Touch a symbol from each header so the linker can't optimize the
    // TU away.
    using namespace threadmaxx::input;
    (void)kMaxGamepads;
    InputState s{};
    (void)s;
    InputContext ctx;
    (void)ctx;
    NullBackend backend;
    (void)backend;
    CHECK_EQ(version_string(), "0.1.0");
    EXIT_WITH_RESULT();
}
