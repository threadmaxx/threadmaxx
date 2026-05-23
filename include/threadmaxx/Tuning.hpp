#pragma once

#include "Stats.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <istream>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace threadmaxx {

/// ADAPTIVE_TUNING.md T4 — one `(systemName, preferredGrain)` entry in a
/// @ref TuningPatch. The engine resolves @c systemName against the
/// registered system list at patch-apply time (case-sensitive byte
/// comparison against @ref ISystem::name); unknown names are silently
/// ignored (logged at @c Warn level via @ref ILogger).
///
/// The override replaces the system's @ref ISystem::preferredGrain
/// return value for subsequent ticks until a later patch overrides it
/// again. The previous value is not restored when the policy is
/// detached — call @ref Engine::setTuningPolicy with `nullptr` and
/// then either re-register the affected systems or apply a patch
/// that explicitly resets the grain to the original value.
struct SystemGrainOverride {
    /// System name, matched against @ref ISystem::name. Owned-string so
    /// the patch survives the lifetime of any borrowed source.
    std::string   systemName;

    /// New preferred grain. Same semantics as
    /// @ref ISystem::preferredGrain: a non-zero value pins the per-system
    /// default chunk size when a `parallelFor` call passes `grain == 0`;
    /// caller-supplied `grain != 0` is unaffected. `0` resets the system
    /// to the engine's automatic 4-chunks-per-worker heuristic.
    std::uint32_t preferredGrain = 0;
};

/// ADAPTIVE_TUNING.md T4 — a bundle of per-system overrides produced by
/// an @ref ITuningPolicy::propose call and applied by the engine at the
/// next tick boundary (before any @ref ISystem::preStep hook runs).
///
/// Mid-wave application is forbidden by the engine — the patch is
/// staged inside the engine's internal state and visible to the next
/// tick's `update`, `preStep`, and `postStep` calls.
///
struct TuningPatch {
    /// Grain overrides applied in vector order. If two entries name the
    /// same system, the last one wins.
    std::vector<SystemGrainOverride> grainOverrides;
};

/// ADAPTIVE_TUNING.md T6 — runtime mode for the adaptive tuner. The
/// engine consults this flag once per @ref Engine::step in the
/// post-commit callback block.
///
/// @c Off is the default and matches v1.3 behaviour exactly (the
/// tuning subsystem is a single null-pointer check per step).
///
/// @c Active runs the installed policy: @ref ITuningPolicy::observe
/// and @ref ITuningPolicy::propose fire as in T4/T5. If a
/// @ref TuningTrace is also attached via @ref Engine::setTuningTrace,
/// every applied patch is recorded into it keyed by the proposing
/// tick (= @ref EngineStats::tick at the moment of the call).
///
/// @c Scripted ignores @ref ITuningPolicy::propose entirely; staged
/// patches are read from the attached @ref TuningTrace using
/// @ref TuningTrace::tryGet keyed by the same tick number. This is
/// the determinism handle: server records in Active, every client
/// replays the same stream in Scripted, and per-tick `commitHash`
/// matches bit-for-bit across the cluster.
enum class TuningMode {
    Off,
    Active,
    Scripted,
};

/// ADAPTIVE_TUNING.md T6 — recorded sequence of @ref TuningPatch
/// values keyed by the tick on which the engine applied them. Used
/// both as a record sink (in @ref TuningMode::Active) and as a
/// replay source (in @ref TuningMode::Scripted).
///
/// @par Storage
///      Entries are kept sorted by tick ascending. Inserting an
///      entry with a tick that is less than or equal to the last
///      entry's tick is supported but linear (small constant under
///      sane policy cadence — `AdaptiveGrainPolicy` produces at most
///      `1 / cooldownTicks` entries per tick per system).
///
/// @par Serialization
///      `[magic 'TUNE' u32][version u32][entryCount u64]` followed by
///      `entryCount` records of
///      `[tick u64][overrideCount u64]{[nameLen u64][nameBytes][grain u32]}*`.
///      Host endian; not portable across endianness boundaries (same
///      caveat as @ref WorldSnapshot).
///
/// @par Thread safety
///      Not thread-safe. Engine touches it only on the simulation
///      thread inside @ref Engine::step. Host code that wants to
///      mutate or read concurrently must externally synchronize.
class TuningTrace {
public:
    /// Append a patch keyed by @p tick. If @p tick is less than or
    /// equal to the last recorded tick, the entry is inserted in
    /// sorted order (the common case is monotonically increasing
    /// ticks, which is O(1) per call).
    void record(std::uint64_t tick, const TuningPatch& p) {
        if (entries_.empty() || tick > entries_.back().tick) {
            entries_.push_back({tick, p});
            return;
        }
        auto it = std::lower_bound(
            entries_.begin(), entries_.end(), tick,
            [](const Entry& e, std::uint64_t t) { return e.tick < t; });
        entries_.insert(it, Entry{tick, p});
    }

    /// Look up the patch recorded for @p tick. Returns @c true and
    /// fills @p out on hit; returns @c false otherwise (no patch
    /// recorded for that tick).
    bool tryGet(std::uint64_t tick, TuningPatch& out) const {
        auto it = std::lower_bound(
            entries_.begin(), entries_.end(), tick,
            [](const Entry& e, std::uint64_t t) { return e.tick < t; });
        if (it == entries_.end() || it->tick != tick) return false;
        out = it->patch;
        return true;
    }

    /// Number of recorded patches.
    std::size_t size() const noexcept { return entries_.size(); }

    /// Whether any patches have been recorded.
    bool empty() const noexcept { return entries_.empty(); }

    /// Drop all recorded patches.
    void clear() noexcept { entries_.clear(); }

