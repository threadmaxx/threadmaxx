#pragma once

/// @file data_source.hpp
/// @brief `IStudioDataSource` — the read / mutate abstraction every
/// studio panel goes through.
///
/// Two concrete impls land later: `DirectDataSource` (ST4, in-process
/// Shape A) and `RemoteDataSource` (M7, out-of-process Shape B). The
/// interface returns studio-owned mirror PODs rather than engine /
/// sibling types directly, so this header stays free of every
/// `threadmaxx*` dependency and the same panel binaries serve both
/// attach modes.
///
/// Missing or not-yet-cached values return `std::nullopt`; panels
/// MUST treat that as a graceful-degradation signal and render a
/// placeholder rather than crash.

#include <cstdint>
#include <optional>
#include <string_view>

namespace threadmaxx::studio {

/// @brief How the studio is attached to a running engine.
enum class AttachMode : std::uint8_t {
    /// In-process: the studio runs in the same address space as the
    /// engine. `DirectDataSource` is the Shape A impl.
    Direct = 0,

    /// Out-of-process: the studio runs in a different process and
    /// reaches the engine over the editor v1.2 remote wire.
    /// `RemoteDataSource` is the Shape B impl (M7).
    Remote = 1,
};

/// @brief Studio-owned summary of the engine's per-tick frame state.
///
/// Mirrors a subset of `threadmaxx::FrameSnapshot` chosen to keep this
/// header free of any core dependency. `DirectDataSource` populates
/// from `Engine::frameSnapshot()`; `RemoteDataSource` decodes from
/// the wire. v1.x adds more fields as panels need them.
struct EngineFrameSummary {
    /// Monotonically-increasing tick at the moment the snapshot was
    /// taken.
    std::uint64_t tick = 0;
    /// Wall-clock seconds the most recent step consumed.
    double lastStepSeconds = 0.0;
    /// `true` when the engine's `setPaused(true)` is in effect.
    bool paused = false;
    /// Number of registered systems.
    std::uint32_t systemCount = 0;
    /// Number of worker threads in the engine's job system.
    std::uint32_t workerCount = 0;
};

/// @brief Tag PODs for sibling-stat accessors. Each grows with its
/// owning panel batch (M4–M8). Empty here so the canary compiles
/// without pulling any sibling header.
struct AnimationStatsView {};
struct AudioStatsView {};
struct InputStatsView {};
struct AssetsStatsView {};
struct UiStatsView {};
struct NavmeshStatsView {};
struct PhysicsStatsView {};
struct ReflectStatsView {};
struct NetworkStatsView {};
struct MigrationStatsView {};

/// @brief Read / mutate abstraction every studio panel uses.
///
/// Every accessor defaults to `std::nullopt`. Concrete impls override
/// only the accessors backed by a sibling that's actually present
/// in the build; the framework treats `nullopt` as "not available,
/// render the placeholder."
///
/// Mutations funnel through `submitCommand()`, which routes into
/// `editor::CommandStack` so undo / redo / replay all work the same
/// across attach modes.
///
/// @thread_safety Single-threaded; the studio invokes the data source
/// from the UI thread only.
class IStudioDataSource {
public:
    virtual ~IStudioDataSource() = default;

    /// @brief Which attach mode the source represents. Panels may
    /// branch on this for cache-warmup behavior.
    virtual AttachMode mode() const noexcept = 0;

    // ---- Engine surface ---------------------------------------------------

    /// @brief Latest per-frame engine summary. `nullopt` if no engine
    /// is attached.
    virtual std::optional<EngineFrameSummary> engineSnapshot() const {
        return std::nullopt;
    }

    // ---- Sibling-stat surfaces -------------------------------------------
    //
    // Each accessor returns `nullopt` from the default impl. Concrete
    // impls override the accessors backed by a sibling that's actually
    // linked into the build. M4–M8 panel batches enlarge the
    // corresponding `*StatsView` POD as they need more state.

    virtual std::optional<AnimationStatsView> animationStats() const {
        return std::nullopt;
    }
    virtual std::optional<AudioStatsView> audioStats() const {
        return std::nullopt;
    }
    virtual std::optional<InputStatsView> inputStats() const {
        return std::nullopt;
    }
    virtual std::optional<AssetsStatsView> assetsStats() const {
        return std::nullopt;
    }
    virtual std::optional<UiStatsView> uiStats() const {
        return std::nullopt;
    }
    virtual std::optional<NavmeshStatsView> navmeshStats() const {
        return std::nullopt;
    }
    virtual std::optional<PhysicsStatsView> physicsStats() const {
        return std::nullopt;
    }
    virtual std::optional<ReflectStatsView> reflectStats() const {
        return std::nullopt;
    }
    virtual std::optional<NetworkStatsView> networkStats() const {
        return std::nullopt;
    }
    virtual std::optional<MigrationStatsView> migrationStats() const {
        return std::nullopt;
    }

    // ---- Mutation --------------------------------------------------------

    /// @brief Submit a typed edit command. ST1 stub: opaque label
    /// only; ST4 wires the payload through `editor::CommandStack`.
    /// Returns `true` if the source accepted the submission.
    virtual bool submitCommand(std::string_view /*label*/) {
        return false;
    }
};

} // namespace threadmaxx::studio
