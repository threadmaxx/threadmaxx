// threadmaxx_simd — `span_view` adapter tests.
//
// Verifies:
//   1. `view(span<T>)` returns a `span_view<T>` whose `data()` /
//      `size()` mirror the underlying span.
//   2. `view(span<const T>)` returns a `span_view<const T>`.
//   3. An empty span produces a view with `empty() == true` and
//      `size() == 0`.
//   4. The view's `data()` matches the underlying buffer's address
//      (no copying).

#include "Check.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx_simd/views.hpp>

#include <cstdio>
#include <span>
#include <type_traits>
#include <vector>

int main() {
    using threadmaxx::Vec3;
    using namespace threadmaxx::simd;

    // ---- 1. writable view round-trip ------------------------------------
    std::vector<Vec3> buf = {
        {1.0f, 2.0f, 3.0f},
        {4.0f, 5.0f, 6.0f},
        {7.0f, 8.0f, 9.0f},
    };
    {
        auto v = view(std::span<Vec3>(buf));
        static_assert(std::is_same_v<decltype(v), span_view<Vec3>>,
            "view of span<Vec3> should deduce span_view<Vec3>");
        CHECK_EQ(v.size(), buf.size());
        CHECK(!v.empty());
        CHECK_EQ(v.data(), buf.data());
        // Mutation through the view is visible in the buffer.
        v.values[0].x = 42.0f;
        CHECK_EQ(buf[0].x, 42.0f);
        std::printf("[simd_views] writable view OK (size=%zu)\n", v.size());
    }

    // ---- 2. const view --------------------------------------------------
    {
        auto v = view(std::span<const Vec3>(buf));
        static_assert(std::is_same_v<decltype(v), span_view<const Vec3>>,
            "view of span<const Vec3> should deduce span_view<const Vec3>");
        CHECK_EQ(v.size(), buf.size());
        CHECK_EQ(v.data(), buf.data());
        std::printf("[simd_views] const view OK\n");
    }

    // ---- 3. empty span ---------------------------------------------------
    {
        std::span<Vec3> empty;
        auto v = view(empty);
        CHECK(v.empty());
        CHECK_EQ(v.size(), std::size_t{0});
        std::printf("[simd_views] empty view OK\n");
    }

    // ---- 4. const view forwarding from an explicit slice ----------------
    {
        // Construct an explicit `std::span<const Vec3>` from a sub-range
        // and verify the view tracks size + data correctly.
        std::span<const Vec3> slice(buf.data() + 1, 2);
        auto v = view(slice);
        CHECK_EQ(v.size(), std::size_t{2});
        CHECK_EQ(v.data(), buf.data() + 1);
        CHECK_EQ(v.values[0].x, 4.0f);
        std::printf("[simd_views] explicit slice view OK\n");
    }

    EXIT_WITH_RESULT();
}
