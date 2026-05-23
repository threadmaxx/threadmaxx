#pragma once

#include "Tuning.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace threadmaxx {

/// ADAPTIVE_TUNING.md T5 — built-in @ref ITuningPolicy that adjusts
/// per-system @c preferredGrain so that the EWMA of
/// @ref SystemStats::avgSubJobMicros lands inside a "hold band" around
/// @ref Config::targetSubJobMicros.
///
/// @par Heuristic
///      For each system that called @c parallelFor in the most recent
///      step (i.e. has @c subJobsLastStep > 0):
///      - if @c avgSubJobMicros < @c targetSubJobMicros / 2 →
///        coarsen (multiply grain by @c stepSize). Sub-jobs were too
///        tiny; dispatch overhead dominated.
///      - if @c avgSubJobMicros > @c targetSubJobMicros * 4 AND
///        @c peakQueueDepth >= worker count → split (divide grain by
///        @c stepSize). One sub-job stalled the wave.
///      - otherwise hold.
///
/// @par Hysteresis
///      A change in a given direction requires
///      @c minSamplesPerChange consecutive observations pointing in
///      that direction; a change of direction resets the streak. After
///      a change fires, the system is on cooldown for
///      @c cooldownTicks ticks — at most one change per
///      @c cooldownTicks per system.
///
/// @par ε-greedy exploration
///      With probability @c explorationEpsilon, the policy proposes a
///      one-step random walk (one stepSize multiplication, direction
///      drawn uniformly) for an otherwise-quiet system. This avoids
///      stuck local minima on flat hold bands. RNG is seeded
///      deterministically from @c randomSeed XOR currentTick so two
///      runs against the same workload + same Config produce identical
///      patch streams.
///
/// @par Determinism
///      The policy reads only the @ref EngineStats / @ref SystemStats /
///      @ref JobSystemStats handed in by the engine — never the
///      wall-clock or thread-local RNG. Two engines configured
///      identically + ticked against identical inputs emit identical
///      @ref TuningPatch sequences (and therefore identical
///      @ref EngineStats::commitHash streams). This makes the policy
///      safe in solo / desktop deployments; for networked / replay
///      determinism, run the authoritative server in active mode and
///      replay the recorded trace on clients (T6).
///
/// @par Thread safety
///      All methods are invoked on the simulation thread. Internal
///      state uses plain members; never accessed off-thread.
class AdaptiveGrainPolicy : public ITuningPolicy {
public:
    /// Tuning constants. Defaults track the values listed in
    /// `ADAPTIVE_TUNING.md` §T5; the empirical sweet spots for our
    /// workloads, sized to be conservative-by-default (slow to act, no
    /// oscillation).
    struct Config {
        /// Minimum gap between two consecutive grain changes on the
        /// same system. Counted in @ref EngineStats::tick.
        std::uint32_t cooldownTicks       = 60;
        /// Consecutive same-direction observations required before a
        /// non-exploratory change fires. Suppresses single-frame
        /// noise.
        std::uint32_t minSamplesPerChange = 3;
        /// Center of the hold band, in microseconds. The hold band is
        /// `[target/2, target*4]`.
        double        targetSubJobMicros  = 200.0;
        /// Probability per (tick, system) of proposing a one-step
        /// random walk when the system is otherwise quiet. Set to 0
        /// to disable exploration entirely.
        double        explorationEpsilon  = 0.05;
        /// Geometric step factor for grain adjustments. Each fired
        /// change multiplies or divides the current grain by
        /// @c stepSize.
        double        stepSize            = 1.5;
        /// Lower clamp on proposed grain. Engine treats grain=0 as
        /// "automatic"; the policy never proposes 0.
        std::uint32_t minGrain            = 1;
        /// Upper clamp on proposed grain.
        std::uint32_t maxGrain            = 65536;
        /// Initial anchor used on the first commit for a system whose
        /// previous grain we don't know. Subsequent changes step
        /// from the last proposed value.
        std::uint32_t initialGrain        = 64;
        /// Deterministic seed for the ε-greedy RNG. XOR'd with the
        /// current tick on each `propose()` to vary the random draw
        /// while keeping the stream reproducible across runs.
        std::uint64_t randomSeed          = 0xA17EA17E2026ull;
    };

    AdaptiveGrainPolicy() = default;
    explicit AdaptiveGrainPolicy(Config c) : cfg_(c) {}

