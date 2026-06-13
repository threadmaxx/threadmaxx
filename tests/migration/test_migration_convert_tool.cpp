/// @file test_migration_convert_tool.cpp
/// @brief M8 — smoke test for the threadmaxx_migration_convert exe.
/// Build a tiny fixture save in a temp file, invoke the tool, verify
/// the output file loads cleanly.

#include "Check.hpp"

#include <threadmaxx_migration/io.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

namespace {

std::string toolPath() {
    // Tests run from build/, the convert exe lives at
    // build/tools/migration_convert/threadmaxx_migration_convert.
    if (const char* env = std::getenv("THREADMAXX_MIGRATION_CONVERT_BIN")) {
        return env;
    }
    return "./tools/migration_convert/threadmaxx_migration_convert";
}

bool writeBytes(const std::string& path,
                const std::vector<std::byte>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

bool readBytes(const std::string& path, std::vector<std::byte>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    out.resize(static_cast<std::size_t>(in.tellg()));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(out.data()),
            static_cast<std::streamsize>(out.size()));
    return in.good() || in.eof();
}

} // namespace

int main() {
    using namespace threadmaxx::migration;

    // Build a small fixture.
    RecordSet rs{};
    rs.metadata.productName = "ConvertSmoke";
    rs.metadata.schemaVersion = SchemaVersion{1, 0, 0};
    Record r1{};
    r1.typeName = "Foo";
    r1.stableId = 1;
    r1.sourceVersion = SchemaVersion{1, 0, 0};
    r1.fields.push_back({"x", FieldValue{{std::byte{0x42}}}});
    rs.records.push_back(r1);
    Record r2{};
    r2.typeName = "Bar";
    r2.stableId = 2;
    r2.sourceVersion = SchemaVersion{1, 0, 0};
    rs.records.push_back(r2);
    const auto blob = writeRecordSet(rs);

    const std::string inPath  = "convert_smoke_in.bin";
    const std::string outPath = "convert_smoke_out.bin";
    CHECK(writeBytes(inPath, blob));

    // Invoke the tool without --target (pure roundtrip).
    std::string cmd = toolPath() + " " + inPath + " " + outPath +
                      " > /dev/null 2>&1";
    const int rc = std::system(cmd.c_str());
    CHECK_EQ(rc, 0);

    // Output loads cleanly.
    std::vector<std::byte> outBlob;
    CHECK(readBytes(outPath, outBlob));
    auto loaded = loadRecordSet(outBlob);
    CHECK(loaded.ok);
    CHECK_EQ(loaded.set.records.size(), 2u);
    CHECK_EQ(loaded.set.metadata.productName,
             std::string{"ConvertSmoke"});

    // Cleanup.
    std::remove(inPath.c_str());
    std::remove(outPath.c_str());

    EXIT_WITH_RESULT();
}
