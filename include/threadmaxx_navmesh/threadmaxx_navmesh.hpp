#pragma once

/// `threadmaxx_navmesh` — navmesh + pathfinding sibling library.
///
/// This umbrella header pulls in the public surface that has shipped
/// so far. See `DESIGN_NOTES.md` for the full spec and
/// `FUTURE_WORK.md` for the per-batch roadmap.
///
/// The library reuses the engine's `Vec3` / `Quat` PODs but is
/// otherwise standalone — no engine-internal headers are required to
/// consume it.

#include "threadmaxx_navmesh/config.hpp"
#include "threadmaxx_navmesh/mesh.hpp"
#include "threadmaxx_navmesh/types.hpp"
