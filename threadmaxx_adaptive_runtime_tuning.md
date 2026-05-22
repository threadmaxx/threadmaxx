# Adaptive Runtime Tuning for `threadmaxx`

## Goal

Add a small, opt-in tuning subsystem to the core that can adjust selected runtime parameters automatically while the engine is running.

The purpose is to improve throughput and frame-time stability for large worlds, without turning the engine into a self-modifying black box.

## Recommendation

Yes, but only for a limited set of knobs.

Good candidates for automatic tuning:
- `parallelFor` grain size
- per-system preferred grain
- wave splitting thresholds
- job batching / chunk grouping
- skip pressure thresholds
- cache refresh frequency
- query cache reuse heuristics

Usually not a good candidate for automatic tuning:
- worker count
- memory layout
- archetype storage strategy
- public API behavior
- commit determinism rules
- anything that changes replay results unless explicitly scripted

## Core principle

The tuning layer should be:

- **opt-in**
- **advisory-first**
- **bounded**
- **hysteresis-based**
- **easy to disable**
- **safe for deterministic modes**

The core engine should remain the source of truth.
The tuner only suggests or applies values for knobs that are already designed to be adjustable.

## What the subsystem should do

The tuner watches engine stats and proposes new runtime values based on real measurements.

Useful inputs:
- per-system `waitSeconds`
- per-system `jobDurationHistogram`
- `peakQueueDepth`
- worker steal rate
- average chunk size
- commit duration
- frame budget overruns
- stall events
- loader backlog
- event channel pressure
- task graph wave width
- system-specific `preferredGrain`

Useful outputs:
- set preferred grain for a system
- increase or decrease grain for a hot loop
- split oversized work more aggressively
- coarsen tiny work to reduce scheduling overhead
- reduce per-tick work for non-essential systems under pressure
- bias low-priority systems to larger grain
- keep expensive systems from fragmenting into too many jobs

## What it should not do

Do not let the tuner:
- change gameplay semantics
- reorder deterministic commit behavior
- silently rewrite user code intent
- change worker count on every tick
- change topology in a way that makes debugging impossible
- react to one bad frame with a large swing
- chase noise instead of trend

## Worker count policy

Do **not** auto-adjust worker count frequently during gameplay.

Reason:
- worker count changes are expensive
- they can destabilize scheduling
- they complicate determinism and profiling
- they are usually OS / deployment level decisions, not per-frame tuning decisions

Better approach:
- worker count is set at boot
- optionally re-evaluated at safe reload points
- if runtime resizing is supported, treat it as a rare administrative action, not a normal adaptive feature

## Better idea: tune grain, not workers

For most gameplay workloads, grain size is the right lever.

Why:
- it directly controls task granularity
- it affects load balancing and overhead
- it can be adjusted safely at a system level
- it does not change the engine’s core threading model

Example policy:
- if a system’s jobs are too tiny, increase grain
- if one worker is consistently overloaded, reduce grain for that system
- if a system is dominated by cache-friendly linear work, keep grain larger
- if a system has a few huge chunks, split those chunks more aggressively

## Suggested subsystem shape

### 1. Metrics source
The engine already gathers stats. Extend that with a compact runtime telemetry feed.

Example:
- `SystemStats`
- `JobSystemStats`
- frame budget watcher events
- commit hash / commit duration
- queue depth
- worker steal rate

### 2. Tuning policy interface
A policy consumes metrics and returns tuning suggestions.

```cpp
class ITuningPolicy {
public:
    virtual ~ITuningPolicy() = default;

    virtual void observe(const EngineStats& engine,
                         std::span<const SystemStats> systems,
                         const JobSystemStats& jobs) = 0;

    virtual std::optional<TuningPatch> propose() = 0;
};
````

### 3. Safe patch object

The policy does not mutate the engine directly.

```cpp
struct TuningPatch {
    std::vector<SystemGrainOverride> grainOverrides;
    std::vector<SystemPriorityOverride> priorityOverrides;
    std::vector<SystemBudgetOverride> budgetOverrides;
    bool requestWorkerRescan = false;   // rare, optional
};
```

### 4. Application point

The engine applies patches at safe boundaries:

* before a wave starts
* after a commit
* at tick boundary
* never halfway through a command commit

## Suggested tuning rules

### Grain tuning

For each system:

* measure average job duration
* measure queue overhead vs work time
* measure steal frequency
* measure tail latency
* keep a moving average over multiple ticks

Then:

* if jobs are too short, increase grain
* if one system’s wave regularly stalls on one large job, reduce grain
* if a system is stable, keep the current value
* never change by more than one step per adjustment window

### Hysteresis

Do not tune on noise.

Use:

* moving averages
* exponential smoothing
* cooldown timers
* minimum sample windows
* bounded step size

Example:

* only consider changes every 60 ticks
* require a sustained trend for 3 windows
* only apply one change at a time
* revert slowly, not instantly

### Budget-aware tuning

If the engine is under frame budget pressure:

* increase grain for tiny tasks to reduce scheduling overhead
* skip or defer skippable systems earlier
* raise priority for latency-sensitive gameplay tasks
* lower priority for analytics / telemetry / non-essential AI

### Load-shape tuning

If chunk sizes are very uneven:

* use finer splitting only for large chunks
* keep small chunks bundled
* avoid oversplitting the whole world just because one chunk is huge

## Good runtime knobs

These are good candidates to expose to the tuner:

* `preferredGrain`
* `JobPriority`
* `skippable`
* per-system time budgets
* prefetch hints
* chunk split threshold
* telemetry sampling rate
* trace sink frequency
* loader concurrency limit

## Bad runtime knobs

These should not be auto-tuned in the normal case:

* worker count
* commit ordering rules
* entity storage format
* component mask semantics
* public API shape
* replay policy
* deterministic hash rules

## Determinism mode

If the engine is running in a deterministic or networked replay mode:

* tuning must be either disabled
* or fully scripted and reproducible
* or derived from the same recorded input stream on every peer

That means:

* no “live heuristic drift”
* no machine-specific random tuning
* no adjustments based on nondeterministic timing unless the policy is explicitly part of replay data

## Debuggability

The tuner must be visible.

Add:

* current settings per system
* previous settings
* reason for each change
* timestamps or tick numbers for changes
* before/after metric snapshots
* a way to freeze tuning for comparison runs

This makes it possible to answer:

* why did grain change?
* when did the system start getting more granular?
* was this change actually useful?
* did the tuner help or hurt this scene?

## Minimal viable version

The first version should be very small:

1. collect per-system timing and queue metrics
2. compute moving averages
3. adjust only `preferredGrain`
4. do it only for systems that opt in
5. apply changes at tick boundaries
6. log every adjustment

That already gives useful gains without risking too much complexity.

## Nice later additions

After the basic version is proven:

* adaptive grain by chunk size distribution
* dynamic priority nudging under budget pressure
* automatic skippable-system biasing
* load-shape classification per system
* “freeze tuning” profiles for profiling sessions
* export tuning decisions to trace files

## Verdict

Yes, this is a good idea **if it stays small and constrained**.

The best version of this feature is not a general auto-optimizer. It is a **runtime hint controller** that:

* watches metrics
* nudges grain and priority
* avoids oscillation
* preserves determinism when needed
* stays easy to disable and inspect
