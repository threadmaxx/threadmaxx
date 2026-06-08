#pragma once

/// Umbrella include for the threadmaxx_physics sibling library.
/// Pull in this header alone to get the v0.1 (P1) public surface;
/// later batches extend the surface incrementally toward v1.0.
///
/// Detail-namespace internals (`detail/`) are not part of the public
/// contract and may churn between batches; consumers should not
/// include them directly.

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/body.hpp"
#include "threadmaxx_physics/config.hpp"
#include "threadmaxx_physics/shape.hpp"
#include "threadmaxx_physics/step.hpp"
#include "threadmaxx_physics/stub_backend.hpp"
#include "threadmaxx_physics/sync.hpp"
#include "threadmaxx_physics/types.hpp"
