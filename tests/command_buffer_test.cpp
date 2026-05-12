// Sanity-checks CommandBuffer recording: counts increase, clear empties,
// move semantics work.

#include "Check.hpp"

#include <threadmaxx/CommandBuffer.hpp>

int main() {
    threadmaxx::CommandBuffer cb;
    CHECK(cb.empty());
    CHECK_EQ(cb.size(), std::size_t{0});

    cb.spawn({});
    cb.spawn({});
    cb.destroy(threadmaxx::EntityHandle{1, 1});
    cb.setTransform(threadmaxx::EntityHandle{2, 1}, {});
    cb.setVelocity(threadmaxx::EntityHandle{3, 1}, {});
    cb.setRenderTag(threadmaxx::EntityHandle{4, 1}, {});
    cb.setUserData(threadmaxx::EntityHandle{5, 1}, {});

    CHECK_EQ(cb.size(), std::size_t{7});
    CHECK(!cb.empty());

    threadmaxx::CommandBuffer moved = std::move(cb);
    CHECK_EQ(moved.size(), std::size_t{7});

    moved.clear();
    CHECK(moved.empty());
    CHECK_EQ(moved.size(), std::size_t{0});

    EXIT_WITH_RESULT();
}
