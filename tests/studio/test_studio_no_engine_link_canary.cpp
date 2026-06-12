/// @file test_studio_no_engine_link_canary.cpp
/// @brief Pin the engine-agnosticism contract of the studio core.
///
/// `threadmaxx_studio/panel.hpp` + `data_source.hpp` MUST NOT pull
/// any `threadmaxx/` core header transitively. Concrete data sources
/// (`DirectDataSource`, ST4) opt into the engine; the interfaces
/// themselves stay pure so the same panel binary works in-process
/// (Shape A) and over the remote wire (Shape B).
///
/// We assert via `#ifdef`: every core header defines its include
/// guard / a recognizable macro; if one snuck in, we fail to compile.

#include "Check.hpp"

#include <threadmaxx_studio/data_source.hpp>
#include <threadmaxx_studio/panel.hpp>

// The core engine's version macro is the canary. If pulling
// `panel.hpp` / `data_source.hpp` dragged in `<threadmaxx/version.hpp>`
// transitively, this would fail to compile.
#ifdef THREADMAXX_VERSION_MAJOR
#  error "studio core MUST NOT pull threadmaxx/ engine headers"
#endif

// Same probe for the engine's umbrella + a couple of load-bearing
// headers used heavily inside threadmaxx_editor. Each defines a
// distinctive include-guard token in the core surface.
#ifdef THREADMAXX_ENGINE_HPP
#  error "studio core MUST NOT pull threadmaxx/Engine.hpp"
#endif
#ifdef THREADMAXX_WORLD_HPP
#  error "studio core MUST NOT pull threadmaxx/World.hpp"
#endif

int main() {
    // The build-time assertions above are the real test. The runtime
    // body just confirms the headers usefully compile in isolation.
    threadmaxx::studio::AttachMode m = threadmaxx::studio::AttachMode::Direct;
    CHECK(m == threadmaxx::studio::AttachMode::Direct);
    EXIT_WITH_RESULT();
}
