#pragma once

#include <cstdint>

namespace boids {

// Window dimensions also serve as the 2D world bounds. Positions wrap at the
// edges. We use Transform::position.x for screen X and .z for screen Y to
// match the existing 2D-on-3D convention (Y is "up", unused for visuals).
constexpr float kWindowW = 1024.0f;
constexpr float kWindowH = 768.0f;

constexpr std::uint32_t kBoidCount = 256;

constexpr float kPerceptionRadius = 60.0f;
constexpr float kSeparationRadius = 22.0f;

constexpr float kMaxSpeed = 120.0f;   // pixels per second
constexpr float kMaxForce = 250.0f;   // pixels per second^2

constexpr float kAlignWeight      = 1.20f;
constexpr float kCohesionWeight   = 0.90f;
constexpr float kSeparationWeight = 1.80f;

} // namespace boids
