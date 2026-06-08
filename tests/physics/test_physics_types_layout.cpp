#include "Check.hpp"

#include "threadmaxx_physics/body.hpp"
#include "threadmaxx_physics/shape.hpp"
#include "threadmaxx_physics/types.hpp"

#include <cstring>
#include <type_traits>

using namespace threadmaxx::physics;

int main() {
    // The id PODs are trivially-copyable handles. Backend authors rely
    // on memcpy-round-trip — if any of these become non-trivial,
    // anything that snapshots a span of ids will silently break.
    static_assert(std::is_trivially_copyable_v<PhysicsWorldId>);
    static_assert(std::is_trivially_copyable_v<BodyId>);
    static_assert(std::is_trivially_copyable_v<ShapeId>);
    static_assert(std::is_trivially_copyable_v<JointId>);

    // BodyState is the per-tick read-back; the sync.hpp API in P3 will
    // memcpy spans of BodyState, so it must stay trivially copyable.
    static_assert(std::is_trivially_copyable_v<BodyState>);

    // Default-constructed id is the "invalid" sentinel.
    PhysicsWorldId w{};
    CHECK_EQ(w.value, std::uint64_t{0});
    CHECK(!static_cast<bool>(w));

    BodyId b{};
    CHECK_EQ(b.value, std::uint64_t{0});
    CHECK(!static_cast<bool>(b));

    // memcpy round-trip on BodyState (the API contract for batch sync).
    BodyState src;
    src.position = Vec3{1.0f, 2.0f, 3.0f};
    src.linearVelocity = Vec3{4.0f, 5.0f, 6.0f};
    src.rotation.x = 0.1f;
    src.rotation.y = 0.2f;
    src.rotation.z = 0.3f;
    src.rotation.w = 0.4f;
    BodyState dst{};
    std::memcpy(&dst, &src, sizeof(BodyState));
    CHECK(dst.position.x == 1.0f);
    CHECK(dst.position.y == 2.0f);
    CHECK(dst.position.z == 3.0f);
    CHECK(dst.linearVelocity.y == 5.0f);
    CHECK(dst.rotation.w == 0.4f);

    // BodyDesc carries no STL containers — also memcpy-safe.
    static_assert(std::is_trivially_copyable_v<BodyDesc>);

    // ShapeDesc DOES carry std::vector — must NOT be trivially copyable;
    // pin that so a future "let's make it a POD" refactor breaks here
    // instead of silently slicing vertices/indices.
    static_assert(!std::is_trivially_copyable_v<ShapeDesc>);

    // BodyType is a plain enum.
    CHECK(BodyType::Dynamic != BodyType::Static);
    CHECK(BodyType::Kinematic != BodyType::Dynamic);

    // Equality + bool conversion behave as advertised.
    BodyId a1{42};
    BodyId a2{42};
    BodyId a3{43};
    CHECK(a1 == a2);
    CHECK(!(a1 == a3));
    CHECK(static_cast<bool>(a1));

    EXIT_WITH_RESULT();
}
