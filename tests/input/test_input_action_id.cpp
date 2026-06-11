/// @file test_input_action_id.cpp
/// @brief Pins the constexpr FNV-1a-32 hash for action names.

#include "Check.hpp"

#include "threadmaxx_input/action.hpp"

int main() {
    using namespace threadmaxx::input;

    // 1) constexpr — evaluable at compile time.
    constexpr auto jump = actionId("Jump");
    constexpr auto fire = actionId("Fire");
    static_assert(jump != fire);

    // 2) Deterministic across calls.
    CHECK_EQ(actionId("Jump"), actionId("Jump"));
    CHECK_EQ(actionId(""), actionId(""));

    // 3) Empty string returns the truncated FNV-1a-64 offset basis.
    constexpr ActionId empty = actionId("");
    CHECK_EQ(empty, static_cast<ActionId>(0xcbf29ce484222325ULL & 0xFFFFFFFFu));

    // 4) Distinct names hash distinctly (sanity — collisions still possible
    //    in the 32-bit space, but these short strings shouldn't clash).
    CHECK(actionId("Jump") != actionId("Crouch"));
    CHECK(actionId("MoveForward") != actionId("MoveBackward"));

    EXIT_WITH_RESULT();
}
