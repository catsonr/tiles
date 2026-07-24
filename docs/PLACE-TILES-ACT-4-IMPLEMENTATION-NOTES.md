# place tiles act 4 — implementation notes

Notes from implementing the first `tiles::game` surface: one fullscreen
`Editor` control which owns a handcrafted `engine::State`, populates it only
through typed `PlaceCommand`s, proves the transactional rejection path, and
renders the authoritative arrangement through a one-way lossy projection.

Everything below is verified against the built test executable, the default
GDExtension build, and headless runs. **No visual verification was performed.**
See "handoff to carson" for what still needs eyes.

## verification

- `scons tests` → `./build/tests/tiles_tests` reports **1569 checks, 0 failed
  across 173 cases** — bit-identical to the act-3-1 baseline. No existing test
  was touched, and `src/game/` is deliberately absent from the test target, so
  the native executable remains unaware of godot-cpp.
- `scons` builds the extension cleanly. `src/game/Editor.cpp` and the modified
  `src/register_types.cpp` compile with **no warnings or errors** under
  godot-cpp's flags.
- Independently recompiled `Editor.cpp` with
  `g++ -std=c++17 -fno-exceptions -Wall -Wextra` against the repository and
  godot-cpp headers: **zero warnings originating in `src/game/`** (the only
  diagnostics are `-Wpedantic` complaints about anonymous structs inside
  `vendor/godot-cpp`'s own `vector3.hpp`/`quaternion.hpp`, which predate this
  act).
- Godot executable: `./Godot.app/Contents/MacOS/Godot`, **v4.7.1.stable.official
  (a13da4feb)**. There is no `Godot` on `PATH`; the repository-local app bundle
  is the established local name, matching `docs/INITIAL-SCAFFOLD.md`.
- Headless run — `./Godot.app/Contents/MacOS/Godot --headless --path godot-project
  --quit-after 2` — output in full:

  ```text
  Godot Engine v4.7.1.stable.official.a13da4feb - https://godotengine.org

  [tiles] handcrafted tetromino state constructed: palette order 7
  [tiles] debug grid: 19 distinct palette orientations placed across 7 columns
  [tiles] overlap fixture: second o rejected with interior_overlap against placement 19 (expected)
  [tiles] overlap fixture: entry count and next_id unchanged by the rejection
  [tiles] bootstrap complete: 20 authoritative placements available to draw
  [tiles] first draw: 20 footprints projected
  ```

  No load, symbol, registration, error, or warning line. `--quit` (one frame)
  produces the same six lines and **exit code 0**.
- **The draw path executes headless.** Godot 4.7's dummy renderer still issues
  draw notifications, so `[tiles] first draw: 20 footprints projected` is real
  evidence that `_draw()` ran and projected every footprint — not just that
  `_ready()` finished.
- Grep-verified: no Godot include, type, or identifier anywhere under
  `src/core/` or `src/engine/`; no remaining reference to the smoke node in
  `src/`, `godot-project/`, or `SConstruct`.

## layout

```text
src/game/Editor.h            (new)
src/game/Editor.cpp          (new)
src/register_types.cpp       (modified: registers Editor, drops the smoke node)
godot-project/main.tscn      (modified: one fullscreen Editor root)
src/nodes/GDExtensionSmokeTest.h    (deleted)
src/nodes/GDExtensionSmokeTest.cpp  (deleted)
```

`src/nodes/` is now empty and was removed. `SConstruct` needed **no edit**: the
recursive `src/**` glob already discovers `src/game/Editor.cpp`, and the test
target's explicit `src/core` + `src/engine` + `tests` trees already exclude it.

## verified debug-grid geometry

Derived by replaying the exact bootstrap layout against core+engine in an
out-of-repo probe, so the numbers below are the model's, not an estimate. Model
bboxes are game units; screen bboxes are pixels after the projection.

