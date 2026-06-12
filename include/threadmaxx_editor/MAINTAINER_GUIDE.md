# threadmaxx_editor — Maintainer Guide

Aimed at contributors landing changes to the library. Read alongside
`DESIGN_NOTES.md` (the spec) and `BACKEND_PORTING_GUIDE.md` (the
contract for new UI backends).

## Source layout

```
include/threadmaxx_editor/
  threadmaxx_editor.hpp    # umbrella
  session.hpp / backend.hpp / commands.hpp / ...
  backends/headless.hpp
src/threadmaxx_editor/
  EditorSession.cpp / CommandStack.cpp / Inspector.cpp / ...
  backends/HeadlessBackend.cpp
tests/editor/
  test_editor_*.cpp
  EditorTestFixture.hpp   # NoopGame + ScopedEngine helper
```

The library's static target is `threadmaxx_editor` (alias
`threadmaxx::editor`). PUBLIC depends on `threadmaxx::threadmaxx`;
PUBLIC depends on `threadmaxx::reflect` when both are built (this
also exports `THREADMAXX_EDITOR_HAS_REFLECT=1`, which the
`PropertyEditor` uses to gate the engine-bridge include).

## Build / test contract

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
cd build && ctest -R '^editor\.' --output-on-failure
```

Warnings-as-errors tree:

```
cmake -S . -B build-werror -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DTHREADMAXX_WARNINGS_AS_ERRORS=ON
cmake --build build-werror -j
cd build-werror && ctest -R '^editor\.' --output-on-failure
```

The editor compiles clean under `-Wall -Wextra -Wpedantic -Wshadow
-Wsign-conversion -Wconversion -Wold-style-cast -Werror`. Keep it
that way.

## Adding a new public symbol

1. Add it to the appropriate `include/threadmaxx_editor/*.hpp` with a
   short `@brief`. Load-bearing methods also get `@thread_safety`.
2. Add an implementation file under `src/threadmaxx_editor/` if not
   header-only.
3. Update `src/threadmaxx_editor/CMakeLists.txt` (add to either
   `THREADMAXX_EDITOR_PUBLIC_HEADERS` or `THREADMAXX_EDITOR_SOURCES`).
4. Add it to the umbrella `threadmaxx_editor.hpp`.
5. Add at least one focused test in `tests/editor/` and register it in
   `tests/editor/CMakeLists.txt`.
6. Re-run both build trees + the editor test slice.
7. If the symbol is a new editor-facing concept, add a section to
   `USER_GUIDE.md` and an entry to `README.md`'s public-surface table.

## Versioning

`THREADMAXX_EDITOR_VERSION = MAJOR*10000 + MINOR*100 + PATCH`. Three
artifacts must move together on every bump:

- `include/threadmaxx_editor/version.hpp` macros and
  `version_string()` literal,
- `tests/editor/test_editor_version.cpp` pin.

(There's no `project(VERSION)` for the editor in `CMakeLists.txt`
yet; the engine carries the project version.)

## Adding a new IEditorBackend

See `BACKEND_PORTING_GUIDE.md`. In short:

1. Subclass `IEditorBackend` in
   `src/threadmaxx_editor/backends/MyBackend.cpp`.
2. Override the lifecycle (`initialize`, `shutdown`) and draw
   (`beginFrame`, `endFrame`, `drawText`, `drawRect`) methods.
3. Add it to `THREADMAXX_EDITOR_SOURCES` in
   `src/threadmaxx_editor/CMakeLists.txt`, gated on whatever toolkit
   it requires (e.g. `if (TARGET imgui)`).
4. Add a smoke binary under `examples/editor_demo` that exercises the
   backend against a small running engine.

## Commit cadence

Each batch is independently shippable and lands as one commit.
Standing authorization (see CLAUDE.md, `feedback_auto_commit_batches`)
is to commit immediately when a batch ships green.
