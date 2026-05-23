/// @file Trace.cpp
/// JSON-Lines serializer for @ref threadmaxx::FrameSnapshot plus the
/// streaming Chrome Trace Event Format writer.
///
/// No allocations beyond what the supplied ostream's buffer does. The
/// JSON-Lines schema is intentionally flat — one line per tick, fields
/// named to match the stats structs. The Chrome trace writer wraps a
/// stream and emits a valid JSON array of duration events that can be
/// loaded directly in `chrome://tracing` / Perfetto.
#include "threadmaxx/Trace.hpp"

#include <cstdint>

namespace threadmaxx {

namespace {

// Escape the small set of characters JSON requires inside a string. The
// stats structs only feed us system names supplied by the game, which
// are typically string literals — escaping is for safety against names
// that happen to contain a quote or backslash, not for full Unicode
// correctness.
void writeJsonString(std::ostream& os, const char* s) {
    os << '"';
    if (s) {
        for (const char* p = s; *p; ++p) {
            const char c = *p;
            switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\b': os << "\\b";  break;
            case '\f': os << "\\f";  break;
            case '\n': os << "\\n";  break;
            case '\r': os << "\\r";  break;
            case '\t': os << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Control char — emit \u00XX.
                    static const char hex[] = "0123456789abcdef";
                    os << "\\u00"
                       << hex[(c >> 4) & 0xF]
                       << hex[c & 0xF];
                } else {
                    os << c;
                }
            }
        }
    }
    os << '"';
}

} // namespace

void writeJsonLines(std::ostream& os, const FrameSnapshot& snap) {
    const auto& e = snap.engine;
    // §3.6 batch 13a: commit_hash is the running FNV-1a-64 over every
    // applied mutation this tick. Emitted as a 16-char lowercase hex
    // string so it survives JSON parsers that mishandle 64-bit ints.
    //   layout: " 0 x H...H "  -> 1 + 2 + 16 + 1 = 20 chars
    char hashBuf[20];
    {
        static const char hex[] = "0123456789abcdef";
        hashBuf[0] = '"';
        hashBuf[1] = '0';
        hashBuf[2] = 'x';
        for (int i = 0; i < 16; ++i) {
            hashBuf[3 + i] = hex[(e.commitHash >> (60 - 4 * i)) & 0xF];
        }
        hashBuf[19] = '"';
    }
    os << "{\"tick\":" << e.tick
       << ",\"step_s\":" << e.lastStepSeconds
       << ",\"avg_step_s\":" << e.avgStepSeconds
       << ",\"commit_s\":" << e.commitDurationSeconds
       << ",\"jobs\":" << e.jobsSubmittedLastStep
       << ",\"commands\":" << e.commandsCommittedLastStep
       << ",\"alive\":" << e.aliveEntities
       << ",\"commit_hash\":";
    os.write(hashBuf, 20);
    os << ",\"systems\":[";

    for (std::size_t i = 0; i < snap.systems.size(); ++i) {
        const auto& s = snap.systems[i];
        if (i != 0) os << ',';
        os << "{\"name\":";
        writeJsonString(os, s.name);
        os << ",\"update_s\":" << s.lastUpdateSeconds
           << ",\"avg_update_s\":" << s.avgUpdateSeconds
           << ",\"jobs\":" << s.jobsSubmittedLastStep
           << ",\"commands\":" << s.commandsCommittedLastStep
           << ",\"wait_s\":" << s.waitSeconds
           << ",\"peak_queue_depth\":" << s.peakQueueDepth
           << ",\"avg_sub_job_us\":" << s.avgSubJobMicros
           << ",\"sub_jobs\":" << s.subJobsLastStep
           << "}";
    }

    const auto& j = snap.jobs;
    os << "],\"job_pool\":{"
       << "\"total_jobs\":" << j.totalJobs
       << ",\"own_pops\":" << j.ownPops
       << ",\"steals\":" << j.stolenJobs
       << ",\"workers\":" << j.workerCount
       << ",\"hist\":[";
    for (std::size_t i = 0; i < kJobDurationHistogramBins; ++i) {
        if (i != 0) os << ',';
        os << j.jobDurationHistogram[i];
    }
    os << "]}}\n";
}

