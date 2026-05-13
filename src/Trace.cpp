/// @file Trace.cpp
/// JSON-Lines serializer for @ref threadmaxx::FrameSnapshot.
///
/// No allocations beyond what the supplied ostream's buffer does. The
/// schema is intentionally flat — one line per tick, fields named to
/// match the stats structs — so adapters to Chrome-trace, Tracy, or a
/// custom ingest stack can be written in a handful of lines on the
/// downstream side.
#include "threadmaxx/Trace.hpp"

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
    os << "{\"tick\":" << e.tick
       << ",\"step_s\":" << e.lastStepSeconds
       << ",\"avg_step_s\":" << e.avgStepSeconds
       << ",\"commit_s\":" << e.commitDurationSeconds
       << ",\"jobs\":" << e.jobsSubmittedLastStep
       << ",\"commands\":" << e.commandsCommittedLastStep
       << ",\"alive\":" << e.aliveEntities
       << ",\"systems\":[";

    for (std::size_t i = 0; i < snap.systems.size(); ++i) {
        const auto& s = snap.systems[i];
        if (i != 0) os << ',';
        os << "{\"name\":";
        writeJsonString(os, s.name);
        os << ",\"update_s\":" << s.lastUpdateSeconds
           << ",\"avg_update_s\":" << s.avgUpdateSeconds
           << ",\"jobs\":" << s.jobsSubmittedLastStep
           << ",\"commands\":" << s.commandsCommittedLastStep
           << "}";
    }

    const auto& j = snap.jobs;
    os << "],\"job_pool\":{"
       << "\"total_jobs\":" << j.totalJobs
       << ",\"own_pops\":" << j.ownPops
       << ",\"steals\":" << j.stolenJobs
       << ",\"workers\":" << j.workerCount
       << "}}\n";
}

} // namespace threadmaxx
