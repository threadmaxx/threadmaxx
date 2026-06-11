/// @file test_input_char_queue.cpp
/// @brief Pins the char-queue contract: events queue into chars[] in
/// order, overflow past kMaxCharsPerFrame drops silently (no realloc),
/// and the queue resets each beginFrame.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/config.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Frame 0 — four chars in order.
    backend.push(CharEvent{'a'});
    backend.push(CharEvent{'b'});
    backend.push(CharEvent{'c'});
    backend.push(CharEvent{'d'});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.state().charCount, std::uint8_t{4});
    CHECK_EQ(ctx.state().chars[0], std::uint32_t{'a'});
    CHECK_EQ(ctx.state().chars[1], std::uint32_t{'b'});
    CHECK_EQ(ctx.state().chars[2], std::uint32_t{'c'});
    CHECK_EQ(ctx.state().chars[3], std::uint32_t{'d'});
    ctx.endFrame();

    // Frame 1 — empty. Count resets.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.state().charCount, std::uint8_t{0});
    ctx.endFrame();

    // Frame 2 — overflow. Push 2x the cap; the queue caps at kMaxCharsPerFrame
    // and the overflow drops without allocating or asserting.
    for (std::size_t i = 0; i < kMaxCharsPerFrame * 2; ++i) {
        backend.push(CharEvent{static_cast<std::uint32_t>('A' + (i % 26))});
    }
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.state().charCount, static_cast<std::uint8_t>(kMaxCharsPerFrame));
    // First kMaxCharsPerFrame entries should match the head of the push
    // sequence (overflow drops the TAIL).
    for (std::size_t i = 0; i < kMaxCharsPerFrame; ++i) {
        CHECK_EQ(ctx.state().chars[i],
                 static_cast<std::uint32_t>('A' + (i % 26)));
    }
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
