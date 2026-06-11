# threadmaxx_ui — Sibling Library Specification Notes

## Purpose

`threadmaxx_ui` is the sibling library that provides **immediate-mode UI primitives** for tools and in-game chrome inside the `threadmaxx` ecosystem.

It is meant to power:

* editor panels
* property inspectors
* trees and lists
* menus and popups
* drag handles and gizmo overlays
* debug HUDs and benchmarking overlays
* simple in-game tool windows

It is **not** a general-purpose application framework, and it is **not** a replacement for the engine renderer.

The guiding principle is the same as the rest of `threadmaxx`:

* small, explicit APIs
* deterministic-friendly behavior where practical
* minimal allocation pressure in hot paths
* easy to test
* easy to benchmark
* easy to embed in tools or game code

## Relationship to threadmaxx core

`threadmaxx_ui` sits **above** the core engine.

The core engine already handles:

* simulation
* scheduling
* entity storage
* command buffers
* render-frame construction
* deterministic commit behavior

`threadmaxx_ui` should not duplicate those responsibilities. It should consume them.

The intended stack is:

* `threadmaxx` core for world/simulation/render-frame plumbing
* `threadmaxx_input` for normalized input
* `threadmaxx_ui` for UI state, layout, and interaction
* rendering backend / renderer bridge for drawing UI primitives

## Proposed package layout

```text
include/threadmaxx_ui/
  threadmaxx_ui.hpp     // umbrella include
  context.hpp           // frame context, input, layout, state
  widget.hpp            // widget IDs, flags, basic element records
  layout.hpp            // layout helpers and constraints
  draw.hpp              // draw list and primitive commands
  theme.hpp             // colors, fonts, metrics, style
  text.hpp              // text shaping / wrapping helpers
  input.hpp             // UI focus, navigation, capture, hover
  tree.hpp              // tree nodes, expand/collapse helpers
  menu.hpp              // menu / popup helpers
  panel.hpp             // docking / panel definitions
  dragdrop.hpp          // drag source / drop target helpers
  gizmo.hpp             // handles / overlays for editor-style interaction
  debug.hpp             // debug overlays and HUD helpers
  backend.hpp           // backend interface for renderer integration
  inspect.hpp           // property inspection helpers
  detail/
    id_stack.hpp
    rect_math.hpp
    clip_stack.hpp
    text_cache.hpp
    nav_state.hpp
    input_router.hpp
```

## Library goals (G-x)

### G-1

Provide a small, immediate-mode UI API that is sufficient for editor chrome and debug UI.

### G-2

Make it easy to build the first useful editor shell for `threadmaxx` without introducing a heavyweight GUI dependency.

### G-3

Keep UI state explicit and frame-driven, so it is easy to reason about and test.

### G-4

Support common tooling primitives out of the box:

* panels
* trees
* property rows
* buttons
* labels
* sliders
* checkboxes
* popups
* menus
* drag/drop
* simple text entry
* gizmo handles

### G-5

Integrate cleanly with the existing renderer and render-frame model.

### G-6

Remain fast enough for always-on debug HUDs and editor overlays.

### G-7

Remain small enough that its internals are understandable and benchmarkable.

## Non-goals (NG-x)

### NG-1

Do not build a full application framework.

### NG-2

Do not own the game loop, windowing system, or platform abstraction.

### NG-3

Do not replace the renderer.

### NG-4

Do not store persistent app state in hidden singletons.

### NG-5

Do not force docking, theming, or multi-window support if the project does not need them.

### NG-6

Do not build a rich text editor, code editor, or document editing system in v1.

### NG-7

Do not make asset import, filesystem browsing, or reflection a hard dependency.

### NG-8

Do not require GPU-driven UI geometry generation.

### NG-9

Do not make the UI system depend on the editor being present; the library must also be useful for in-game overlays and debug tools.

### NG-10

Do not make the UI API so large that it becomes impossible to test or stabilize.

## Invariants (I-x)

### I-1

