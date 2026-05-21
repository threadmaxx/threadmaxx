/// @file Telemetry.cpp
/// §3.7 batch 14 — non-header-only telemetry sinks + FrameBudgetWatcher.
///
/// `ITraceSink` itself is just a virtual interface; the work lives in
/// `FileTraceSink` (file-backed Chrome trace with rotation) and
/// `HudTraceSink` (seqlock-protected latest-snapshot for HUDs).

#include "threadmaxx/Telemetry.hpp"

#include "threadmaxx/Engine.hpp"
#include "threadmaxx/EventChannel.hpp"
#include "threadmaxx/Stats.hpp"

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// ThreadSanitizer note: `HudTraceSink` is a classic seqlock. The
// write of `data_` AND the read of `data_` deliberately race —
// detection of the torn read is the seqlock's correctness contract
// (the reader checks `seq_` before vs after and discards mismatched
// reads). TSAN cannot model this pattern; the build ships
// `cmake/tsan.supp` with a suppression for `HudTraceSink::onFrame`
// and `HudTraceSink::tryGet`. Activate via `TSAN_OPTIONS=
// suppressions=<path>/cmake/tsan.supp`. See the suppression file
// for the rationale and prior-art links.

namespace threadmaxx {

// ---------- FileTraceSink ------------------------------------------------

// §3.9.5 batch 20 — owned copy of a FrameSnapshot. `FrameSnapshot::systems`
// is a borrowed span; for off-thread consumption we materialize the
// systems into a `std::vector` so the writer thread can outlive the
// sim-thread span.
struct OwnedFrameSnapshot {
    EngineStats              engine;
    std::vector<SystemStats> systems;
    JobSystemStats           jobs;

    FrameSnapshot view() const noexcept {
        return FrameSnapshot{engine, std::span<const SystemStats>(systems), jobs};
    }
};

struct FileTraceSink::Impl {
    std::ofstream                      out;
    std::unique_ptr<ChromeTraceWriter> writer;

