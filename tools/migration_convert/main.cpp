/// @file main.cpp
/// @brief M8 — `threadmaxx_migration_convert`: offline conversion of
/// a save file from one schema version to another.
///
/// Usage:
///   threadmaxx_migration_convert <input> <output> [--target M.m.p]
///
/// With `--target` omitted, the tool loads the input but does not
/// migrate (it's a roundtrip / validate-only mode). The exe never
/// links the engine bridge — it's pure RecordSet I/O.

#include <threadmaxx_migration/io.hpp>
#include <threadmaxx_migration/pipeline.hpp>
#include <threadmaxx_migration/report.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool parseVersion(const std::string& s,
                  threadmaxx::migration::SchemaVersion& out) {
    // Accept M.m.p.
    auto first = s.find('.');
    if (first == std::string::npos) return false;
    auto second = s.find('.', first + 1);
    if (second == std::string::npos) return false;
    try {
        out.major = static_cast<std::uint32_t>(std::stoul(s.substr(0, first)));
        out.minor = static_cast<std::uint32_t>(std::stoul(s.substr(first + 1,
                                                                  second - first - 1)));
        out.patch = static_cast<std::uint32_t>(std::stoul(s.substr(second + 1)));
    } catch (...) {
        return false;
    }
    return true;
}

bool readAllBytes(const std::string& path, std::vector<std::byte>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    const auto s = ss.str();
    out.resize(s.size());
    if (!s.empty()) {
        std::memcpy(out.data(), s.data(), s.size());
    }
    return true;
}

bool writeAllBytes(const std::string& path,
                   std::span<const std::byte> bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

} // namespace

int main(int argc, char** argv) {
    using namespace threadmaxx::migration;

    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s <input> <output> [--target M.m.p]\n",
                     argc > 0 ? argv[0] : "threadmaxx_migration_convert");
        return 2;
    }
    const std::string inputPath  = argv[1];
    const std::string outputPath = argv[2];
    SchemaVersion target{};
    bool hasTarget = false;
    for (int i = 3; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--target") {
            if (!parseVersion(argv[i + 1], target)) {
                std::fprintf(stderr, "error: bad --target value '%s'\n",
                             argv[i + 1]);
                return 2;
            }
            hasTarget = true;
            break;
        }
    }

    std::vector<std::byte> bytes;
    if (!readAllBytes(inputPath, bytes)) {
        std::fprintf(stderr, "error: cannot read '%s'\n", inputPath.c_str());
        return 1;
    }

    auto load = loadRecordSet(bytes);
    if (!load.ok) {
        std::fprintf(stderr, "error: %s\n", load.error.c_str());
        return 1;
    }
    std::printf("loaded: %zu records, schema %u.%u.%u\n",
                load.set.records.size(),
                load.set.metadata.schemaVersion.major,
                load.set.metadata.schemaVersion.minor,
                load.set.metadata.schemaVersion.patch);

    RecordSet out = std::move(load.set);
    if (hasTarget) {
        MigrationRegistry registry;
        for (const auto& rec : out.records) {
            registry.registerType(rec.typeName, rec.sourceVersion);
        }
        MigrationPipeline pipeline{registry};
        MigrationOptions opts{};
        opts.failOnUnknownType = false;
        opts.keepUnknownFields = true;
        auto result = pipeline.migrate(out, target, opts);
        const auto summary = summarize(result);
        std::printf("converted: ok=%d, records=%zu, warnings=%zu, "
                    "errors=%zu, applied=%zu\n",
                    summary.ok ? 1 : 0,
                    summary.recordCount,
                    summary.warningCount,
                    summary.errorCount,
                    summary.appliedStepCount);
        for (const auto& w : result.warnings) {
            std::printf("  warning: %s\n", w.c_str());
        }
        for (const auto& e : result.errors) {
            std::fprintf(stderr, "  error: %s\n", e.c_str());
        }
        if (!result.ok) return 1;
        out = std::move(result.output);
    }

    auto blob = writeRecordSet(out);
    if (!writeAllBytes(outputPath, blob)) {
        std::fprintf(stderr, "error: cannot write '%s'\n", outputPath.c_str());
        return 1;
    }
    std::printf("wrote %zu bytes to %s\n", blob.size(), outputPath.c_str());
    return 0;
}
