#include "Check.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>

#include "threadmaxx_assets/watch.hpp"

using namespace threadmaxx::assets;

namespace {

std::filesystem::path makeTempFile(const char* name, std::string_view content) {
    auto p = std::filesystem::temp_directory_path() / name;
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    return p;
}

} // namespace

int main() {
    auto p1 = makeTempFile("threadmaxx_assets_watch_a.bin", "v0");
    auto p2 = makeTempFile("threadmaxx_assets_watch_b.bin", "u0");

    FilesystemWatcher w;
    w.watch(p1.string());
    w.watch(p2.string());
    CHECK_EQ(w.watchCount(), std::size_t{2});

    // First tick after watch baselines mtime — no changes reported.
    auto changed0 = w.tick();
    CHECK(changed0.empty());

    // Touch p1 with new content; filesystem mtime granularity varies, so
    // sleep a tick to make sure we land on a different one.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    {
        std::ofstream out(p1, std::ios::binary | std::ios::trunc);
        out << "v1";
    }
    auto changed1 = w.tick();
    bool sawP1 = false;
    for (const auto& c : changed1) {
        if (c == p1.string()) sawP1 = true;
    }
    CHECK(sawP1);

    // No further changes → empty.
    auto changed2 = w.tick();
    CHECK(changed2.empty());

    // unwatch removes the entry; touching the file after unwatch produces
    // no event.
    w.unwatch(p1.string());
    CHECK_EQ(w.watchCount(), std::size_t{1});
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    {
        std::ofstream out(p1, std::ios::binary | std::ios::trunc);
        out << "v2";
    }
    auto changed3 = w.tick();
    bool sawP1Again = false;
    for (const auto& c : changed3) {
        if (c == p1.string()) sawP1Again = true;
    }
    CHECK(!sawP1Again);

    std::filesystem::remove(p1);
    std::filesystem::remove(p2);

    EXIT_WITH_RESULT();
}
