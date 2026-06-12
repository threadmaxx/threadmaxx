/// @file test_reflect_engine_bridge_idempotent.cpp
/// @brief Calling `registerBuiltins` twice on the same registry is a
/// no-op (no duplicate entries).

#include "Check.hpp"

#include <threadmaxx_reflect/engine_bridge.hpp>
#include <threadmaxx_reflect/registry.hpp>

#ifdef THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE

int main() {
    using namespace threadmaxx::reflect;
    using namespace threadmaxx::reflect::engine_bridge;

    TypeRegistry reg;
    registerBuiltins(reg);
    const std::size_t first = reg.size();
    CHECK_EQ(first, 14u);

    registerBuiltins(reg);
    CHECK_EQ(reg.size(), first);

    registerBuiltins(reg);
    CHECK_EQ(reg.size(), first);

    EXIT_WITH_RESULT();
}

#else
int main() { return 0; }
#endif