UI state for a frame is built from explicit calls made during that frame.

### I-2

Widgets are identified by stable IDs derived from the current ID stack and widget identity, not by memory address alone.

### I-3

UI interactions must be deterministic given the same input stream, frame order, and state seed.

### I-4

A frame's UI build must not mutate the simulation world directly.

### I-5

Layout decisions must be reproducible within a frame.

### I-6

A widget may be hovered, focused, active, or disabled, but those states must be unambiguous.

### I-7

Input capture must be explicit. The UI may consume input, but it must not secretly steal it from unrelated systems.

### I-8

The backend must be able to consume the UI draw list without requiring extra per-widget state reconstruction.

### I-9

The UI library must not allocate per-primitive in the hot path when avoidable.

### I-10

A UI frame must be safe to discard and rebuild on the next tick without leaking resources or state.

### I-11

Text layout should be stable enough that a given string and style produce the same measurements in a frame.

### I-12

If a widget is not visible, it should not contribute unnecessary interaction or draw cost.

## Functional requirements (FR-x)

### FR-1

The library shall provide a frame context object that begins and ends a UI frame.

### FR-2

The library shall provide primitive widgets:

* text label
* button
* checkbox
* radio option
* slider
* drag scalar
* input text
* separator
* image / icon placeholder
* tree node
* collapsing header
* selectable item
* menu item
* popup
* tooltip

### FR-3

The library shall support nested layout regions such as panels, columns, and child areas.

### FR-4

The library shall support an ID stack or equivalent scoped identity mechanism.

### FR-5

The library shall support hover, focus, active, and disabled state queries.

### FR-6

The library shall support keyboard navigation for common editor flows.

### FR-7

The library shall support mouse-based interaction, including click, drag, resize, and selection.

### FR-8

The library shall support drag-and-drop between widgets.

### FR-9

The library shall provide text rendering helpers for wrapping, truncation, and simple measuring.

### FR-10

The library shall provide a draw-list or primitive stream that can be consumed by the renderer backend.

### FR-11

The library shall provide basic style/theme support:

* colors
* spacing
* padding
* font metrics
* widget sizing defaults

### FR-12

The library shall support editor-oriented panels and dock-like arrangements, even if docking is optional in v1.

### FR-13

The library shall support property inspection rows for common types:

* bool
* integer
* float
* string
* enums
* vectors
* handles / IDs

### FR-14

The library shall support gizmo-like handles suitable for transforms and level editing.

### FR-15

The library shall support debug overlays and always-on HUD style windows.

### FR-16

The library shall support clipping and visible rect handling so large trees/lists remain cheap.

### FR-17

The library shall expose a backend interface for the renderer to consume the UI primitives.

### FR-18

The library shall allow the host to query whether the UI wants mouse capture, keyboard capture, or text input capture.

### FR-19

The library shall provide a way to reset or seed the frame state cleanly between scenes.

### FR-20

The library shall make it possible to render multiple UI contexts if the application needs split views or multiple editor panes.

## Non-functional requirements (NFR-x)

### NFR-1

UI frame generation should be low-latency and responsive.

### NFR-2

The common case should avoid heap allocation or reduce it to amortized growth only.

### NFR-3

The draw path should be cache-friendly and batch-friendly.

### NFR-4

The API should be small enough to learn quickly.

### NFR-5

The system should be deterministic enough for repeatable testing and stable editor behavior.

### NFR-6

The library should remain portable and not require a specific windowing toolkit.

### NFR-7

It should be possible to benchmark UI frame generation on synthetic workloads.

### NFR-8

The system should remain usable in both debug and release builds.

### NFR-9

The library should not introduce large hidden per-frame memory churn.

### NFR-10

The renderer backend contract should be simple enough to implement in more than one backend if needed.

## Recommended architecture

### 1. Frame context

A `UIContext` or similar object should hold the per-frame state:

* input snapshot
* layout stack
* widget ID stack
* style reference
* capture state
* draw list buffer
* navigation state