    /// @copydoc ITuningPolicy::observe
    void observe(const EngineStats&            engine,
                 std::span<const SystemStats>  systems,
                 const JobSystemStats&         jobs) override {
        currentTick_ = engine.tick;
        workerCount_ = jobs.workerCount;
        decisions_.clear();
        for (const auto& s : systems) {
            if (s.name == nullptr || s.subJobsLastStep == 0) continue;
            const std::string key{s.name};
            State& st = states_[key];

            const double mic   = s.avgSubJobMicros;
            const double lower = cfg_.targetSubJobMicros * 0.5;
            const double upper = cfg_.targetSubJobMicros * 4.0;

            int dir = 0;
            if (mic > 0.0 && mic < lower) {
                dir = +1; // sub-job too small → coarsen
            } else if (mic > upper && s.peakQueueDepth >= workerCount_) {
                dir = -1; // sub-job too large + queue saturated → split
            }

            if (dir != 0 && dir == st.lastDirection) {
                st.streak += 1;
            } else {
                st.streak = (dir != 0) ? 1u : 0u;
            }
            st.lastDirection    = dir;
            st.lastSubJobMicros = mic;

            decisions_.push_back({key, dir});
        }
    }

    /// @copydoc ITuningPolicy::propose
    std::optional<TuningPatch> propose() override {
        TuningPatch patch;

        std::mt19937_64 rng(cfg_.randomSeed ^ currentTick_);
        std::uniform_real_distribution<double> uni(0.0, 1.0);

        for (const auto& d : decisions_) {
            auto it = states_.find(d.name);
            if (it == states_.end()) continue;
            State& st = it->second;

            const bool cooldownOk =
                !st.everChanged ||
                currentTick_ - st.lastChangeTick >= cfg_.cooldownTicks;

            const bool streakOk = st.streak >= cfg_.minSamplesPerChange;

            bool fired = false;
            if (cooldownOk && streakOk && d.direction != 0) {
                const std::uint32_t base = st.everChanged
                    ? st.currentGrain : cfg_.initialGrain;
                const double f = (d.direction > 0)
                    ? cfg_.stepSize : (1.0 / cfg_.stepSize);
                const std::uint32_t next = clamp_(
                    static_cast<std::uint32_t>(
                        std::lround(static_cast<double>(base) * f)));
                if (next != st.currentGrain || !st.everChanged) {
                    patch.grainOverrides.push_back({d.name, next});
                    st.currentGrain   = next;
                    st.lastChangeTick = currentTick_;
                    st.everChanged    = true;
                    st.streak         = 0;
                    fired             = true;
                }
            }

            // ε-greedy exploration: cooldown-respecting random one-step
            // walk on otherwise-quiet systems. Drawing two doubles even
            // when we won't fire keeps the RNG-consume count stable per
            // system per tick, which preserves determinism across
            // refactors of the early-out path.
            const double draw      = uni(rng);
            const double direction = uni(rng);
            if (!fired && cooldownOk && draw < cfg_.explorationEpsilon) {
                const std::uint32_t base = st.everChanged
                    ? st.currentGrain : cfg_.initialGrain;
                const double f = (direction < 0.5)
                    ? (1.0 / cfg_.stepSize) : cfg_.stepSize;
                const std::uint32_t next = clamp_(
                    static_cast<std::uint32_t>(
                        std::lround(static_cast<double>(base) * f)));
                if (next != st.currentGrain || !st.everChanged) {
                    patch.grainOverrides.push_back({d.name, next});
                    st.currentGrain   = next;
                    st.lastChangeTick = currentTick_;
                    st.everChanged    = true;
                    st.streak         = 0;
                }
            }
        }

        if (patch.grainOverrides.empty()) return std::nullopt;
        return patch;
    }

    /// Latest grain the policy has proposed for the named system, or
    /// `std::nullopt` if the policy has never fired on it. Inspection
    /// helper for tests and HUDs; never read by the engine.
    std::optional<std::uint32_t>
    lastAppliedGrain(std::string_view name) const {
        auto it = states_.find(std::string{name});
        if (it == states_.end() || !it->second.everChanged) {
            return std::nullopt;
        }
        return it->second.currentGrain;
    }

    /// Tick at which the policy last committed a grain change for
    /// the named system, or 0 if it has never fired on it.
    std::uint64_t lastChangeTick(std::string_view name) const {
        auto it = states_.find(std::string{name});
        if (it == states_.end()) return 0;
        return it->second.lastChangeTick;
    }

    /// Active config. Mutable accessors are deliberately not exposed —
    /// pass a new policy if you need different constants.
    const Config& config() const noexcept { return cfg_; }

private:
    struct State {
        std::uint32_t currentGrain     = 0;
        std::uint64_t lastChangeTick   = 0;
        std::uint32_t streak           = 0;
        int           lastDirection    = 0;
        double        lastSubJobMicros = 0.0;
        bool          everChanged      = false;
    };

    struct Decision {
        std::string name;
        int         direction;
    };

    std::uint32_t clamp_(std::uint32_t g) const noexcept {
        if (g < cfg_.minGrain) return cfg_.minGrain;
        if (g > cfg_.maxGrain) return cfg_.maxGrain;
        return g;
    }

    Config cfg_;
    std::unordered_map<std::string, State> states_;
    std::vector<Decision>                  decisions_;
    std::uint64_t                          currentTick_ = 0;
    std::uint32_t                          workerCount_ = 0;
};

} // namespace threadmaxx
