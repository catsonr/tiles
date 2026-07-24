# place tiles act 4: first game surface

## implementor context seed

this act is the first visible consumer of the exact core and the mutable engine
assembled in acts 0 through 3-1. it is deliberately a narrow integration act,
not the beginning of a general ui framework. seed the implementation session
from the repository; do not treat this document as a replacement for the
contracts already established there.

before editing:

1. run `git status --short` and preserve every pre-existing user change;
2. enumerate the repository with `rg --files`, excluding generated build output
   and `vendor/`;
3. read this document completely;
4. read `DEFINITIONS.md`, `COORDINATES.md`, and
   `docs/PLACE-TILES-ROADMAP.md` completely;
5. read every previous implementation plan and note file completely:
   - `docs/INITIAL-SCAFFOLD.md`;
   - `docs/INITIAL-SCAFFOLD-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-0-IMPLEMENTATION.md`;
   - `docs/PLACE-TILES-ACT-0-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-1-IMPLEMENTATION.md`;
   - `docs/PLACE-TILES-ACT-1-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-2-IMPLEMENTATION.md`;
   - `docs/PLACE-TILES-ACT-2-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-3-IMPLEMENTATION.md`;
   - `docs/PLACE-TILES-ACT-3-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-3-1-IMPLEMENTATION.md`; and
   - `docs/PLACE-TILES-ACT-3-1-IMPLEMENTATION-NOTES.md`;
6. read every file beneath `src/core/`, `src/engine/`, and `tests/`, paying
   particular attention to:
   - `src/core/geometry/Coordinate.h`;
   - `src/core/geometry/Point.h`;
   - `src/core/geometry/Polygon.h`;
   - `src/core/Placement.h`;
   - `src/core/Arrangement.h`;
   - `src/core/Prototile.h`;
   - `src/engine/Commands.h`;
   - `src/engine/Palette.h`;
   - `src/engine/State.h`;
   - `src/engine/TetrominoState.h`;
   - `tests/test_state.cpp`;
   - the act-3-1 command tests; and
   - `tests/TestHarness.h`;
7. inspect `SConstruct`, `src/register_types.{h,cpp}`,
   `src/nodes/GDExtensionSmokeTest.{h,cpp}`, `godot-project/main.tscn`,
   `godot-project/project.godot`, and `godot-project/tiles.gdextension`;
8. inspect only the narrow generated godot-cpp headers needed to confirm the
   Godot 4.7 `Control`/`CanvasItem` virtual and drawing signatures; do not read
   broadly through `vendor/godot-cpp`; and
9. establish the completed act-3-1 baseline by running:

   ```text
   scons tests
   ./build/tests/tiles_tests
   scons
   Godot --headless --path godot-project --quit
   ```

act 4 consumes the completed act-3-1 command boundary. if
`docs/PLACE-TILES-ACT-3-1-IMPLEMENTATION-NOTES.md` is absent, the act-3-1 tests
are incomplete, or the worktree otherwise shows that act 3-1 is still being
implemented, stop and ask carson rather than guessing at a moving interface.

after implementation, record verified api decisions, exact test totals, build
results, headless runtime output, and any integration detail relevant to later
formalization in:

```text
docs/PLACE-TILES-ACT-4-IMPLEMENTATION-NOTES.md
```

do not commit. do not mention this implementation document in code comments or
runtime-facing documentation.

the implementor must not visually verify the result. do not launch a visible
Godot window, capture a screenshot, or judge layout, color, winding, fill, or
other appearance by eye. carson owns the visual pass. the implementor owns
compilation, automated tests, headless execution, runtime invariants, and clear
reporting of anything which still requires carson's eyes.

## goal

introduce the first Godot-facing `tiles::game` type:

```haskell
data Editor = Editor
  { state :: Maybe tiles::engine::State
  }
```

`tiles::game::Editor` is one `godot::Control` which:

1. boots directly as the application's only scene;
2. constructs the handcrafted tetromino `engine::State`;
3. uses typed `State::apply` commands to place all nineteen distinct
   palette-orientation candidates in a deterministic debug grid;
4. uses the same command boundary to place one additional `o`;
5. attempts a second, deliberately overlapping `o`;
6. receives and verifies the expected typed overlap error without crashing or
   mutating state; and
7. renders every successful authoritative arrangement footprint by projecting
   its ordered q16.48 boundary into Godot screen coordinates.

