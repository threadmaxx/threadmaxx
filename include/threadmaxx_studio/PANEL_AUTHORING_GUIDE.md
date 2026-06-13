# Panel Authoring Guide

This guide is the per-panel recipe. Read `MAINTAINER_GUIDE.md` first for
the contribution conventions.

## The contract

Every panel implements `IStudioPanel`:

```cpp
class IStudioPanel {
public:
    virtual ~IStudioPanel() = default;
    virtual std::string_view id() const noexcept = 0;
    virtual std::string_view title() const noexcept = 0;
    virtual void render(editor::IEditorBackend& backend,
                        IStudioDataSource& source) = 0;
    virtual void onAttachChanged(AttachMode) {}
};
```

- `id()` is the layout-persistence key. Convention: lowercase dotted
  segments, e.g. `"engine.inspector"`, `"network.packet_trace"`.
- `title()` is the human-readable window title.
- `render()` is the once-per-frame draw call. Single-threaded; the
  framework never calls `render()` on the same instance from two
  threads.
- `onAttachChanged()` lets a panel invalidate cached state when the
  host swaps `DirectDataSource` for `RemoteDataSource`.

## Step-by-step: add a `FooBar` panel

### 1. Header

```cpp
// include/threadmaxx_studio/panels/foo_bar.hpp
#pragma once

/// @file panels/foo_bar.hpp
/// @brief STxx — FooBarPanel renders the Foo system's Bar state.

#include "../panel.hpp"

#include <string_view>

namespace threadmaxx::studio {

class FooBarPanel final : public IStudioPanel {
public:
    std::string_view id()    const noexcept override { return "foo.bar"; }
    std::string_view title() const noexcept override { return "Foo Bar"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    // Optional panel-state hooks the tests want to assert on.
    std::size_t rowCount()  const noexcept { return lastRows_; }
    void        setMaxRows(std::size_t n) noexcept { maxRows_ = n; }

private:
    std::size_t lastRows_ = 0;
    std::size_t maxRows_  = 32;
};

} // namespace threadmaxx::studio
```

The header includes only `panel.hpp` (and STL). It does NOT include
any `threadmaxx/` or sibling header. Test-observable state lives on
the class.

### 2. Implementation

```cpp
// src/threadmaxx_studio/panels/FooBarPanel.cpp
#include <threadmaxx_studio/panels/foo_bar.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <cstdio>

namespace threadmaxx::studio {

void FooBarPanel::render(editor::IEditorBackend& backend,
                        IStudioDataSource& source) {
    char buf[200];
    const auto summary = source.engineSnapshot();
    if (!summary) {
        backend.drawText("Foo Bar: <no engine attached>", 0.0f, 0.0f);
        lastRows_ = 1;
        return;
    }

    std::snprintf(buf, sizeof(buf),
                  "Foo Bar  tick=%llu  paused=%d",
                  static_cast<unsigned long long>(summary->tick),
                  summary->paused ? 1 : 0);
    backend.drawText(buf, 0.0f, 0.0f);

    // ... per-row draws clamped to maxRows_ ...
    lastRows_ = 1 + drawn;
}

} // namespace threadmaxx::studio
```

Implementation rules:
- Render through `backend.drawText` / `drawLine` / `drawRect`. Never
  reach for ImGui directly.
- Read via `source.engineSnapshot()` and sibling-stat accessors.
  Treat `std::nullopt` as a graceful-degradation signal and draw a
  placeholder.
- Snapshot the test-observable state (`lastRows_`, etc.) at the end
  of `render()` so tests can assert on it.

### 3. CMake wiring

For an engine-only panel, append both paths to the base lists in
`src/threadmaxx_studio/CMakeLists.txt`:

```cmake
set(THREADMAXX_STUDIO_PUBLIC_HEADERS
    ...
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_studio/panels/foo_bar.hpp
)

set(THREADMAXX_STUDIO_SOURCES
    ...
    panels/FooBarPanel.cpp
)
```