// ---- ChromeTraceWriter ---------------------------------------------------

namespace {

// Stable per-system thread id derived from the system name. Chrome
// trace groups events by (pid, tid); reusing the same tid across
// snapshots stacks events on a per-system row in the UI.
std::uint32_t systemTid(const char* name) noexcept {
    // FNV-1a 32-bit. The exact hash doesn't matter; we just need a
    // stable, well-distributed integer per name string.
    std::uint32_t h = 2166136261u;
    if (name) {
        for (const char* p = name; *p; ++p) {
            h ^= static_cast<std::uint8_t>(*p);
            h *= 16777619u;
        }
    }
    // Reserve tid 0 for the "step" row; bias non-zero.
    if (h == 0) h = 1;
    return h;
}

} // namespace

ChromeTraceWriter::ChromeTraceWriter(std::ostream& os) : os_(&os) {
    (*os_) << "[\n";
}

ChromeTraceWriter::~ChromeTraceWriter() {
    if (os_) (*os_) << "\n]\n";
}

void ChromeTraceWriter::emit(const FrameSnapshot& snap) {
    if (!os_) return;
    const auto& e = snap.engine;

    // Step record (`tid:0`) frames the whole tick; system records live
    // inside it on their own tids.
    const double stepStart = cursorMicros_;
    const double stepDur   = e.lastStepSeconds * 1'000'000.0;

    // ADAPTIVE_TUNING.md T3 — extended per-system arg payload now
    // carries avg_sub_job_us and sub_jobs so a chrome://tracing
    // viewer can show the same tuning signals as JSON-lines.
    auto writeSystemEvent = [this](const SystemStats& s, double tsMicros,
                                   double durMicros, std::uint32_t tid,
                                   std::uint64_t tick) {
        if (!firstRecord_) (*os_) << ",\n";
        firstRecord_ = false;
        (*os_) << "{\"ph\":\"X\",\"name\":";
        writeJsonString(*os_, s.name ? s.name : "(unnamed)");
        (*os_) << ",\"ts\":" << tsMicros
               << ",\"dur\":" << durMicros
               << ",\"pid\":1,\"tid\":" << tid
               << ",\"args\":{\"tick\":" << tick
               << ",\"avg_sub_job_us\":" << s.avgSubJobMicros
               << ",\"sub_jobs\":" << s.subJobsLastStep
               << "}}";
    };

    // Step record (tid=0) carries the commit_hash in its args so a
    // diff between two trace files surfaces the diverging tick.
    if (!firstRecord_) (*os_) << ",\n";
    firstRecord_ = false;
    (*os_) << "{\"ph\":\"X\",\"name\":\"step\""
           << ",\"ts\":" << stepStart
           << ",\"dur\":" << stepDur
           << ",\"pid\":1,\"tid\":0"
           << ",\"args\":{\"tick\":" << e.tick
           << ",\"commit_hash\":\"0x";
    {
        static const char hex[] = "0123456789abcdef";
        for (int i = 0; i < 16; ++i) {
            (*os_) << hex[(e.commitHash >> (60 - 4 * i)) & 0xF];
        }
    }
    (*os_) << "\"}}";

    // Place system events back-to-back inside the step window. The
    // measured per-system update_s values may sum to less than step_s
    // (commit / overhead), which is fine — Chrome trace just shows the
    // gap as idle time on that row.
    double cur = stepStart;
    for (const auto& s : snap.systems) {
        const double dur = s.lastUpdateSeconds * 1'000'000.0;
        writeSystemEvent(s, cur, dur, systemTid(s.name), e.tick);
        cur += dur;
    }

    cursorMicros_ = stepStart + stepDur;
}

} // namespace threadmaxx
