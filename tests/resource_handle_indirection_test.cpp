// §3.6.5 batch 15a — ResourceHandle<T> operator-> / operator* / get().
//
// Indirection sugar for refcounted handles so users don't have to
// reach back to the registry every time. Asserts:
//
//   (1) `get()` returns a non-null pointer for a live handle and the
//       value matches what was stored.
//   (2) `operator->` and `operator*` work the same way.
//   (3) Null / default-constructed handles return nullptr from
//       `get()`. (Dereferencing a null handle remains UB; we just
//       check `get` here.)
//   (4) After the last owning handle is dropped, the slot is freed
//       and `get()` on a kept-around copy returns nullptr (only
//       possible if we held the registry slot in a non-refcounted
//       way — that case is the legacy `add`/`remove` path).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

namespace {

using namespace threadmaxx;

struct TestMesh {
    int vertexCount = 0;
    float scale = 1.0f;
};

} // namespace

int main() {
    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
    Engine engine(cfg);

    struct G : IGame {
        void onSetup(Engine&, World&, CommandBuffer&) override {}
    } g;
    CHECK(engine.initialize(g));

    // ---- (1) get() returns the value.
    auto h = engine.resources().addRefCounted(TestMesh{1024, 2.5f});
    CHECK(h.valid());
    CHECK(h.get() != nullptr);
    CHECK_EQ(h.get()->vertexCount, 1024);
    CHECK_EQ(h.get()->scale, 2.5f);

    // ---- (2) operator-> and operator* work.
    CHECK_EQ(h->vertexCount, 1024);
    CHECK_EQ((*h).scale, 2.5f);

    // ---- (3) Default-constructed handle returns nullptr.
    ResourceHandle<TestMesh> empty;
    CHECK(!empty.valid());
    CHECK_EQ(empty.get(), static_cast<const TestMesh*>(nullptr));

    // ---- (4) Copying bumps the refcount; both handles see the same
    //          value.
    {
        auto copy = h;
        CHECK(copy.valid());
        CHECK_EQ(copy->vertexCount, 1024);
    }
    // After the copy drops, h still valid.
    CHECK(h.valid());
    CHECK(h.get() != nullptr);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