the resulting vertical slice is:

```text
handcrafted core geometry
          │
          ▼
 engine::make_tetromino_state()
          │
          ▼
 tiles::game::Editor
          │
          ├── typed PlaceCommand ──▶ State::apply
          │                              │
          │                              ▼
          │                      authoritative Arrangement
          │                              │
          └──── read-only state ◀────────┘
                         │
                         ▼
           Point -> godot::Vector2
                         │
                         ▼
             draw_colored_polygon
```

this act proves successful command transport, repeated mutation, legal boundary
contact, transactional failure transport, read-only state observation, and the
one-way rendering projection. it adds no player input.

## preserved architecture

retain the authored one-way dependency direction:

```text
tiles             exact geometry and proof-bearing values
  ▲
  │
tiles::engine     palette-resolved commands and mutable application state
  ▲
  │
tiles::game       Godot lifetime, debug layout, projection, and drawing
```

preserve every completed core and engine contract, including:

- q16.48 `Coordinate` and exact `Point` values;
- cartesian model coordinates with positive y upward;
- validated, simple, hole-free polygon boundaries in cyclic vertex order;
- certified polygon triangulations, although this renderer need not consume
  them;
- canonical oriented polygons compiled once by the core;
- exact placement translations;
- pairwise interior-disjoint arrangements which admit boundary contact;
- monotonic placement identity;
- palette-authored distinct orientation vectors;
- configured supply enforced by the engine;
- transactional `State::apply` commands;
- complete typed engine/core errors;
- read-only `State::palette()` and `State::arrangement()` views;
- `-fno-exceptions` compatibility; and
- the Godot-independent native core-and-engine test executable.

do not add a Godot include, type, callback, signal, or presentation value to
`src/core/` or `src/engine/`. do not register any core value, engine value, or
command with Godot. `Editor` owns an ordinary C++ `engine::State` and invokes it
directly.

all arrangement mutation in this act flows through the act-3-1
`State::apply(const PlaceCommand&)` overload. the game must not receive mutable
access to `Arrangement`, construct arbitrary `Placement` values for insertion,
or call a core mutating verb directly.

## source and scene layout

add:

```text
src/game/Editor.h
src/game/Editor.cpp
```

with:

```cpp
namespace tiles::game {

class Editor : public godot::Control {
    GDCLASS(Editor, godot::Control)

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _draw() override;

private:
    std::optional<engine::State> state_;
};

} // namespace tiles::game
```

private helper functions and small private state may be added when they directly
serve bootstrap diagnostics or drawing. do not split projection, debug layout,
color selection, error formatting, or drawing into frameworks or separately
registered Godot types. keep the act legible from `Editor.{h,cpp}`.

replace the disposable smoke-test surface:

- register `tiles::game::Editor` at scene initialization in
  `src/register_types.cpp`;
- remove the smoke-test include and registration;
- make `godot-project/main.tscn` contain one root `Editor` control which fills
  the viewport;
- delete `src/nodes/GDExtensionSmokeTest.h`; and
- delete `src/nodes/GDExtensionSmokeTest.cpp`.

the smoke node has always been disposable toolchain evidence. do not retain it
as a child, wrapper, base class, compatibility alias, or second scene.

the recursive authored-source discovery in `SConstruct` already includes
`src/game/Editor.cpp`. do not add a special extension source list. do not add
`src/game/` to the native test executable: it must remain independent of
godot-cpp.

## state ownership and fallible bootstrap

`Editor` owns the `engine::State`. because
`engine::make_tetromino_state()` is fallible and `State` has no empty/default
meaning, store it in `std::optional<engine::State>` or an equally direct
non-dangling representation.

in `_ready()`:

1. call `engine::make_tetromino_state()`;
2. inspect the result before accessing its value;
3. on failure, emit one useful Godot error which identifies the bootstrap stage,
   leave the editor without a state, request a redraw if useful, and return
   without crashing;
4. on success, move the complete state into the editor; and
5. populate it only through typed engine commands as specified below.

do not assert handcrafted state construction into existence, throw, terminate,
or call `.value()` on an unexamined failure. do not create a partially valid
fallback palette.

all unexpected debug-layout and placement failures are runtime integration
failures. report their operation, palette-entry index, orientation index where
applicable, and typed error alternative/code. remain non-crashing; do not flatten
the engine interface itself into strings or add a general error framework merely
to print a bootstrap diagnostic.