    // §3.9.5 batch 20 — async writer plumbing. `worker` is empty in
    // sync mode; spawned by `setAsync(true)`. `queue` is producer→writer
    // FIFO; mutex/cv guard the boundary. The default-constructed state
    // is sync mode = legacy behavior.
    std::mutex                         queueMtx;
    std::condition_variable            queueCv;
    std::deque<OwnedFrameSnapshot>     queue;
    bool                               stopRequested = false;
    std::thread                        worker;
};

namespace {

std::string expandPath(const std::string& tmpl, std::uint32_t index) {
    auto pos = tmpl.find("%N");
    if (pos == std::string::npos) {
        return tmpl + "." + std::to_string(index) + ".json";
    }
    return tmpl.substr(0, pos) + std::to_string(index) +
           tmpl.substr(pos + 2);
}

} // namespace

FileTraceSink::FileTraceSink(Config cfg)
    : impl_(std::make_unique<Impl>()), cfg_(std::move(cfg)) {
    const auto path = expandPath(cfg_.pathTemplate, rotationIndex_);
    impl_->out.open(path, std::ios::binary | std::ios::trunc);
    impl_->writer = std::make_unique<ChromeTraceWriter>(impl_->out);
    bytesCurrent_ = 1; // the opening '[' written by ChromeTraceWriter ctor
}

FileTraceSink::~FileTraceSink() {
    if (!impl_) return;
    // §3.9.5 batch 20 — if the async writer thread is still running,
    // stop it and drain any pending work before closing the file.
    if (impl_->worker.joinable()) {
        setAsync(false);
    }
    if (impl_->writer) {
        impl_->writer.reset();   // writes closing ']'
        impl_->out.close();
    }
}

namespace {

OwnedFrameSnapshot copySnapshot(const FrameSnapshot& snap) {
    OwnedFrameSnapshot copy;
    copy.engine  = snap.engine;
    copy.systems.assign(snap.systems.begin(), snap.systems.end());
    copy.jobs    = snap.jobs;
    return copy;
}

} // namespace

void FileTraceSink::writeSyncLocked(const FrameSnapshot& snap) {
    if (!impl_ || !impl_->writer) return;

    const auto before = impl_->out.tellp();
    impl_->writer->emit(snap);
    impl_->out.flush();
    const auto after = impl_->out.tellp();
    if (after >= 0 && before >= 0) {
        bytesCurrent_ = static_cast<std::size_t>(after);
    }

    if (cfg_.rotationBytes > 0 && bytesCurrent_ >= cfg_.rotationBytes) {
        // Rotate: close current file (writer dtor adds ']'), open next.
        impl_->writer.reset();
        impl_->out.close();
        ++rotationIndex_;
        const auto path = expandPath(cfg_.pathTemplate, rotationIndex_);
        impl_->out.open(path, std::ios::binary | std::ios::trunc);
        impl_->writer = std::make_unique<ChromeTraceWriter>(impl_->out);
        bytesCurrent_ = 1;
    }
}

void FileTraceSink::onFrame(const FrameSnapshot& snap) {
    if (!impl_) return;
    if (impl_->worker.joinable()) {
        // §3.9.5 batch 20 — async mode. Copy the snapshot's borrowed
        // span into owned storage and enqueue; the writer thread does
        // the file I/O. Producer side is one short critical section
        // plus a notify; cheap to call from the sim thread.
        OwnedFrameSnapshot copy = copySnapshot(snap);
        {
            std::lock_guard<std::mutex> lk(impl_->queueMtx);
            impl_->queue.push_back(std::move(copy));
        }
        impl_->queueCv.notify_one();
        return;
    }
    writeSyncLocked(snap);
}

void FileTraceSink::setAsync(bool enable) {
    if (!impl_) return;
    const bool currentlyAsync = impl_->worker.joinable();
    if (enable == currentlyAsync) return;

    if (enable) {
        impl_->stopRequested = false;
        impl_->worker = std::thread([this] {
            for (;;) {
                OwnedFrameSnapshot job;
                {
                    std::unique_lock<std::mutex> lk(impl_->queueMtx);
                    impl_->queueCv.wait(lk, [&] {
                        return impl_->stopRequested || !impl_->queue.empty();
                    });
                    if (impl_->queue.empty()) {
                        if (impl_->stopRequested) return;
                        continue;
                    }
                    job = std::move(impl_->queue.front());
                    impl_->queue.pop_front();
                }
                // Outside the lock — file I/O is the long pole.
                writeSyncLocked(job.view());
            }
        });
    } else {
        {
            std::lock_guard<std::mutex> lk(impl_->queueMtx);
            impl_->stopRequested = true;
        }
        impl_->queueCv.notify_all();
        impl_->worker.join();
        // Drain any remaining queued frames synchronously so the user
        // sees a clean handoff back to sync mode.
        while (!impl_->queue.empty()) {
            auto job = std::move(impl_->queue.front());
            impl_->queue.pop_front();
            writeSyncLocked(job.view());
        }
    }
}

bool FileTraceSink::isAsync() const noexcept {
    return impl_ && impl_->worker.joinable();
}

void FileTraceSink::onShutdown() {
    if (!impl_) return;
    // Stop the async writer (if any) and drain any pending work
    // synchronously before tearing down the file handle.
    if (impl_->worker.joinable()) {
        setAsync(false);
    }
    if (impl_->writer) {
        impl_->writer.reset();
        impl_->out.close();
    }
}

// ---------- HudTraceSink -------------------------------------------------

void HudTraceSink::onFrame(const FrameSnapshot& snap) {
    LatestTelemetry t{};
    t.tick                       = snap.engine.tick;
    t.lastStepSeconds            = snap.engine.lastStepSeconds;
    t.avgStepSeconds             = snap.engine.avgStepSeconds;
    t.commitDurationSecs         = snap.engine.commitDurationSeconds;
    t.jobsSubmittedLastStep      = snap.engine.jobsSubmittedLastStep;
    t.commandsCommittedLastStep  = snap.engine.commandsCommittedLastStep;
    t.aliveEntities              = snap.engine.aliveEntities;
    t.commitHash                 = snap.engine.commitHash;
    t.workerCount                = snap.jobs.workerCount;
    t.totalJobs                  = snap.engine.totalJobsSubmitted;
    t.totalCommands              = snap.engine.totalCommandsCommitted;

    // Seqlock write: bump to odd, write, bump to even.
    const std::uint32_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_release);
    data_ = t;
    seq_.store(s + 2, std::memory_order_release);
}

bool HudTraceSink::tryGet(LatestTelemetry& out) const noexcept {
    for (int attempt = 0; attempt < 16; ++attempt) {
        const std::uint32_t s1 = seq_.load(std::memory_order_acquire);
        if (s1 == 0) return false;            // never written
        if (s1 & 1u) continue;                // writer in progress
        out = data_;
        const std::uint32_t s2 = seq_.load(std::memory_order_acquire);
        if (s1 == s2) return true;
    }
    return false;
}

// ---------- FrameBudgetWatcher -------------------------------------------

FrameBudgetWatcher::FrameBudgetWatcher(Engine* engine, double targetSeconds)
    : engine_(engine),
      target_(targetSeconds > 0.0 ? targetSeconds : 0.0) {}

void FrameBudgetWatcher::postStep(SystemContext&) {
    if (!engine_ || target_ <= 0.0) return;
    const auto stats = engine_->stats();
    if (stats.lastStepSeconds <= target_) return;
    ++exceedCount_;
    engine_->events<BudgetExceeded>().emit(BudgetExceeded{
        stats.tick,
        stats.lastStepSeconds,
        target_,
    });
}

} // namespace threadmaxx
