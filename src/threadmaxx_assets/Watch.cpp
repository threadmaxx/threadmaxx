#include "threadmaxx_assets/watch.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace threadmaxx::assets {

struct FilesystemWatcher::Impl {
    std::unordered_map<std::string, std::filesystem::file_time_type> mtimes;
    std::vector<std::string> changed;
};

FilesystemWatcher::FilesystemWatcher() : impl_(std::make_unique<Impl>()) {}
FilesystemWatcher::~FilesystemWatcher() = default;

void FilesystemWatcher::watch(std::string_view path) {
    std::string key{path};
    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(key, ec);
    if (ec) {
        // Watched path doesn't exist yet; install with zero mtime so the
        // first appearance after a tick fires as a change.
        impl_->mtimes[key] = std::filesystem::file_time_type{};
    } else {
        impl_->mtimes[key] = mtime;
    }
}

void FilesystemWatcher::unwatch(std::string_view path) {
    impl_->mtimes.erase(std::string(path));
}

std::size_t FilesystemWatcher::watchCount() const noexcept {
    return impl_->mtimes.size();
}

std::span<const std::string> FilesystemWatcher::tick() {
    impl_->changed.clear();
    for (auto& [path, last] : impl_->mtimes) {
        std::error_code ec;
        auto now = std::filesystem::last_write_time(path, ec);
        if (ec) {
            continue; // Vanished mid-session; leave the slot, will fire on
                      // next appearance.
        }
        if (now != last) {
            impl_->changed.push_back(path);
            last = now;
        }
    }
    return std::span<const std::string>(impl_->changed);
}

} // namespace threadmaxx::assets
