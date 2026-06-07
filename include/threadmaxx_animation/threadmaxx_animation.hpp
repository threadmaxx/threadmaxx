#pragma once

/// Umbrella include for the threadmaxx_animation sibling library.
/// Pull in this header alone to get the v1.0 public surface.
///
/// Detail-namespace internals (`detail/`) are not part of the public
/// contract and may churn between batches; consumers should not
/// include them directly.

#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/pose.hpp"
#include "threadmaxx_animation/registry.hpp"
#include "threadmaxx_animation/skeleton.hpp"
#include "threadmaxx_animation/types.hpp"
