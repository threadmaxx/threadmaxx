#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace threadmaxx::assets {

// Polling filesystem watcher. Each `tick()` checks the mtime of every
// path in the watch set; entries whose mtime changed since the previous
// tick are returned. The first tick after `watch(...)` does NOT return
// the file as changed (mtime is captured as the baseline).
//
// Poll-based by design — `inotify` / `kqueue` / `ReadDirectoryChangesW`
// are platform-specific and need their own threads; the polling fallback
// keeps the v1.0 surface portable. The host gates how often `tick()` is
// called.
class FilesystemWatcher {
public:
    FilesystemWatcher();
    ~FilesystemWatcher();
    FilesystemWatcher(const FilesystemWatcher&) = delete;
    FilesystemWatcher& operator=(const FilesystemWatcher&) = delete;

    void watch(std::string_view path);
    void unwatch(std::string_view path);
    [[nodiscard]] std::size_t watchCount() const noexcept;

    // Returns a span of paths whose mtime changed since the previous
    // tick(). The span aliases an internal buffer that's invalidated by
    // the next tick(); copy what you need.
    std::span<const std::string> tick();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace threadmaxx::assets