For a sibling-dependent panel, append inside the matching
`if (TARGET threadmaxx::<sibling>)` block, alongside the existing
gated sources:

```cmake
if (TARGET threadmaxx::foo)
    target_sources(threadmaxx_studio PRIVATE
        panels/FooBarPanel.cpp
        ${CMAKE_SOURCE_DIR}/include/threadmaxx_studio/panels/foo_bar.hpp)
    target_link_libraries(threadmaxx_studio PUBLIC threadmaxx::foo)
    target_compile_definitions(threadmaxx_studio PUBLIC
        THREADMAXX_STUDIO_HAS_FOO_PANEL=1)
endif()
```

### 4. Test

```cpp
// tests/studio/test_studio_foo_bar_panel.cpp
/// @file test_studio_foo_bar_panel.cpp
/// @brief STxx — FooBarPanel renders + tracks row count.

#include "Check.hpp"

#include <threadmaxx_studio/panels/foo_bar.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/backends/headless.hpp>

namespace {

class StubSource final : public threadmaxx::studio::IStudioDataSource {
public:
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
    std::optional<threadmaxx::studio::EngineFrameSummary>
    engineSnapshot() const override {
        threadmaxx::studio::EngineFrameSummary s{};
        s.tick = 42;
        return s;
    }
};

} // namespace

int main() {
    threadmaxx::studio::FooBarPanel panel;
    threadmaxx::editor::HeadlessBackend backend;
    CHECK(backend.initialize());
    backend.beginFrame();

    StubSource source;
    panel.render(backend, source);
    CHECK(panel.rowCount() > 0u);
    EXIT_WITH_RESULT();
}
```

### 5. Test registration

For an engine-only panel, append to the base `THREADMAXX_STUDIO_TESTS`
list in `tests/studio/CMakeLists.txt`:

```cmake
set(THREADMAXX_STUDIO_TESTS
    ...
    test_studio_foo_bar_panel
)
```

For a sibling-dependent panel, append inside the matching `MATCHES`
gate:

```cmake
if (_studio_compile_defs MATCHES "THREADMAXX_STUDIO_HAS_FOO_PANEL=1")
    list(APPEND THREADMAXX_STUDIO_TESTS test_studio_foo_bar_panel)
endif()
```

### 6. Build + ctest

```
cmake --build build -j
ctest --test-dir build -R '^studio\.test_studio_foo_bar_panel'
```

Always run both `build/` and `build-werror/`. Add a CHANGELOG entry.

## Gotchas

- The header MUST NOT include any sibling header. Forward-declare in
  the header; include in the cpp. The test that pins this is
  `test_studio_no_engine_link_canary`.
- For Shape B, the panel reads through `IStudioDataSource`. If the
  data your panel needs isn't on the abstract base, extend the
  `*StatsView` POD AND populate it from both
  `DirectDataSource::<sibling>Stats()` and the
  `RemoteDataSource` decoder. Otherwise the panel won't survive
  the attach-mode swap.
- Test the placeholder branch. Every panel should render something
  reasonable when `source.engineSnapshot()` returns `nullopt` —
  Shape B can be in that state for the first few frames after a
  reconnect.

## Naming conventions

- Header file: `panels/<snake_name>.hpp`.
- Class name: `<UpperCamel>Panel`.
- `id()`: `"<sibling>.<short>"`, e.g. `"network.aoi"`.
- `title()`: human-readable, no leading verb, no trailing
  ellipsis.
- Test file: `tests/studio/test_studio_<snake_behavior>.cpp` —
  describe the *behavior*, not the panel ("renders snapshot" vs.
  "FrameSnapshotPanel").

## When you're done

- All five studio test buckets green on both flavors.
- `studio_overhead` bench within budget if the panel is heavy.
- `CHANGELOG.md` entry mentions the new panel.
- Commit message follows the "lands STxx" convention.
