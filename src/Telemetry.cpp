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

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

namespace threadmaxx {

// ---------- FileTraceSink ------------------------------------------------

struct FileTraceSink::Impl {
    std::ofstream         out;
    std::unique_ptr<ChromeTraceWriter> writer;
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
    if (impl_ && impl_->writer) {
        impl_->writer.reset();   // writes closing ']'
        impl_->out.close();
    }
}

void FileTraceSink::onFrame(const FrameSnapshot& snap) {
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

void FileTraceSink::onShutdown() {
    if (impl_ && impl_->writer) {
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