## debug orientation grid

place every geometrically distinct admitted orientation of every tetromino.
iterate `PaletteEntry::orientations()` itself:

```haskell
placements =
  [ (entryIndex, orientationIndex)
  | entryIndex       <- debugColumnOrder
  , orientationIndex <- [0 .. orientationCount entryIndex - 1]
  ]
```

never iterate an `OrientedPrototile::equivalent_orientations()` group as though
its labels were distinct drawable geometry.

use this debug column order:

```text
i, o, t, s, z, j, l
```

the handcrafted palette's stable indices and ids are:

```text
column  mino  palette entry  PrototileId  distinct orientations
0       i     1              2            2
1       o     0              1            1
2       t     2              3            4
3       s     3              4            2
4       z     4              5            2
5       j     5              6            4
6       l     6              7            4
```

this produces exactly nineteen successful debug-grid commands.

### exact 4 by 4 cells

partition the model-space debug area into exact `4 × 4` game-unit cells. every
tier-1 tetromino orientation has an axis-aligned bounding-box width and height
no greater than four game units.

rows descend on screen. one suitable model-space cell convention for column
`c` and orientation row `r` is:

```text
x in [4c, 4(c + 1)]
y in [-4(r + 1), -4r]
```

derive each oriented candidate's exact axis-aligned bounds from
`OrientedPrototile::canonical_polygon().vertices()`. the candidate's canonical
origin is its lexicographically smallest vertex; it is not promised to be the
lower-left or upper-left corner of its bounding box. therefore, do not assume
that placing at the raw cell corner places the whole polygon inside the cell.

align a consistent candidate bounding-box corner to the corresponding cell
corner. for example, top-left alignment uses:

```text
translation.x = cell_min_x - candidate_min_x
translation.y = cell_max_y - candidate_max_y
```

perform the subtraction through existing checked q16.48 point/coordinate
arithmetic. construct integer game-unit cell coordinates exactly from raw
multiples of `Coordinate::SCALE`; do not quantize a `double`, round, or introduce
a grid type into the engine.

verify during bootstrap that each candidate's exact width and height fit its
four-unit cell. a violated bound or arithmetic failure is an unexpected
integration error: report it and remain non-crashing rather than overlapping
cells deliberately or silently increasing the pitch.

four-unit cell pitch is intentional. neighboring closed cells share boundaries,
and their placed polygons may therefore share complete edges, partial edges, or
points while their interiors remain disjoint. in particular, the top-row
horizontal `i` followed by the `o`, both aligned consistently, creates legal
partial-edge boundary contact. both ordinary exact `PlaceCommand`s must succeed.
do not add a tier-1 drawing path, a contact-specific placement path, redundant
vertices, or a mating command to produce this grid.

## deliberate overlap rejection

after all nineteen debug-grid placements succeed, create a separate overlap
fixture below the grid using palette entry zero (`o`) and distinct orientation
zero.

1. choose an exact translation which is disjoint from the complete debug grid;
2. apply a `PlaceCommand` for the first `o`;
3. inspect and retain its successful `PlacementId`;
4. snapshot the arrangement entry count and `Arrangement::next_id()`;
5. construct a second translation exactly one game unit to the right of the
   first;
6. apply the same `o` candidate at that second translation; and
7. inspect the failure before accessing either result value.

the two `2 × 2` squares then have positive-area interior overlap. the second
command must return:

```text
PlaceCommandError
└── ArrangementError
    ├── code == ArrangementErrorCode::interior_overlap
    └── conflicting_placement == first_o_id
```

verify the complete shape of this error. receiving a candidate error,
`PlacementError`, another `ArrangementErrorCode`, an absent conflict id, the
wrong conflict id, or a success is an unexpected integration failure.

the expected rejection is successful act-4 behavior. report it through a normal
informational print, not `push_error`, `ERR_PRINT`, an assertion, an exception,
or a process failure.

after the rejection, verify:

- the arrangement entry count is unchanged from the snapshot;
- `next_id()` is unchanged from the snapshot;
- the first overlap-fixture `o` is still present;
- no second fixture placement is present; and
- the complete arrangement contains exactly twenty entries: nineteen debug-grid
  placements plus the first fixture `o`.

the rejected candidate is not authoritative arrangement state and must not be
drawn as a ghost, error preview, outline, or second polygon.

