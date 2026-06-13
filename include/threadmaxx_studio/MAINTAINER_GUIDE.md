# `threadmaxx_studio` — Maintainer Guide

This guide is for contributors extending `threadmaxx_studio`. For
host-author orientation, see `USER_GUIDE.md`; for the v1.0 spec, see
`DESIGN_NOTES.md`.

## Repo layout

```
include/threadmaxx_studio/
  version.hpp                # version stamp
  panel.hpp                  # IStudioPanel
  data_source.hpp            # IStudioDataSource + AttachMode
  studio.hpp                 # StudioApp + PanelHost
  direct_data_source.hpp     # Shape A
  remote_data_source.hpp     # Shape B (gated TARGET threadmaxx::network)
  agent.hpp                  # Game-side RPC endpoint (gated TARGET threadmaxx::network)
  shard_directory.hpp        # Multi-shard selection (gated TARGET threadmaxx::network)
  panels/<panel>.hpp         # one per concrete panel
  README.md / USER_GUIDE.md / MAINTAINER_GUIDE.md /
  PANEL_AUTHORING_GUIDE.md / DESIGN_NOTES.md / FUTURE_WORK.md / CHANGELOG.md

src/threadmaxx_studio/
  CMakeLists.txt             # sibling-gated link + define set
  StudioApp.cpp / PanelHost.cpp / StudioVersion.cpp
  DirectDataSource.cpp / RemoteDataSource.cpp / StudioAgent.cpp /
  ShardDirectory.cpp
  panels/<Panel>.cpp         # one per concrete panel

tests/studio/
  CMakeLists.txt             # MATCHES-gated test set
  test_studio_*.cpp          # one per shipped behavior

examples/studio_demo/
  main.cpp                   # end-to-end demo (ST40)

bench/
  studio_overhead.cpp        # perf gate (ST41)
```

## Load-bearing invariants

These are pinned by the test suite — break one and the build goes
red. Read DESIGN_NOTES §3 for the full rationale.

1. **`IStudioPanel` + `IStudioDataSource` are the only public
   abstractions panels see.** Panels never include
   `<threadmaxx/Engine.hpp>` directly. The studio is engine-agnostic
   at link time; only `DirectDataSource.cpp` pulls the core engine
   headers (pinned by `test_studio_no_engine_link_canary`).
2. **Every panel works under both attach modes.** If you write a new
   panel, both `DirectDataSource` and `RemoteDataSource` must drive
   it. The `EngineFrameSummary` POD is the lingua franca; expand it
   rather than reaching for a sibling-specific type.
3. **Mutations go through `editor::CommandStack`.** No sidecar paths.
   Shape B routes label → factory → `IEditCommand` server-side; the
   wire is forge-resistant (peer focus on
   `AgentRequestTag::SetClientFocus` rewrites the client-supplied
   peerId to the actual transport `from`).
4. **Layout state lives in `editor::LayoutManager`.** Studio never
   defines a parallel layout system.
5. **Sibling-independence at link time.** Every panel cpp is gated by
   `if (TARGET threadmaxx::<sibling>)` in
   `src/threadmaxx_studio/CMakeLists.txt`; matching tests gate on
   the `THREADMAXX_STUDIO_HAS_<X>=1` definition via `MATCHES`.

## Adding a panel

See `PANEL_AUTHORING_GUIDE.md` for the full recipe. The summary:

1. Drop `panels/<name>.hpp` next to the existing ones.
2. Drop `src/threadmaxx_studio/panels/<Name>Panel.cpp`.
3. Add both to `src/threadmaxx_studio/CMakeLists.txt` (under the
   correct sibling gate if applicable).
4. Add `tests/studio/test_studio_<name>.cpp`.
5. Register the test in `tests/studio/CMakeLists.txt` (under the
   matching `MATCHES` gate if applicable).
6. Build + ctest both `build/` and `build-werror/`.
7. Add an entry to `CHANGELOG.md`.

## Sibling-gating recipe

When the panel depends on a sibling library:

1. In `src/threadmaxx_studio/CMakeLists.txt`, under the
   `if (TARGET threadmaxx::<sibling>)` block:
   - Add the panel `.cpp` to `target_sources`.
   - Add the panel header to `target_sources` (so it ships with the
     install).
   - PUBLIC-link the sibling target if not already linked.
   - Add `THREADMAXX_STUDIO_HAS_<X>=1` to
     `target_compile_definitions`.
2. In `tests/studio/CMakeLists.txt`:
   - Add the test name to the matching
     `if (_studio_compile_defs MATCHES "THREADMAXX_STUDIO_HAS_<X>=1")`
     block.
3. Match the `editor`-only panels for the "no sibling needed" path
   (just append to the base `THREADMAXX_STUDIO_TESTS` list).

## Build + test loop

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
ctest --test-dir build -R '^studio\.' --output-on-failure

# Werror flavor (always run before merging):
cmake -S . -B build-werror -DCMAKE_BUILD_TYPE=Release \
  -DTHREADMAXX_WARNINGS_AS_ERRORS=ON
cmake --build build-werror -j
ctest --test-dir build-werror -R '^studio\.' --output-on-failure
```

The studio test set must be 100% green on both. The repo has a
standing convention that every batch commit drops with a green
test count delta.

## Perf gate

`bench/studio_overhead` is the regression gate. Opt-in via
`-DTHREADMAXX_BUILD_BENCHMARKS=ON`. The bench measures the
per-frame cost of `panel.render()` across the full panel set under
the headless backend. The v1.0 budget is < 1 ms / frame at 1080p
with 50 visible widgets.

```
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release \
  -DTHREADMAXX_BUILD_BENCHMARKS=ON
cmake --build build-bench --target studio_overhead -j
./build-bench/bench/studio_overhead
```

## Version bumps

The version stamp moves in `include/threadmaxx_studio/version.hpp`.
Three lines move together: macros, `version_string()`, and the
`test_studio_version` test. The test is the contract; mismatched
constants fail at link / run.

`CHANGELOG.md` gets a new section. Existing sections are immutable
history — never edit, only append.

## Release checklist (v1.x and beyond)

1. Update version.hpp + `test_studio_version`.
2. Append `CHANGELOG.md` entry.
3. Update `README.md` "Status" line.
4. Rebuild both flavors green.
5. Run `bench/studio_overhead`; record the number in the
   `CHANGELOG.md` entry.
6. Run `examples/studio_demo` and confirm exit code 0.
7. Commit with the message body following the project's "lands
   <batch>" convention.

## Out of scope (forever)

DESIGN_NOTES §9 — none of this is going to land:

- Asset authoring (game / host concern)
- Animation graph editing (separate sibling)
- Save editing offline (migration territory)
- Network packet replay against a live game (anti-cheat)
- Renderer ownership
- Sidecar mutation paths
- ImGui as a hard dependency in studio core
- Studio panels pushed into sibling libraries

If a contribution touches any of these, the answer is "no" — please
discuss the architectural change first.
