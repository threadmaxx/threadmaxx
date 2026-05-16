// §3.9.1 batch 16 — Shared benchmark utilities.
//
// Header-only; no dependency beyond the C++20 standard library and
// the threadmaxx public headers. Every §3.9 benchmark binary uses
// this file so output is directly comparable across runs (same
// percentile definition, same CSV column order, same warmup
// convention).
//
// Three primitives:
//   - `Stopwatch`         — RAII / start-stop wall-clock timer.
//   - `LatencyHistogram`  — collects ns samples, reports mean / p50
//                           / p95 / p99 / stddev. Sort-on-finalize
//                           (accurate for our sample sizes).
//   - `CsvWriter`         — header + row append, optional stdout
//                           echo when no path is given.
//
// All values are reported in nanoseconds internally; per-bench
// formatting decides on us / ms.

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace threadmaxx_bench {

class Stopwatch {
public:
    void start() noexcept { t0_ = clock::now(); }
    /// Returns elapsed ns since the last @ref start. Does not stop the
    /// timer — call again to take another snapshot from the same start.
    std::uint64_t elapsedNs() const noexcept {
        const auto dt = clock::now() - t0_;
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
    }
private:
    using clock = std::chrono::steady_clock;
    clock::time_point t0_{};
};

/// Sample collector. Push ns values; call @ref finalize before
/// reading the summary fields.
class LatencyHistogram {
public:
    void reserve(std::size_t n) { samples_.reserve(n); }
    void push(std::uint64_t ns) { samples_.push_back(ns); }

    /// Sort once, populate mean / stddev / percentiles. Idempotent.
    void finalize() {
        if (finalized_) return;
        finalized_ = true;
        n_ = samples_.size();
        if (n_ == 0) return;
        std::sort(samples_.begin(), samples_.end());
        std::uint64_t sum = 0;
        for (auto v : samples_) sum += v;
        mean_ = static_cast<double>(sum) / static_cast<double>(n_);
        long double accum = 0.0L;
        for (auto v : samples_) {
            const long double d = static_cast<long double>(v) - mean_;
            accum += d * d;
        }
        stddev_ = static_cast<double>(std::sqrt(accum / static_cast<long double>(n_)));
        p50_ = pctl(0.50);
        p95_ = pctl(0.95);
        p99_ = pctl(0.99);
        min_ = samples_.front();
        max_ = samples_.back();
    }

    std::size_t   samples() const noexcept { return n_; }
    double        meanNs()  const noexcept { return mean_; }
    double        stddev()  const noexcept { return stddev_; }
    std::uint64_t p50Ns()   const noexcept { return p50_; }
    std::uint64_t p95Ns()   const noexcept { return p95_; }
    std::uint64_t p99Ns()   const noexcept { return p99_; }
    std::uint64_t minNs()   const noexcept { return min_; }
    std::uint64_t maxNs()   const noexcept { return max_; }

private:
    std::uint64_t pctl(double q) const {
        if (n_ == 0) return 0;
        const std::size_t idx = static_cast<std::size_t>(
            static_cast<double>(n_ - 1) * q);
        return samples_[idx];
    }

    std::vector<std::uint64_t> samples_;
    bool          finalized_ = false;
    std::size_t   n_         = 0;
    double        mean_      = 0.0;
    double        stddev_    = 0.0;
    std::uint64_t p50_       = 0;
    std::uint64_t p95_       = 0;
    std::uint64_t p99_       = 0;
    std::uint64_t min_       = 0;
    std::uint64_t max_       = 0;
};

/// One row of a benchmark report. Fields are the union of every
/// column any §3.9 bench wants to emit; unset fields print empty.
struct BenchRow {
    std::string  label;          // e.g. "forEachChunk"
    std::string  workload;       // "AI", "Render+AI", "Churn"
    std::size_t  entities = 0;   // entity count
    std::uint32_t workers = 0;
    std::uint32_t grain   = 0;
    double       mean_ns  = 0.0;
    double       stddev   = 0.0;
    std::uint64_t p50_ns  = 0;
    std::uint64_t p95_ns  = 0;
    std::uint64_t p99_ns  = 0;
    double       throughput = 0.0; // items/sec, or empty
    double       steal_pct  = -1.0; // -1 → unset
    std::string  note;           // free-form extra column
};

class CsvWriter {
public:
    /// `path == nullptr` => stdout only. Otherwise: write CSV to file
    /// AND echo to stdout so terminal output is readable too.
    explicit CsvWriter(const char* path = nullptr) {
        if (path && *path) {
            file_.open(path, std::ios::out | std::ios::trunc);
            haveFile_ = file_.is_open();
        }
        writeHeader();
    }

    void row(const BenchRow& r) {
        std::ostringstream csv;
        csv << r.label << ',' << r.workload << ',' << r.entities << ','
            << r.workers << ',' << r.grain << ','
            << r.mean_ns << ',' << r.stddev << ','
            << r.p50_ns << ',' << r.p95_ns << ',' << r.p99_ns << ','
            << r.throughput << ',';
        if (r.steal_pct >= 0.0) csv << r.steal_pct;
        csv << ',' << '"' << r.note << '"';
        const std::string s = csv.str();
        if (haveFile_) file_ << s << '\n';
        std::printf("%s\n", s.c_str());
    }

private:
    void writeHeader() {
        constexpr const char* hdr =
            "label,workload,entities,workers,grain,"
            "mean_ns,stddev_ns,p50_ns,p95_ns,p99_ns,"
            "throughput,steal_pct,note";
        if (haveFile_) file_ << hdr << '\n';
        std::printf("%s\n", hdr);
    }

    std::ofstream file_;
    bool          haveFile_ = false;
};

/// Convenience: takes a callable invoked `iters` times after `warmup`
/// dry runs; records per-iteration ns into the histogram.
template <typename Fn>
void runIters(LatencyHistogram& hist, int warmup, int iters, Fn&& fn) {
    for (int i = 0; i < warmup; ++i) fn();
    hist.reserve(static_cast<std::size_t>(iters));
    Stopwatch sw;
    for (int i = 0; i < iters; ++i) {
        sw.start();
        fn();
        hist.push(sw.elapsedNs());
    }
    hist.finalize();
}

/// Convenience for headline prints when CSV is overkill.
inline void printSummary(std::string_view label, const LatencyHistogram& h) {
    std::printf("  %-32.*s  mean=%9.2f us  p50=%9.2f  p95=%9.2f  p99=%9.2f  N=%zu\n",
                static_cast<int>(label.size()), label.data(),
                h.meanNs() / 1e3,
                static_cast<double>(h.p50Ns()) / 1e3,
                static_cast<double>(h.p95Ns()) / 1e3,
                static_cast<double>(h.p99Ns()) / 1e3,
                h.samples());
}

} // namespace threadmaxx_bench