```text
mino col orient | model x        model y        | screen x       screen y      | verts
i    0   0      | [ 0.0,  4.0]   [ -1.0,   0.0] | [ 60,  156]    [ 40,   64]   | 4
i    0   1      | [ 0.0,  1.0]   [ -8.0,  -4.0] | [ 60,   84]    [136,  232]   | 4
o    1   0      | [ 4.0,  6.0]   [ -2.0,   0.0] | [156,  204]    [ 40,   88]   | 4
t    2   0      | [ 8.0, 11.0]   [ -2.0,   0.0] | [252,  324]    [ 40,   88]   | 8
t    2   1      | [ 8.0, 10.0]   [ -7.0,  -4.0] | [252,  300]    [136,  208]   | 8
t    2   2      | [ 8.0, 11.0]   [-10.0,  -8.0] | [252,  324]    [232,  280]   | 8
t    2   3      | [ 8.0, 10.0]   [-15.0, -12.0] | [252,  300]    [328,  400]   | 8
s    3   0      | [12.0, 15.0]   [ -2.0,   0.0] | [348,  420]    [ 40,   88]   | 8
s    3   1      | [12.0, 14.0]   [ -7.0,  -4.0] | [348,  396]    [136,  208]   | 8
z    4   0      | [16.0, 19.0]   [ -2.0,   0.0] | [444,  516]    [ 40,   88]   | 8
z    4   1      | [16.0, 18.0]   [ -7.0,  -4.0] | [444,  492]    [136,  208]   | 8
j    5   0      | [20.0, 22.0]   [ -3.0,   0.0] | [540,  588]    [ 40,  112]   | 6
j    5   1      | [20.0, 23.0]   [ -6.0,  -4.0] | [540,  612]    [136,  184]   | 6
j    5   2      | [20.0, 22.0]   [-11.0,  -8.0] | [540,  588]    [232,  304]   | 6
j    5   3      | [20.0, 23.0]   [-14.0, -12.0] | [540,  612]    [328,  376]   | 6
l    6   0      | [24.0, 26.0]   [ -3.0,   0.0] | [636,  684]    [ 40,  112]   | 6
l    6   1      | [24.0, 27.0]   [ -6.0,  -4.0] | [636,  708]    [136,  184]   | 6
l    6   2      | [24.0, 26.0]   [-11.0,  -8.0] | [636,  684]    [232,  304]   | 6
l    6   3      | [24.0, 27.0]   [-14.0, -12.0] | [636,  708]    [328,  376]   | 6
o    fixture    | [ 0.0,  2.0]   [-22.0, -20.0] | [ 60,  108]    [520,  568]   | 4
```

Consequences worth recording:

- **Every candidate fits its cell**, and the bootstrap fit check confirms it at
  runtime rather than trusting the table. Widths and heights are all ≤ 4 units.
- **The predicted partial-edge contact is real and legal.** The horizontal `i`
  occupies `x ∈ [0, 4]`, `y ∈ [-1, 0]`; the `o` occupies `x ∈ [4, 6]`,
  `y ∈ [-2, 0]`. They meet along `x = 4` over `y ∈ [-1, 0]` — the `i`'s entire
  right edge lying strictly inside the `o`'s left edge, sharing **no coincident
  polygon vertex**. Both are ordinary `PlaceCommand`s and both succeed. No
  contact-specific path, redundant vertex, or mating command exists.
- **Vertex counts confirm the concave fills are genuinely exercised.** `t`, `s`,
  and `z` are 8-gons; `j` and `l` are 6-gons. Only `i` and `o` are convex
  rectangles, so 15 of the 20 drawn polygons depend on
  `draw_colored_polygon`'s general concave contract.
- **Whole surface: x ∈ [60, 708] px, y ∈ [40, 568] px**, comfortably inside the
  1152 × 648 default window with margin on every side. No dynamic fitting.

## decisions worth recording

