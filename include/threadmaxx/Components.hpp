#pragma once

#include <cstdint>

namespace threadmaxx {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

struct Transform {
    Vec3 position;
    Quat orientation;
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct Velocity {
    Vec3 linear;
    Vec3 angular;
};

// Lightweight tag the renderer keys off of. A negative meshId means the
// entity is not renderable.
struct RenderTag {
    std::int32_t meshId = -1;
    std::int32_t materialId = -1;
    std::uint32_t flags = 0;
};

// User-controlled 64 bits per entity. Useful for AI state, faction IDs, etc.
// The engine never interprets this value.
struct UserData {
    std::uint64_t value = 0;
};

} // namespace threadmaxx
