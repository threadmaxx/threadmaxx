# `threadmaxx_input` — Maintainer Guide

How to extend `threadmaxx_input` without breaking the v1.x contract.

## Versioning

SemVer. `version.hpp` carries `THREADMAXX_INPUT_VERSION` and
`version_string()`. The `MAJOR * 10000 + MINOR * 100 + PATCH` integer
form is the hard pin — bump it together with the literal each release.

- **Patch** — bug fixes that preserve every observable behavior. Tests
  pass without changes.
- **Minor** — additive surface (new headers, new public API, new
  `Binding::Source` alternatives, new backends). Existing call sites
  keep compiling and produce identical results.
- **Major** — breaking changes (renames, removals, on-disk format bumps
  for `BindingSet` / `InputTrace`, behavior changes in existing
  function signatures). Avoid; deprecate first.

## ABI

Every header under `include/threadmaxx_input/` except `detail/` is part
of the v1.x ABI contract. Don't add private state to public types
without considering size-and-layout breakage — `InputState`,
`InputContext`, `Binding`, `BindingSet`, and `InputTrace` are the most
sensitive.

`InputState` is documented as a `trivially_copyable` POD. Adding fields
to it bumps minor (additive); changing field types or order bumps major.

`detail/` is private. Re-arrange freely.

## Hot-path discipline

The library's contract includes `zero allocations after warmup` for:
- `InputContext::beginFrame / endFrame`,
- `action()` queries,
- `toUIInput()`,
- `InputTrace::replayTo` + the replaying context's frame loop.

When adding code that runs on the hot path:

- **Don't introduce `std::string` or owning containers** unless you can
  prove they hit the reserve path. Use `std::string_view` / spans.
- **Don't iterate `std::unordered_map`** — `BindingSet` keeps a
  registration-order vector for that; reuse the same pattern if you add
  a new map.
- **`InputContext::eventBuffer_` is grown by power-of-two batches** and
  reused across frames. Don't switch it to `assign(span.begin(), end())`
  — that path can shrink.
- **Pin a no-alloc gate** for any new public API that runs per-frame.
  Copy the shape of `test_input_action_no_allocations` /
  `test_input_picking_no_allocations` /
  `test_input_ui_bridge_no_allocations`. The v1.0 crowd gate
  (`test_input_crowd_no_alloc`) is the load-bearing umbrella — extend
  it if the new API is a per-frame query.

The bench (`bench/input_crowd_bench`) is the throughput guardrail
(< 50 µs per frame at 200 actions / 800 bindings / 16 events). Re-run
it after any change to the per-frame path; +5% drift is OK, +20% is
not.

## Adding a new `Binding::Source`

1. Append the new value to `Binding::Source` enum (DO NOT reorder
   existing entries — those values land in serialized data).
2. Update `evaluateBindingHeld` and `evaluateBindingValue` in
   `InputContext.cpp` with the new arms.
3. Add a `static Binding::xxx()` builder for ergonomics.
4. Add at least one binding test (mirror `test_input_binding_single_source`).
5. Confirm `test_input_binding_serialize` still passes — the binary
   format encodes the source as `std::uint8_t`, so the new variant
   slots in without a version bump as long as no existing variant
   moved.

## Adding a new built-in input event

`InputEvent` is `std::variant<...>` — adding an alternative is a minor
bump. Check the variant size on a 32 B budget; the current largest
payload is `MouseMoveEvent` at 16 B.

1. Add the new POD struct in `events.hpp` next to its siblings.
2. Append it to the `InputEvent` variant. **Do NOT reorder** — replay
   trace files store the variant tag index.
3. Apply the event in `InputContext::applyEvent` (top-level `if
   constexpr` chain).
4. Add the matching `EventTag::` value at the END of the enum in
   `Trace.cpp`, plus the read/write arms. Same caveat: do not reorder.
5. If the event is a producer-side event for `GlfwBackend`, add a
   `pushGlfwXxx` helper and document its GLFW callback origin.

## Adding a new key

1. Insert the new `Key::` value in `types.hpp` BEFORE `Key::Count`. Bit
   positions inside the `KeyBitset` are derived from the enum integer,
   so adding before `Count` is safe but **adding before an existing
   entry shifts every bit**. Always append at the end of its group.
2. If you cross the 256-bit boundary (look at `kKeyBitsetWords`), the
   bitset grows automatically — but every serialized form that
   memcpy's a `KeyBitset` (none in v1.0) breaks. Mention in CHANGELOG.
3. Extend `detail::keymap.hpp` with the diagnostic name.
4. Extend `GlfwBackend::keyFromGlfw` with the GLFW scancode mapping.
5. Extend the UI bridge's `mapNavKeys` if it should surface as a
   nav-key bit.

## Backend authors

`IInputBackend` is the contract. New backends:

1. Subclass `IInputBackend`, override `poll`, `setCursorMode`,
   `connectedGamepads`.
2. Translate platform events into `InputEvent` variants — do NOT
   touch `InputContext` directly.
3. Provide a `reserve(n)` and a `clear()` if your queue can be reused;
   the test suite expects that shape.
4. Add a smoke test (mirror `test_input_glfw_smoke`).
5. Gate the build behind the right find_package check (see the GLFW
   pattern in `src/threadmaxx_input/CMakeLists.txt`).

## On-disk formats

Both `BindingSet::serialize` and `InputTrace::serialize` use tagged
headers (`'TMIB'` / `'TMIN'` + version `u32`). Bump the version field
when you change the layout AND keep the loader compatible with the
previous version — write `if (version == 1) ... else if (version == 2)`,
don't just reject old files.

Hosts that persist these blobs across builds (game settings, replay
archives) need migration paths — surface that in CHANGELOG.

## Things NOT to do

- Don't reintroduce string-based `action()` queries that hash inside
  the hot loop. Already-hashed `ActionId` is the contract.
- Don't make `BindingSet` thread-safe; the documented usage is "config
  during setup, read during play."
- Don't allocate inside `toUIInput`. The output is a small POD; copy.
- Don't pull in `<GLFW/glfw3.h>` from the GLFW backend header — the
  whole point is hosts can use it without installing GLFW headers.
- Don't add `std::filesystem` or any I/O. Backends are event sources;
  loaders / persisters are the host's job.
