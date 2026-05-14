// §3.2 batch 7: refcounted ResourceHandle<T>. addRefCounted seeds
// refcount=1; copies bump; destruction decrements; last drop frees the
// slot AND bumps generation so stale ids never alias. Legacy
// `add`/`remove` path stays independent.

#include "Check.hpp"

#include <threadmaxx/Resource.hpp>

#include <utility>

namespace {

struct Mesh {
    int id = 0;
};

} // namespace

int main() {
    using namespace threadmaxx;

    ResourceRegistry reg;

    // ---- legacy add/remove path is unchanged -----------------------------
    auto rawId = reg.add(Mesh{1});
    CHECK(rawId.valid());
    CHECK_EQ(reg.count<Mesh>(), std::size_t{1});
    CHECK(reg.get(rawId) != nullptr);
    // refCount on a non-refcounted slot is reported as 0.
    CHECK_EQ(reg.refCount(rawId), std::uint32_t{0});
    CHECK(reg.remove(rawId));
    CHECK_EQ(reg.count<Mesh>(), std::size_t{0});

    // ---- addRefCounted: 1 handle == refcount 1 ---------------------------
    {
        ResourceHandle<Mesh> h = reg.addRefCounted(Mesh{42});
        CHECK(h.valid());
        CHECK_EQ(reg.refCount(h.id()), std::uint32_t{1});
        CHECK_EQ(reg.count<Mesh>(), std::size_t{1});

        const Mesh* m = reg.get(h.id());
        CHECK(m != nullptr);
        CHECK_EQ(m->id, 42);
    }
    // After the handle goes out of scope, the slot is freed.
    CHECK_EQ(reg.count<Mesh>(), std::size_t{0});

    // ---- copy bumps refcount; both copies must drop before slot frees ----
    ResourceId<Mesh> watched;
    {
        ResourceHandle<Mesh> h1 = reg.addRefCounted(Mesh{7});
        watched = h1.id();
        CHECK_EQ(reg.refCount(watched), std::uint32_t{1});
        {
            ResourceHandle<Mesh> h2 = h1; // copy bumps
            CHECK_EQ(reg.refCount(watched), std::uint32_t{2});
        }
        // h2 dropped; refcount back to 1
        CHECK_EQ(reg.refCount(watched), std::uint32_t{1});
        CHECK(reg.get(watched) != nullptr);
    }
    // h1 dropped; slot is freed AND the id becomes stale.
    CHECK(reg.get(watched) == nullptr);
    CHECK_EQ(reg.count<Mesh>(), std::size_t{0});

    // ---- acquire: bumps refcount on an existing live id ------------------
    {
        ResourceHandle<Mesh> primary = reg.addRefCounted(Mesh{99});
        const ResourceId<Mesh> idCopy = primary.id();
        CHECK_EQ(reg.refCount(idCopy), std::uint32_t{1});

        ResourceHandle<Mesh> shared = reg.acquire(idCopy);
        CHECK(shared.valid());
        CHECK_EQ(reg.refCount(idCopy), std::uint32_t{2});

        primary.reset();
        // shared is the sole owner now; slot still alive.
        CHECK_EQ(reg.refCount(idCopy), std::uint32_t{1});
        CHECK(reg.get(idCopy) != nullptr);

        // acquire on a stale id (different generation): returns null
        // handle and does NOT bump anything.
        ResourceId<Mesh> stale = idCopy;
        stale.generation = 0; // explicit invalid
        ResourceHandle<Mesh> badGen = reg.acquire(stale);
        CHECK(!badGen.valid());
    }
    CHECK_EQ(reg.count<Mesh>(), std::size_t{0});

    // ---- move transfers ownership without bumping refcount ---------------
    {
        ResourceHandle<Mesh> src = reg.addRefCounted(Mesh{5});
        const auto idCopy = src.id();
        CHECK_EQ(reg.refCount(idCopy), std::uint32_t{1});

        ResourceHandle<Mesh> dst = std::move(src);
        CHECK(dst.valid());
        CHECK(!src.valid());
        CHECK_EQ(reg.refCount(idCopy), std::uint32_t{1});
    }
    CHECK_EQ(reg.count<Mesh>(), std::size_t{0});

    // ---- acquire on legacy (non-refcounted) id refuses --------------------
    {
        auto rawId2 = reg.add(Mesh{123});
        ResourceHandle<Mesh> wrong = reg.acquire(rawId2);
        CHECK(!wrong.valid()); // acquire only works on addRefCounted slots
        CHECK(reg.remove(rawId2));
    }

    // ---- self-assignment is a no-op --------------------------------------
    {
        ResourceHandle<Mesh> h = reg.addRefCounted(Mesh{8});
        ResourceHandle<Mesh>* selfp = &h; // dodge -Wself-assign-overloaded
        h = *selfp;
        CHECK(h.valid());
        CHECK_EQ(reg.refCount(h.id()), std::uint32_t{1});
    }
    CHECK_EQ(reg.count<Mesh>(), std::size_t{0});

    EXIT_WITH_RESULT();
}