### 2. Stateless-ish widgets

Widgets should be driven by current frame calls, not by heavy retained objects.

Retained state should be limited to what is necessary for:

* focus
* open/closed tree state
* text cursor state
* drag state
* hover state
* per-widget toggle memory when the caller asks for it

### 3. Draw list

Widgets should emit a compact intermediate representation:

* rectangles
* lines
* text
* icons
* textured quads / placeholders
* clipping commands
* z/order or layer hints if needed

### 4. Backend bridge

A backend interface should consume the draw list and produce renderer commands.
The backend should not need to understand widget semantics.

### 5. Layout engine

Use a simple, explicit layout model:

* horizontal and vertical stacking
* row/column helpers
* spacing and padding
* size hints
* clipping regions
* optional grids if needed later

Keep it predictable before making it clever.

## Editor-first priorities

Because `threadmaxx_ui` exists primarily to unblock the editor, the first useful features should be:

* panels
* trees
* property rows
* selection lists
* text input
* menus
* drag and resize handles
* split panes or dock-like regions if practical

## In-game UI priorities

The same library should also be useful for:

* pause menus
* debug overlays
* HUDs
* benchmark selection screens
* simple settings menus

So the API must not be editor-only.

## Test strategy

The library should have focused tests for the core behavior.

### Unit tests

* ID stack stability
* widget activation and hover rules
* focus transition behavior
* drag-and-drop lifecycle
* layout size calculation
* clipping behavior
* tree expand/collapse state
* text wrapping and truncation
* capture flags

### Integration tests

* panel layout with nested widgets
* property inspector on a mixed data set
* keyboard navigation through menus
* mouse interaction on overlapping widgets
* multiple UI frames with stable IDs
* interaction with a fake backend draw-list consumer

### Determinism tests

* same input sequence → same UI state
* same frame order → same draw list
* stable tree open/close behavior
* stable focus behavior across repeated runs

## Bench strategy

`threadmaxx_ui` should be bench-driven where possible.

Useful benchmarks:

* widget creation throughput
* draw list emission throughput
* large tree rendering cost
* large list rendering cost with clipping
* text measurement cost
* panel layout cost
* hover / focus resolution cost
* many-widget editor frame cost

### Bench principles

* measure synthetic UI scenes
* compare clipped vs unclipped lists
* compare retained vs rebuilt state costs where relevant
* measure heap allocations where possible
* keep regressions visible in CI

## Integration notes

### With threadmaxx_input

The UI should consume normalized input, but the input layer should remain separate.

### With threadmaxx_render or renderer backend

The UI should emit a compact draw stream that the renderer can consume efficiently.

### With threadmaxx_reflect

Reflection is optional, but highly useful for property inspectors.

### With threadmaxx_assets

Asset browsing can come later, but the UI should not block on it.

## Suggested implementation order

1. frame context + ID stack
2. layout primitives
3. basic widgets
4. draw list / backend bridge
5. focus and input capture
6. tree / menu / popup support
7. property inspector helpers
8. debug overlay helpers
9. drag handles / simple gizmos
10. optional docking / panel management

## Acceptance criteria

`threadmaxx_ui` is ready when:

* the editor can draw panels, trees, menus, and inspectors
* the game can draw HUDs and debug overlays
* keyboard and mouse interaction are reliable
* the draw list is cheap enough for always-on use
* the API remains small and understandable
* the library has unit tests and benchmark coverage
* it fits the threadmaxx architecture without dragging in unnecessary dependencies

## Future expansion ideas

Potential later additions, only if needed:

* docking
* richer text input
* multi-window support
* layout persistence
* UI theming presets
* icon atlas helpers
* animation / transition helpers
* accessibility improvements
* optional immediate-mode table helpers

## Closing note

`threadmaxx_ui` should feel like a clean, minimal UI layer that is powerful enough for an editor, lightweight enough for debug HUDs, and consistent with the rest of the engine philosophy:

* explicit data flow
* low overhead
* testable behavior
* predictable performance