- **Projection constants are `PIXELS_PER_UNIT = 24.0`, origin `(60.0, 40.0)`.**
  Chosen so the 7 × 4 grid and the lower fixture fit a default window with the
  margins above. `to_real` divides `Coordinate::raw()` by `Coordinate::SCALE`
  through a `double`; narrowing to `real_t` happens **only** at the final
  `Vector2` construction. Model y is negated exactly once, inside `to_screen`.
- **No inverse projection exists.** There is no screen-to-model function, no
  `Vector2` parameter on any engine call, and no path from a rendered float back
  into a `Coordinate`. This is enforced by absence, not by a comment.
- **Bounding boxes are measured across every vertex, never assumed.** The
  canonical origin is the lexicographically smallest vertex, which for several
  orientations is *not* a bbox corner — the normalized `z`, for instance, has
  vertices at `y = -1` while its lex-smallest vertex sits at the origin. Cells
  are aligned top-left via `cell_min_x - candidate_min_x` and
  `cell_max_y - candidate_max_y`, both through `checked_subtract`.
- **Cell coordinates are built exactly.** `game_units(n)` is
  `Coordinate::from_raw(n * Coordinate::SCALE)`. Nothing quantizes a `double`,
  rounds, or snaps; no grid type entered core or engine.
- **The fit check is a runtime integration check, not an assumption.** Each
  candidate's exact width and height are compared against `4 * SCALE` before
  placement. A violation reports and returns non-crashing rather than
  overlapping cells or widening the pitch.
- **`std::optional<engine::State>` is dereferenced with `operator*`, never
  `.value()`.** `std::optional::value()` has a throwing path that becomes an
  abort under `-fno-exceptions`; every dereference here is guarded by a prior
  `has_value()` check or by assignment earlier in the same function. This
  matches `Result.h`'s deliberate avoidance of throwing accessors.
- **The overlap rejection is verified in full shape, in order.** Success is
  rejected first, then the `ArrangementError` alternative, then
  `interior_overlap`, then the presence of `conflicting_placement`, then its
  equality with the retained first `PlacementId`. Only after all five does the
  informational print fire. Post-failure it re-checks entry count, `next_id()`,
  the first fixture `o`'s continued presence, the absence of any entry at the
  second translation, and the total of twenty. The conflict id is **19** —
  the twentieth allocated id, i.e. the fixture `o` immediately after the
  nineteen grid placements.
- **The rejection uses `print`, not `push_error`.** It is expected act-4
  behavior. Genuine integration failures use `push_error` and return early
  leaving the editor drawable-but-incomplete rather than crashing.
- **Typed errors are stringified only at the presentation boundary.** A handful
  of `const char *describe(...)` overloads plus one `std::get_if` chain over
  `PlaceCommandError` live in `Editor.cpp`'s anonymous namespace. Nothing was
  pushed down into the engine, and no general error framework was added — this
  is exactly the translation layer act 3-1's notes anticipated.
- **`_draw()` builds one `PackedVector2Array` per entry via `push_back` and
  makes one `draw_colored_polygon` call.** The first vertex is not repeated. No
  convexity branch, tier branch, triangulation call, `Geometry2D` call,
  `Polygon2D` node, per-placement node, or cached second representation exists.
  The core triangulation is carried by every footprint and deliberately unused.
- **`equivalent_orientations()` is never iterated.** The grid iterates
  `PaletteEntry::orientations()` and indexes it with `PaletteOrientationIndex`,
  which is what makes the count 19 rather than 28.
- **Redraw is a single `queue_redraw()` per bootstrap outcome**, including the
  failure paths. There is no `_process()`, timer, input handler, or continuous
  redraw. The first-draw diagnostic is guarded by a one-shot `bool`.
- **`main.tscn` is a bare root `Editor` with `anchors_preset = 15`** and full
  anchors so it fills the viewport. No child nodes, no second scene, no wrapper.

## handoff to carson — unverified visual facts

Headless execution does not verify pixels, and I did not open a window. The
following need your eyes. Use the layout table above as ground truth:

- whether all **nineteen** grid minos are visible, in seven columns left to
  right ordered `i, o, t, s, z, j, l`, with orientations descending within each
  column;
- whether each shape *looks* like the tetromino it claims to be, and whether the
  orientation ordering within a column reads sensibly (the engine's order is
  representative-angle order: `0°, 90°, 180°, 270°`, minus deduplicated angles);
- whether the screen y direction is right — the reference (`0°`) orientation of
  every mino should sit in the **top** row, and the fixture `o` at the
  **bottom**;
- whether the concave fills (`t`, `s`, `z`, `j`, `l`) render solid with no
  notches, bowties, or missing wedges — this is the one thing the general
  `draw_colored_polygon` contract could plausibly get wrong;
- whether colors match mino identity: `o` yellow, `i` cyan, `t` purple,
  `s` green, `z` red, `j` blue, `l` orange — and whether any conspicuous
  **magenta** appears, which would mean an unmapped `PrototileId`;
- whether the top-row `i` and `o` appear genuinely **edge-to-edge** with no gap
  and no overlap (they share `x = 4` over `y ∈ [-1, 0]`); and
- whether the lower fixture `o` sits alone and clearly separated below the grid,
  with **exactly one** square there — a second, overlapping square would mean
  the rejected candidate leaked into the draw path.

The colors are debug values picked blind; if any pair reads as too similar on
your display, they are three lines in `color_for` and are not domain state.

## out of scope (correctly absent)

No input of any kind (`_gui_input`, mouse, keyboard, gestures, hotkeys); no
screen-to-model conversion; no snapping policy; no selection, orientation, or
held-tile state; no ghosts or previews; no hit testing; no removal, movement,
undo, redo, or persistence; no `_process`; no zoom, pan, camera, or responsive
layout; no grid type in core or engine; no tier-1 renderer or tier dispatch; no
explicit triangulation; no per-tile nodes, meshes, materials, textures, or
shaders; no labels, panels, outlines, backgrounds, or animation; no Godot
bindings for any core or engine value; no signals, observers, or event bus; no
scene navigation; no targets, regions, or level legality; no tier-2 geometry;
and no change to any existing core or engine behavior.

## notes for the next act

- **The vertical slice is proven end to end and is thinner than it looks.**
  `Editor.cpp` is one file with two bootstrap helpers, one projection, one color
  switch, and one draw loop. Adding input should not require restructuring it —
  but it *will* require deciding where selection state lives, and that decision
  belongs in a spec, not in a helper.
- **Nothing here is reusable as a renderer yet, and that is fine.** The debug
  grid, the fixture, and the fixed projection constants are all bootstrap
  scaffolding. When a real editing surface arrives, the grid and fixture should
  be deleted outright rather than generalized into a layout system.
- **Screen-to-model is the first genuinely new thing input needs**, and it is
  the one direction `COORDINATES.md` and this act both forbid so far. It cannot
  be the inverse of `to_screen`: `to_screen` is lossy, so an inverse would
  quantize a pixel back onto the lattice. Whatever act introduces input has to
  say explicitly what a click *means* in exact terms — a nearest lattice point,
  a nearest legal contact, a cell index — because "unproject the mouse" is not
  a well-defined operation against an exact model.
- **The 19-vs-28 distinction bit nothing here, but it will.** Any future palette
  UI that shows "four rotations" per mino will be wrong for `i`, `o`, `s`, and
  `z`. The truthful count is always `orientations().size()`.
- **`draw_colored_polygon` handled the concave 8-gons without complaint** in the
  headless dummy renderer, but that renderer draws nothing — the concave fill is
  genuinely unverified until carson looks. If it turns out wrong, the fix is
  *not* to reintroduce triangulation in the renderer without a spec change; the
  core already carries a certified triangulation and that would be the obvious
  path, but it contradicts this act's explicit prohibition.