## general polygon rendering

`Editor::_draw()` renders the current authoritative arrangement only:

```haskell
drawEditor
  = traverse_ drawPlacement
  . Arrangement::entries
  . State::arrangement
```

if bootstrap did not produce a state, `_draw()` returns without dereferencing
the absent value.

for each arrangement entry:

1. read `entry.placement.footprint().vertices()`;
2. preserve their cyclic order;
3. project each authoritative `tiles::Point` directly to a
   `godot::Vector2`;
4. append the projected values to one `godot::PackedVector2Array`; and
5. call `CanvasItem::draw_colored_polygon(points, color)`.

do not repeat the first vertex at the end of the fill array; the model's closing
edge and Godot's polygon closure are both implicit.

Godot 4.7 `draw_colored_polygon` accepts ordered convex or concave polygons.
use that general contract. do not:

- branch on convexity;
- branch on tetromino or geometry tier;
- draw one triangle at a time;
- invoke the core triangulation;
- invoke `Geometry2D::triangulate_polygon` explicitly;
- create `Polygon2D` child nodes;
- create one Godot node per placement;
- cache a second polygon representation; or
- reconstruct gameplay geometry from projected points.

all current and planned prototiles share the same simple, hole-free polygon
boundary contract. a future target `Region` with holes is not a prototile and
does not justify complicating this tile renderer.

filled colored polygons are the required presentation. an outline, label,
background treatment, grid drawing, animation, or other decoration is not
required by this act. do not spend implementation or verification time on
presentation beyond the specified projection and distinct mino colors.

## model-to-screen projection

the fixed-point model is authoritative. define one game-local, one-way
projection equivalent to:

```haskell
toScreen :: Point -> Vector2
toScreen p =
  screenOrigin
  + pixelsPerUnit * (toReal (p.x), negate (toReal (p.y)))

toReal coordinate =
  double (Coordinate::raw coordinate)
  / double Coordinate::SCALE
```

requirements:

- convert raw q16.48 coordinates through a `double` intermediate;
- divide by `Coordinate::SCALE`;
- multiply by one fixed positive pixels-per-game-unit scale;
- add one fixed screen-space origin;
- negate model y so positive cartesian y points upward while positive Godot
  canvas y points downward;
- narrow to Godot's rendering scalar only at the final `Vector2` construction;
- use the exact same projection for every polygon and geometry tier; and
- keep projection entirely beneath `src/game/`.

a fixed debug scale and origin are sufficient. choose constants which put the
specified `7 × 4` cell grid and lower overlap fixture in a normal project window
without dynamic fitting, zoom, pan, camera state, or responsive layout logic.

this conversion is lossy by design and presentation-only. do not implement an
inverse conversion. no `Vector2`, pixel coordinate, viewport coordinate, or
rendered float may enter an engine command or be quantized back into a
`Coordinate`.

## debug color mapping

color by exact `PrototileId`, so every orientation and placement of one mino
uses the same color. use one direct switch equivalent to:

```cpp
godot::Color color_for(PrototileId p_id) {
    switch (p_id.value()) {
        case 1: return /* o: yellow */;
        case 2: return /* i: cyan */;
        case 3: return /* t: purple */;
        case 4: return /* s: green */;
        case 5: return /* z: red */;
        case 6: return /* j: blue */;
        case 7: return /* l: orange */;
        default: return /* conspicuous fallback */;
    }
}
```

use clear, distinct, opaque Godot colors. exact channel values are not domain
state and need not be configurable. keep the mapping local to the debug game
surface. do not introduce a theme, palette resource, material, texture, color
registry, map, authored decoration model, or engine-side color.

## redraw and runtime diagnostics

request a redraw after `_ready()` finishes its bootstrap attempts. the initial
scene must not require `_process()`, continuous redraw, input, or a timer.

emit concise deterministic runtime diagnostics sufficient for a headless run to
establish:

- the handcrafted state was constructed;
- nineteen orientation-grid placements succeeded;
- the first overlap-fixture placement succeeded;
- the expected second placement was rejected with the correct conflict id;
- the rejected command preserved entry count and `next_id()`; and
- twenty authoritative placements are available to `_draw()`.

it is acceptable to emit one first-draw diagnostic from `_draw()` if needed to
prove that the registered scene reached its drawing path during a multi-frame
headless run. guard it so redraws do not spam the log. do not turn expected
overlap rejection into an error-level diagnostic.

