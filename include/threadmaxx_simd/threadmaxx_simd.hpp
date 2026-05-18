// threadmaxx_simd — umbrella include.
//
// Pulls in every public header. Use this from consumer code when
// you don't care about minimizing compile time; otherwise include
// the specific kernel headers (`vec3_ops.hpp` etc.) directly.
//
// See `doc/USER_GUIDE.md` for usage and integration patterns;
// `doc/MAINTAINER_GUIDE.md` for the implementation architecture
// + how to add new kernels / backends.

#pragma once

#include "aabb_ops.hpp"
#include "config.hpp"
#include "cpu.hpp"          // runtime CPU probe (S5)
#include "quat_ops.hpp"
#include "simd_math.hpp"
#include "traits.hpp"
#include "transform_ops.hpp"
#include "vec3_ops.hpp"
#include "version.hpp"      // library version macros + version_string()
#include "views.hpp"
