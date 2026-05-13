// ResourceRegistry: typed add/get/remove, generation bump on reuse, type
// safety (a handle from one T does not validate against another T), and
// thread-safe concurrent add.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <thread>
#include <vector>

namespace {

struct Mesh { std::uint32_t triCount = 0; };
struct Texture { std::uint32_t pixelCount = 0; };

struct EmptyGame : threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {}
};

} // namespace

int main() {
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;
    threadmaxx::Engine engine(cfg);
    EmptyGame game;
    CHECK(engine.initialize(game));

    auto& reg = engine.resources();

    // Empty registry.
    CHECK_EQ(reg.count<Mesh>(),    std::size_t{0});
    CHECK_EQ(reg.count<Texture>(), std::size_t{0});

    // Add and read back.
    auto m1 = reg.add(Mesh{42});
    CHECK(m1.valid());
    CHECK_EQ(reg.count<Mesh>(),    std::size_t{1});
    const Mesh* gotM1 = reg.get(m1);
    CHECK(gotM1 != nullptr);
    CHECK_EQ(gotM1->triCount, std::uint32_t{42});

    // Different type, same registry — isolated.
    auto t1 = reg.add(Texture{1024});
    CHECK_EQ(reg.count<Texture>(), std::size_t{1});
    CHECK_EQ(reg.count<Mesh>(),    std::size_t{1});
    const Texture* gotT1 = reg.get(t1);
    CHECK(gotT1 != nullptr);
    CHECK_EQ(gotT1->pixelCount, std::uint32_t{1024});

    // Adding more of one type does not perturb counts of the other.
    auto m_extra = reg.add(Mesh{99});
    CHECK_EQ(reg.count<Mesh>(),    std::size_t{2});
    CHECK_EQ(reg.count<Texture>(), std::size_t{1});
    CHECK(reg.remove(m_extra));

    // Remove and confirm stale handle no longer validates.
    CHECK(reg.remove(m1));
    CHECK(reg.get(m1) == nullptr);
    CHECK_EQ(reg.count<Mesh>(), std::size_t{0});
    // Second remove is a no-op.
    CHECK(!reg.remove(m1));

    // New add reuses the freed slot, but with a bumped generation so the
    // old handle still does not validate.
    auto m2 = reg.add(Mesh{7});
    CHECK_EQ(m2.index, m1.index);
    CHECK(m2.generation != m1.generation);
    CHECK(reg.get(m1) == nullptr);
    CHECK(reg.get(m2) != nullptr);
    CHECK_EQ(reg.get(m2)->triCount, std::uint32_t{7});

    // Concurrent add — N threads each add K meshes; final count must equal
    // N*K plus the one we already have.
    constexpr int kThreads = 8;
    constexpr int kPerThread = 200;
    std::vector<std::thread> threads;
    std::atomic<int> oks{0};
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&reg, &oks, t] {
            for (int i = 0; i < kPerThread; ++i) {
                auto id = reg.add(Mesh{static_cast<std::uint32_t>(t * 10000 + i)});
                if (reg.get(id) != nullptr) oks.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& th : threads) th.join();
    CHECK_EQ(oks.load(), kThreads * kPerThread);
    CHECK_EQ(reg.count<Mesh>(),
             static_cast<std::size_t>(kThreads * kPerThread + 1));

    engine.shutdown();
    EXIT_WITH_RESULT();
}