    /// Wire-format magic constant — `'TUNE'` little-endian.
    static constexpr std::uint32_t kMagic   = 0x454E5554u;

    /// Wire-format version. Bump when the serialized layout changes.
    static constexpr std::uint32_t kVersion = 1u;

    /// Serialize to @p os in the format described in the class
    /// documentation. Always succeeds (the stream's badbit is the
    /// caller's responsibility).
    void serialize(std::ostream& os) const {
        detail_writePod(os, kMagic);
        detail_writePod(os, kVersion);
        const std::uint64_t n = entries_.size();
        detail_writePod(os, n);
        for (const auto& e : entries_) {
            detail_writePod(os, e.tick);
            const std::uint64_t m = e.patch.grainOverrides.size();
            detail_writePod(os, m);
            for (const auto& ov : e.patch.grainOverrides) {
                const std::uint64_t nameLen = ov.systemName.size();
                detail_writePod(os, nameLen);
                if (nameLen != 0) {
                    os.write(ov.systemName.data(),
                             static_cast<std::streamsize>(nameLen));
                }
                detail_writePod(os, ov.preferredGrain);
            }
        }
    }

    /// Restore a trace from @p is. Returns @c false if the stream is
    /// short, the magic is wrong, or the version is unknown. On
    /// failure the returned trace is empty.
    static TuningTrace deserialize(std::istream& is) {
        TuningTrace out;
        std::uint32_t magic   = 0;
        std::uint32_t version = 0;
        std::uint64_t n       = 0;
        if (!detail_readPod(is, magic))   return out;
        if (magic != kMagic)              return out;
        if (!detail_readPod(is, version)) return out;
        if (version != kVersion)          return out;
        if (!detail_readPod(is, n))       return out;
        out.entries_.reserve(static_cast<std::size_t>(n));
        for (std::uint64_t i = 0; i < n; ++i) {
            Entry e;
            std::uint64_t m = 0;
            if (!detail_readPod(is, e.tick)) { out.entries_.clear(); return out; }
            if (!detail_readPod(is, m))      { out.entries_.clear(); return out; }
            e.patch.grainOverrides.resize(static_cast<std::size_t>(m));
            for (auto& ov : e.patch.grainOverrides) {
                std::uint64_t nameLen = 0;
                if (!detail_readPod(is, nameLen)) {
                    out.entries_.clear(); return out;
                }
                ov.systemName.resize(static_cast<std::size_t>(nameLen));
                if (nameLen != 0) {
                    is.read(ov.systemName.data(),
                            static_cast<std::streamsize>(nameLen));
                    if (!is) { out.entries_.clear(); return out; }
                }
                if (!detail_readPod(is, ov.preferredGrain)) {
                    out.entries_.clear(); return out;
                }
            }
            out.entries_.push_back(std::move(e));
        }
        return out;
    }

private:
    struct Entry {
        std::uint64_t tick = 0;
        TuningPatch   patch;
    };
    std::vector<Entry> entries_;

    template <typename T>
    static void detail_writePod(std::ostream& os, const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        os.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }
    template <typename T>
    static bool detail_readPod(std::istream& is, T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        is.read(reinterpret_cast<char*>(&v), sizeof(T));
        return static_cast<bool>(is);
    }
};

/// ADAPTIVE_TUNING.md T4 — policy interface for the adaptive runtime
/// tuner. The engine calls @ref observe once per @ref Engine::step
/// AFTER the commit phase and BEFORE the next tick begins, then
/// @ref propose once per step; if the latter returns a patch, the
/// engine stages it and applies it at the next tick boundary before
/// any @ref ISystem::preStep hook runs.
///
/// @par Determinism contract
///      The same input stream + same scripted policy stream produces
///      bit-identical `EngineStats::commitHash` streams. A policy is
///      free to read from @ref EngineStats, @ref SystemStats, and
///      @ref JobSystemStats; reading from any other side channel
///      (wall-clock, thread-local RNG, environment) breaks
///      determinism. The Scripted-replay mechanism in
///      @ref TuningMode::Scripted bypasses @ref propose entirely and
///      pulls patches from a recorded @ref TuningTrace, sidestepping
///      side-channel reads at the cost of a one-time recording pass.
///
/// @par Lifetime
///      The engine never owns the policy. @ref Engine::setTuningPolicy
///      borrows the pointer; the host owns lifetime. Pass `nullptr`
///      to detach (the default state).
///
/// @par Thread safety
///      Both methods are invoked on the simulation thread inside
///      @ref Engine::step. Worker jobs never see the policy. Policies
///      that maintain internal state may use plain (non-atomic)
///      members.
class ITuningPolicy {
public:
    virtual ~ITuningPolicy() = default;

    /// Engine hand-off of the just-completed step's instrumentation.
    /// Invoked once per @ref Engine::step, AFTER the commit phase
    /// (final stats published, render-frame submitted, event channels
    /// drained) and BEFORE the next tick begins. The spans into
    /// engine-owned storage are valid only for the duration of the
    /// call — copy any retained fields.
    ///
    /// @param engine  Snapshot of @ref EngineStats for the just-finished tick.
    /// @param systems Per-system stats in registration order.
    /// @param jobs    Worker-pool aggregate stats.
    virtual void observe(const EngineStats&            engine,
                         std::span<const SystemStats>  systems,
                         const JobSystemStats&         jobs) = 0;

    /// Optional patch to apply before the next tick. Return `std::nullopt`
    /// (or default-constructed) to leave engine knobs untouched. The
    /// patch is staged when this method returns and applied at the
    /// next tick boundary before @ref ISystem::preStep — the engine
    /// never applies a patch mid-wave.
    virtual std::optional<TuningPatch> propose() = 0;
};

} // namespace threadmaxx
