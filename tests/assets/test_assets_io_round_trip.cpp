#include "Check.hpp"

#include <filesystem>
#include <fstream>
#include <string_view>

#include "threadmaxx_assets/detail/io.hpp"

using namespace threadmaxx::assets;

namespace {

std::filesystem::path writeTemp(const char* name, std::string_view content) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return path;
}

} // namespace

int main() {
    const auto p = writeTemp("threadmaxx_assets_io_test.bin",
                             std::string_view{"\x01\x02\x03\x04\xFF\xEE", 6});

    auto res = detail::readFile(p.string());
    CHECK(res.ok());
    CHECK_EQ(res.value.size(), std::size_t{6});
    if (res.value.size() == 6) {
        CHECK_EQ(static_cast<unsigned>(res.value[0]), 0x01u);
        CHECK_EQ(static_cast<unsigned>(res.value[4]), 0xFFu);
    }

    std::vector<std::byte> reused;
    reused.reserve(64);
    const auto cap = reused.capacity();
    auto code = detail::readFileInto(p.string(), reused);
    CHECK(code == ErrorCode::Ok);
    CHECK_EQ(reused.size(), std::size_t{6});
    // capacity is preserved across the call (no shrink) when caller pre-reserved.
    CHECK(reused.capacity() >= cap);

    auto missing = detail::readFile("/this/path/does/not/exist/asset_io_test");
    CHECK(!missing.ok());
    CHECK(missing.code == ErrorCode::FileNotFound);

    std::filesystem::remove(p);

    EXIT_WITH_RESULT();
}
