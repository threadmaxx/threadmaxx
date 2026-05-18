// threadmaxx_simd — umbrella include.
//
// Pulls in every public header. Use this from consumer code when
// you don't care about minimizing compile time; otherwise include
// the specific kernel headers (`vec3_ops.hpp` etc.) directly.

#pragma once

#include "aabb_ops.hpp"
#include "config.hpp"
#include "quat_ops.hpp"
#include "simd_math.hpp"
#include "traits.hpp"
#include "transform_ops.hpp"
#include "vec3_ops.hpp"
#include "views.hpp"
