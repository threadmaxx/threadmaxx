# Porting a new editor backend

The editor core stays UI-toolkit-agnostic. Every panel emits its UI
through `IEditorBackend`. Adding a new backend (Dear ImGui, native
Windows, web, custom) means implementing one interface and registering
its source file in CMake.

## The contract

```cpp
class IEditorBackend {
public:
    virtual ~IEditorBackend() = default;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;

    virtual void drawText(std::string_view text, float x, float y) = 0;
    virtual void drawRect(float x, float y, float w, float h) = 0;
};
```

- `initialize()` runs once when the host calls
  `EditorSession::setBackend(backend)`. Bind GPU/OS resources here.
  Return `true` on success; the session refuses to attach on `false`.
- `shutdown()` runs once on session teardown (or on
  `setBackend(nullptr)` / `setBackend(otherBackend)`).
- `beginFrame()` / `endFrame()` bracket every per-frame batch of
  draw calls. Buffer or submit at `endFrame()` — the rest of the
  editor expects the frame to be observable after `endFrame()`
  returns.
- `drawText(text, x, y)` and `drawRect(x, y, w, h)` are the v1.0
  primitives. Coordinates are in editor logical space (the host
  installs the mapping to physical pixels).

## Adding a backend

1. Create `src/threadmaxx_editor/backends/MyBackend.cpp` and
   (optionally) a public header at
   `include/threadmaxx_editor/backends/my.hpp`.
2. Subclass `IEditorBackend`. Use the headless backend as the
   reference implementation:
   `src/threadmaxx_editor/backends/HeadlessBackend.cpp`.
3. Gate the source file in `src/threadmaxx_editor/CMakeLists.txt`:

   ```cmake
   if (TARGET imgui::imgui)
       target_sources(threadmaxx_editor PRIVATE
           backends/ImGuiBackend.cpp)
       target_link_libraries(threadmaxx_editor PUBLIC imgui::imgui)
       target_compile_definitions(threadmaxx_editor PUBLIC
           THREADMAXX_EDITOR_HAS_IMGUI_BACKEND=1)
   endif()
   ```

4. Add a smoke binary under `examples/editor_demo/` that exercises
   the full editor against a live engine + your backend.
5. Document the backend's lifecycle / threading expectations in its
   public header.

## Testing without a real toolkit

Every panel and overlay is exercised via `HeadlessBackend` in the
test suite (`tests/editor/test_editor_*.cpp`). Your backend should
not need new tests for the *core* editor logic — only for
backend-specific behavior (e.g. ImGui dock state translation).
