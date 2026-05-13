// §3.2 ScratchArena: bump allocation, alignment, growth across slabs,
// reset, and the JobFnArena parallelFor/single overloads.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <atomic>

namespace {

// Standalone unit tests of the ScratchArena itself.
void testAlone() {
    threadmaxx::ScratchArena a(64);
    auto* p1 = a.allocate<std::uint32_t>(4);
    CHECK(p1 != nullptr);
    for (int i = 0; i < 4; ++i) p1[i] = static_cast<std::uint32_t>(i + 1);

    auto* p2 = a.allocate<double>(2);
    CHECK(p2 != nullptr);
    // double alignment must be respected.
    CHECK_EQ(reinterpret_cast<std::uintptr_t>(p2) % alignof(double),
             std::uintptr_t{0});
    p2[0] = 3.14; p2[1] = 2.71;

    // Earlier pointer remained valid.
    CHECK_EQ(p1[0], std::uint32_t{1});
    CHECK_EQ(p1[3], std::uint32_t{4});

    // Growth: allocate more than the initial capacity so a new slab is
    // pushed. Previously-issued pointers must still be valid.
    auto* big = a.allocate<std::uint8_t>(256);
    CHECK(big != nullptr);
    CHECK_EQ(p1[0], std::uint32_t{1});
    CHECK(a.capacity() >= 256u);

    // Reset rewinds the bump pointer. Old pointers are now dangling, but
    // a fresh allocation reuses the same underlying memory.
    a.reset();
    CHECK_EQ(a.bytesUsed(), std::size_t{0});
    auto* after = a.allocate<std::uint32_t>(2);
    CHECK(after != nullptr);
    // We don't compare addresses (the slab layout is allowed to differ),
    // but the capacity must be at least as much as before reset.
    CHECK(a.capacity() >= 256u);
}

// Sums per-chunk allocations into a single result. Each chunk gets its
// own arena, so there's no aliasing across workers.
class ArenaSumSystem : public threadmaxx::ISystem {
public:
    std::atomic<std::int64_t> totalAcrossChunks{0};

    const char* name() const noexcept override { return "arena_sum"; }
    void update(threadmaxx::SystemContext& ctx) override {
        ctx.parallelFor(/*count*/ 100, /*grain*/ 10,
            [this](threadmaxx::Range r, threadmaxx::CommandBuffer&,
                   threadmaxx::ScratchArena& arena) {
                // Use the arena for a scratch sum buffer per chunk.
                auto* buf = arena.allocate<std::int64_t>(r.size());
                CHECK(buf != nullptr);
                for (std::uint32_t i = 0; i < r.size(); ++i) {
                    buf[i] = static_cast<std::int64_t>(r.begin + i);
                }
                std::int64_t s = 0;
                for (std::uint32_t i = 0; i < r.size(); ++i) s += buf[i];
                totalAcrossChunks.fetch_add(s, std::memory_order_relaxed);
            });
    }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
};

struct ArenaGame : threadmaxx::IGame {
    ArenaSumSystem* sysPtr = nullptr;
    void onSetup(threadmaxx::Engine& e, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {
        auto s = std::make_unique<ArenaSumSystem>();
        sysPtr = s.get();
        e.registerSystem(std::move(s));
    }
};

void testInSystem() {
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;
    threadmaxx::Engine e(cfg);
    ArenaGame g;
    CHECK(e.initialize(g));
    e.step();
    // Sum of 0..99 = 4950.
    CHECK_EQ(g.sysPtr->totalAcrossChunks.load(), std::int64_t{4950});
    e.shutdown();
}

// single(JobFnArena) overload smoke test.
class SingleArenaSystem : public threadmaxx::ISystem {
public:
    bool ranWithArena = false;
    const char* name() const noexcept override { return "single_arena"; }
    void update(threadmaxx::SystemContext& ctx) override {
        ctx.single([this](threadmaxx::Range, threadmaxx::CommandBuffer&,
                          threadmaxx::ScratchArena& arena) {
            auto* p = arena.allocate<std::uint64_t>(8);
            CHECK(p != nullptr);
            p[7] = 99;
            ranWithArena = (p[7] == 99);
        });
    }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
};

void testSingleArena() {
    threadmaxx::Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
    threadmaxx::Engine e(cfg);
    struct G : threadmaxx::IGame {
        SingleArenaSystem* p = nullptr;
        void onSetup(threadmaxx::Engine& eng, threadmaxx::World&,
                     threadmaxx::CommandBuffer&) override {
            auto s = std::make_unique<SingleArenaSystem>();
            p = s.get();
            eng.registerSystem(std::move(s));
        }
    } g;
    CHECK(e.initialize(g));
    e.step();
    CHECK(g.p->ranWithArena);
    e.shutdown();
}

} // namespace

int main() {
    testAlone();
    testInSystem();
    testSingleArena();
    EXIT_WITH_RESULT();
}