runtime diagnostics supplement typed checks; they do not replace them. do not
print success before inspecting the corresponding `Result`, variant
alternative, error code, conflict id, and post-failure state.

## verification

retain every existing test and its behavior. do not make the native test target
depend on Godot merely to unit-test `Editor`; the integration checks in
`_ready()`, the extension build, and the headless scene run are the act-4 game
verification surface.

run and report:

```text
scons tests
./build/tests/tiles_tests
scons
Godot --headless --path godot-project --quit-after 2
```

if the installed Godot 4.7 executable uses another already-established local
name, use that exact executable and record it. allow enough headless frames for
`_ready()` and `_draw()` to execute before quitting.

the headless run must:

- load the GDExtension without symbol or registration errors;
- instantiate `tiles::game::Editor` as the main-scene root;
- execute the complete bootstrap;
- report the expected overlap rejection as normal behavior;
- report twenty drawable authoritative placements;
- execute the draw path when the headless runtime supports draw notifications;
- contain no unexpected error or warning lines; and
- exit successfully.

headless execution does not verify pixels. the implementor must explicitly hand
off the following unverified visual facts to carson:

- whether all nineteen grid minos are visible;
- whether their shapes and orientation ordering look correct;
- whether the screen-space y direction is correct;
- whether concave fills appear correct;
- whether colors correspond to mino identity;
- whether intended boundary contacts appear edge-to-edge; and
- whether the lower fixture's single accepted `o` is positioned sensibly.

do not open a visible window to answer those questions.

## acceptance

act 4 is complete when all of the following hold:

- `src/game/Editor.{h,cpp}` exists under `namespace tiles::game`;
- `Editor` is a registered `godot::Control`;
- the disposable smoke node is removed;
- `main.tscn` boots directly into one fullscreen `Editor`;
- `Editor` safely owns the fallibly constructed tetromino `engine::State`;
- every one of the nineteen distinct palette orientations enters the
  arrangement through a typed `PlaceCommand`;
- those placements occupy exact four-game-unit debug cells;
- legal complete-edge, partial-edge, or point contact between cells does not
  cause a rejection;
- one separate `o` overlap fixture enters successfully;
- a second `o`, shifted one exact game unit, is rejected with the complete
  expected `ArrangementError`;
- the rejection leaves arrangement entries and id allocation unchanged;
- the final authoritative arrangement contains exactly twenty placements;
- `_draw()` reads only the state's const arrangement view;
- every placement is rendered by one general ordered-boundary
  `draw_colored_polygon` call;
- q16.48 points flow through the one-way y-flipping game projection;
- color is selected by `PrototileId`;
- no rendered coordinate flows back into core or engine state;
- all existing native tests pass;
- the GDExtension builds under its existing flags;
- the main scene completes its headless runtime checks without unexpected
  errors; and
- visual verification is left explicitly to carson.

## explicitly out of scope

act 4 does not implement:

- player, mouse, touch, keyboard, controller, or accessibility input;
- `_gui_input`, placement gestures, or hotkeys;
- screen-to-model conversion;
- snapping policy exposed to a player;
- palette or orientation selection state;
- held tiles, ghost placements, or previews;
- automatic placement, contact, edge, or vertex search;
- full-edge or vertex mating interaction;
- placement hit testing;
- removal, movement, replacement, undo, redo, or persistence;
- per-frame processing;
- zoom, pan, a camera, dynamic fitting, or responsive layout;
- a grid type or grid policy in core or engine;
- a specialized tier-1 renderer;
- explicit triangulation in the renderer;
- nodes per tile;
- render snapshots, caches, meshes, textures, materials, or shaders;
- labels, buttons, panels, menus, toolbars, animation, or presentation polish;
- Godot bindings for core, engine, state, commands, or errors;
- signals, callbacks, observers, controllers, services, repositories, or an
  event bus;
- a title screen or scene-navigation system;
- targets, regions, levels, legality beyond arrangement interior-disjointness,
  solution checking, or campaign flow;
- tier-2 or tier-3 geometry;
- a visual test, screenshot, or implementor visual judgment; or
- changes to existing core or engine behavior.

if implementation reveals that any existing core or engine contract must change
to make this surface work, stop and report the exact conflict to carson. do not
quietly widen act 4.
