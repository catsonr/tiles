# place tiles act 3-1: typed engine commands

## implementor context seed

this act is a deliberately small bridge between the dependency-free state
aggregate completed in act 3 and the first Godot-facing game surface planned for
act 4. seed the implementation session from the repository; do not treat this
document as a replacement for the contracts already established there.

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
   - `docs/PLACE-TILES-ACT-3-IMPLEMENTATION.md`; and
   - `docs/PLACE-TILES-ACT-3-IMPLEMENTATION-NOTES.md`;
6. read every file beneath `src/core/`, `src/engine/`, and `tests/`, paying
   particular attention to:
   - `src/core/Arrangement.h` and `.cpp`;
   - `src/core/Placement.h` and `.cpp`;
   - `src/core/OrientedPrototile.h` and `.cpp`;
   - `src/core/Result.h`;
   - `src/core/geometry/Alignment.h` and `.cpp`;
   - `tests/test_state.cpp`;
   - `tests/test_supply.cpp`;
   - `tests/test_palette.cpp`;
   - `tests/test_tetromino_state.cpp`;
   - `tests/test_placement.cpp`;
   - `tests/test_arrangement.cpp`;
   - `tests/test_join.cpp`;
   - `tests/test_vertex_join.cpp`; and
   - `tests/TestHarness.h`;
7. inspect `SConstruct` and the GDExtension registration/smoke-test files
   without changing their behavior; and
8. run `scons tests`, `./build/tests/tiles_tests`, `scons`, and the existing
    headless Godot smoke load before changing code.

do not read through `vendor/godot-cpp` unless a build failure specifically
requires it. act 3-1 contains no Godot-facing implementation.

after implementation, record verified API decisions, test totals, build
results, the headless smoke result, and anything act 4 must know in:

```text
docs/PLACE-TILES-ACT-3-1-IMPLEMENTATION-NOTES.md
```

do not commit. do not mention this implementation document in code comments or
runtime-facing documentation.

## goal

make `tiles::engine::State` the only mutation surface above the exact core:

```haskell
apply :: PlaceCommand
      -> State
      -> Result PlacementId PlaceCommandError

apply :: MateFullEdgesCommand
      -> State
      -> Result PlacementId MateCommandError

apply :: MateVerticesCommand
      -> State
      -> Result PlacementId MateCommandError
```

the game names a candidate through the engine-owned palette. the engine resolves
that candidate, enforces its configured supply from the authoritative
arrangement, and delegates all geometric construction and validation to the
existing core:

```text
typed command from tiles::game
              │
              ▼
     tiles::engine::State
       │      │       │
       │      │       └── derive supply use from Arrangement
       │      └────────── resolve PaletteEntry + oriented variant
       └───────────────── delegate exact work to tiles core
                                    │
                       ┌────────────┼────────────┐
                       ▼            ▼            ▼
                Placement::make  edge mating  vertex mating
                       └────────────┬────────────┘
                                    ▼
                           Arrangement insertion
```

successful commands append one placement and return its `PlacementId`. every
failure preserves the complete observable state. the public palette and
arrangement views remain read-only.

act 3-1 does not add frontend selection state, Godot integration, or rendering.

## preserved architecture

retain the authored dependency direction:

```text
tiles             exact geometry and proof-bearing values
  ▲
  │
tiles::engine     palette-resolved commands and mutable application state
  ▲
  │
tiles::game       future input policy and presentation
```

no file beneath `src/engine/` may include a Godot header, mention a Godot runtime
type, or depend on `src/nodes/`.

preserve every existing core and engine contract, including:

- authoritative q16.48 coordinates;
- validated canonical polygons and certified triangulations;
- value-owned, non-dangling prototiles, oriented prototiles, and placements;
- palette-authored admitted orientations;
- pairwise interior-disjoint arrangements;
- monotonic placement identity;
- transactional core insertion and feature mating;
- exact core error information;
- `-fno-exceptions` compatibility;
- the native dependency-free core-and-engine test executable; and
- the existing handcrafted tetromino state.

do not change any existing core API or behavior merely to make the engine
forwarding code shorter.

## translation and geometry-tier neutrality

`State` is not a tetromino state machine and does not know which geometry tier
compiled a palette.

`PlaceCommand` accepts an exact `tiles::Point`. it must not:

- require either raw coordinate to be a multiple of `Coordinate::SCALE`;
- round, snap, clamp, or otherwise alter the supplied point;
- encode a cell size or integer grid;
- inspect a prototile to infer a translation rule; or
- branch on tier-1, tier-2, tetromino, or other content identity.

the current handcrafted tetrominoes have mino side length exactly one game unit,
and their admitted quarter-turn orientations remain axis-aligned. nevertheless,
the core coordinate lattice permits fractional translations, and the engine
must preserve that generality.

future input snapping belongs to `tiles::game`. if translation constraints later
become part of level legality rather than input behavior, they require an
explicit authored domain rule; they must not be inferred from geometry tier or
coordinate representation.

feature-mating commands derive their exact translations through the core and
therefore remain equally independent of frontend grids and geometry tiers.

## command vocabulary

use concrete command values equivalent to:

```cpp
namespace tiles::engine {

class PaletteEntryIndex final {
public:
    using Value = std::size_t;

    explicit constexpr PaletteEntryIndex(Value p_value);
    constexpr Value value() const;

    friend constexpr bool operator==(
        PaletteEntryIndex p_lhs, PaletteEntryIndex p_rhs);
    friend constexpr bool operator!=(
        PaletteEntryIndex p_lhs, PaletteEntryIndex p_rhs);
};

class PaletteOrientationIndex final {
public:
    using Value = std::size_t;

    explicit constexpr PaletteOrientationIndex(Value p_value);
    constexpr Value value() const;

    friend constexpr bool operator==(
        PaletteOrientationIndex p_lhs, PaletteOrientationIndex p_rhs);
    friend constexpr bool operator!=(
        PaletteOrientationIndex p_lhs, PaletteOrientationIndex p_rhs);
};

struct PlaceCommand final {
    PaletteEntryIndex palette_entry;
    PaletteOrientationIndex orientation;
    Point translation;
};

struct MateFullEdgesCommand final {
    PlacementId anchor;
    EdgeIndex anchor_edge;
    PaletteEntryIndex candidate_entry;
    PaletteOrientationIndex candidate_orientation;
    EdgeIndex candidate_edge;
};

struct MateVerticesCommand final {
    PlacementId anchor;
    VertexIndex anchor_vertex;
    PaletteEntryIndex candidate_entry;
    PaletteOrientationIndex candidate_orientation;
    VertexIndex candidate_vertex;
};

} // namespace tiles::engine
```

implement these names and distinctions:

- palette-entry and palette-orientation indices are strong, non-interchangeable
  types rather than bare `std::size_t`;
- a palette orientation index selects one element of
  `PaletteEntry::orientations()`;
- it never indexes or cycles through an oriented value's
  `equivalent_orientations()` labels;
- core `PlacementId`, `EdgeIndex`, and `VertexIndex` remain the types used for
  their existing meanings; and
- commands contain values only. they own no pointer or reference into `State`.

the strong indices are stable because a `State` never mutates or reorders its
palette.

`mate` is chosen deliberately rather than an unqualified `join`: these commands
derive one candidate translation by aligning selected boundary features. they do
not create or retain an adjacency relation inside `Arrangement`.

## state mutation surface

add overloads equivalent to:

```cpp
class State final {
public:
    // Existing construction and read-only views remain unchanged.
    State(Palette p_palette, Arrangement p_arrangement);
    const Palette &palette() const;
    const Arrangement &arrangement() const;

    Result<PlacementId, PlaceCommandError> apply(
        const PlaceCommand &p_command);

    Result<PlacementId, MateCommandError> apply(
        const MateFullEdgesCommand &p_command);

    Result<PlacementId, MateCommandError> apply(
        const MateVerticesCommand &p_command);

private:
    Palette palette_;
    Arrangement arrangement_;
};
```

the overload set is the command boundary. do not add:

- a mutable palette or arrangement getter;
- a public operation accepting an arbitrary `Placement` or
  `OrientedPrototile`;
- a generic command base class;
- a `std::variant` command dispatcher or visitor;
- queued commands;
- callbacks, signals, observers, or revision counters;
- service, repository, controller, or abstract-engine interfaces; or
- a second state-owning aggregate.

free functions taking `State&` are not preferred because they would require
friend access or a mutable arrangement seam. keep mutation on `State`.

## common candidate resolution

every command resolves its candidate in this order:

1. look up `palette().entries()[palette_entry]`;
2. look up that entry's distinct
   `orientations()[palette_orientation_index]`;
3. derive how many arrangement placements carry the selected entry's exact
   `PrototileId`; and
4. reject the command if the configured finite supply is already exhausted.

out-of-range access must be checked before indexing. do not assert, throw,
clamp, wrap, or silently select another entry or orientation.

candidate resolution uses the palette's stored `OrientedPrototile` directly.
do not reconstruct or recompile orientation geometry.

an arbitrary arrangement supplied to the existing `State` constructor may
contain a prototile absent from the palette because act 3 deliberately accepts
any already-valid arrangement. act 3-1 does not retroactively reject or rewrite
such state. it guarantees that each newly commanded placement comes from the
palette.

if a preloaded arrangement contains the selected `PrototileId`, that placement
counts against supply even if it did not enter through an engine command. supply
identity is determined by exact `PrototileId`, not by geometry comparison,
pointer identity, placement storage order, or orientation.

perform supply counting without an overflow path. a finite maximum
`uint64_t` supply remains valid. do not store a used or remaining counter.

## supply

configured supply remains immutable capacity:

```haskell
used :: PrototileId -> Arrangement -> Natural

available Unlimited _ = True
available (Finite n) usedCount = usedCount < n
```

there is exactly one authoritative source of usage: the arrangement.

therefore:

- a successful command changes derived remaining supply by appending a
  placement;
- a failed command changes nothing;
- a failed geometric operation never consumes supply;
- every orientation of one palette entry shares the entry's supply;
- unlimited supply never exhausts; and
- no decrementing counter, reservation, rollback, or synchronization mechanism
  is introduced.

act 3-1 need not add a public remaining-supply query. the command boundary only
needs to enforce capacity. a later consumer-facing query may be added when a
concrete interface requires it.

## placement command

`PlaceCommand` means:

```text
resolve one palette-authored oriented candidate
              │
              ▼
Placement::make(candidate, exact translation)
              │
              ▼
Arrangement::try_insert(placement)
```

delegate both steps exactly:

1. resolve the candidate and supply;
2. pass the command's `Point` unchanged to `Placement::make`;
3. on placement failure, preserve the exact `PlacementError`;
4. on success, move the placement into `Arrangement::try_insert`;
5. on insertion failure, preserve the complete `ArrangementError`; and
6. on success, return the allocated `PlacementId`.

do not pre-test overlap in the engine. the core arrangement remains the single
whole-footprint insertion proof.

`PlaceCommand` is the complete exact-translation operation. among other cases,
it admits legal partial-edge contact where no pair of polygon vertices
coincides.

## full-edge mating command

after common candidate resolution,
`MateFullEdgesCommand` delegates directly to:

```cpp
arrangement_.try_join_full_edges(
    p_command.anchor,
    p_command.anchor_edge,
    candidate,
    p_command.candidate_edge);
```

the core operation requires the complete selected anchor and candidate edges to
have exactly opposite displacement vectors. it derives the candidate
translation and routes the result through arrangement insertion.

preserve the complete returned `JoinError`, including any conflicting placement
id. do not duplicate:

- anchor lookup;
- edge bounds checks;
- edge compatibility;
- translation derivation;
- footprint construction;
- overlap testing; or
- placement-id allocation.

## vertex mating command

after common candidate resolution, `MateVerticesCommand` delegates directly to:

```cpp
arrangement_.try_join_vertices(
    p_command.anchor,
    p_command.anchor_vertex,
    candidate,
    p_command.candidate_vertex);
```

the core operation aligns the selected vertices exactly and routes the result
through arrangement insertion. the resulting footprints may additionally share
complete edges, partial edges, other vertices, or several disconnected contacts.
the only independent arrangement condition remains absence of positive-area
interior overlap.

preserve the complete returned `JoinError`. do not duplicate core logic.

## contact completeness

the two mating commands are translation-derivation conveniences, not an
exhaustive classification of legal contact.

in particular, suppose one long anchor edge and one shorter candidate edge
overlap strictly inside the anchor edge:

```text
anchor:     o----------------o
candidate:      o------o
```

the contact is legal when the footprints have no interior overlap, but:

- it is not a full-edge mating because the selected edge vectors differ;
- it is not a vertex mating when no candidate vertex coincides with an anchor
  vertex; and
- it remains expressible by `PlaceCommand` with the exact intended translation.

do not add a partial-edge mating kernel, automatic contact search, redundant
collinear polygon vertices, or stored adjacency graph in this act.

## errors

represent engine candidate failures separately from exact core failures. a
suitable surface is:

```cpp
enum class CandidateError {
    palette_entry_out_of_range,
    orientation_out_of_range,
    supply_exhausted,
};

using PlaceCommandError =
    std::variant<CandidateError, PlacementError, ArrangementError>;

using MateCommandError =
    std::variant<CandidateError, JoinError>;
```

the command errors must:

- distinguish all three candidate failures;
- preserve `PlacementError` exactly for placement construction;
- preserve the complete `ArrangementError`, including
  `conflicting_placement`;
- preserve the complete `JoinError`, including `code` and
  `conflicting_placement`;
- compile under `-fno-exceptions`; and
- publish no partial success.

do not flatten core failures into generic strings, log instead of returning,
discard conflict identity, or replace exact error types with Godot-facing
values.

all commands use the same candidate-resolution precedence. if the palette entry
is invalid, return `palette_entry_out_of_range` without inspecting the
orientation or supply. if the entry is valid but its orientation index is not,
return `orientation_out_of_range` without inspecting supply. only a valid
candidate can produce `supply_exhausted`.

## transactional behavior

every failed command leaves all observable state unchanged:

- palette entries and their order;
- arrangement entries and storage order;
- every existing placement and id;
- `Arrangement::next_id()` and exhaustion state; and
- derived supply usage.

candidate lookup and supply derivation are read-only. `Placement::make` is pure.
the existing core insertion and mating operations already own transactional
mutation. use those properties rather than copying `State` before each command
or implementing rollback.

## source layout

use a separation equivalent to:

```text
src/engine/
├── Commands.h       command values, strong indices, and command errors
├── State.h          State views and apply overloads
└── State.cpp        candidate resolution and thin delegation
```

exact filenames may be consolidated if the resulting surface stays equally
clear. do not create a framework directory or split each command into its own
class hierarchy.

the existing recursive source discovery already compiles new engine `.cpp`
files into both the extension and native tests. do not add a special source
list or Godot dependency.

## native tests

retain every existing test. extend the engine state coverage with cases
equivalent to the following.

### strong indices and lookup

- palette-entry and palette-orientation indices retain their exact values;
- they are not implicitly constructible from `std::size_t`;
- they are distinct, non-interchangeable types;
- an out-of-range palette entry returns
  `CandidateError::palette_entry_out_of_range`;
- an out-of-range orientation of a valid entry returns
  `CandidateError::orientation_out_of_range`; and
- failed lookup leaves arrangement entries and `next_id()` unchanged.

### exact placement

- placing a valid palette candidate at the exact origin succeeds with id zero;
- a nonzero exact q16.48 translation is preserved bit-for-bit;
- a deliberately fractional q16.48 translation succeeds when geometry and
  overlap permit, proving that the engine does not impose an integer grid;
- the stored placement has the selected palette prototile id and selected
  representative orientation;
- a second disjoint placement receives the next id;
- interior overlap preserves the complete core `ArrangementError`;
- identifier exhaustion preserves
  `ArrangementErrorCode::identifier_exhausted`;
- placement-coordinate overflow preserves the exact `PlacementError`; and
- every failed placement leaves state and id allocation unchanged.

### supply

- unlimited supply permits repeated successful placement;
- finite supply one permits one placement and rejects the next candidate with
  `supply_exhausted`;
- different orientations of one entry consume the same supply;
- a failed geometric placement does not consume supply;
- placements of a different `PrototileId` do not consume the selected entry's
  supply;
- a matching placement already present in the arrangement passed to the
  `State` constructor consumes supply;
- an arrangement entry absent from the palette is tolerated and left
  unchanged; and
- finite maximum `uint64_t` supply has no counting overflow path.

### feature mating

- full-edge mating resolves its candidate through the palette and succeeds for
  compatible features;
- vertex mating resolves its candidate through the palette and succeeds for
  valid vertices;
- representative full-edge and vertex failures preserve the complete core
  `JoinError`;
- an invalid candidate is rejected before anchor or feature errors are
  inspected;
- exhausted supply is rejected before core mating and leaves state unchanged;
- a failed mating does not consume supply or a placement id; and
- a successful mating returns exactly the id stored in the appended arrangement
  entry.

do not duplicate the core's exhaustive alignment, overlap, or transactional
test matrices in the engine suite. engine tests prove candidate resolution,
supply enforcement, error preservation, and delegation.

### contact semantics

add one focused engine test in which exact placement creates legal partial-edge
contact that neither feature-mating command can derive from the selected
features. prove:

- `PlaceCommand` accepts the intended translation;
- the resulting footprints share boundary without positive-area overlap; and
- no partial-edge-specific engine or core path was required.

use exact authored coordinates; do not introduce a tolerance comparison.

## act-4 handoff

act 4 may own a `State` inside a Godot-facing `tiles::game` object, send these
typed commands into it, and render only through the existing read-only views:

```text
tiles::game input/bootstrap
          │
          └── State::apply(command)

State::palette()     ──▶ palette rendering
State::arrangement() ──▶ arrangement rendering
```

after a successful future edit, the game may request a redraw. act 3-1 adds no
engine notification system.

act 4 owns the lossy `tiles::Point -> godot::Vector2` rendering projection.
neither rendered coordinates nor Godot vectors enter an engine command.
frontend snapping, selection, current orientation, layout, zoom, color, and
input gestures remain game concerns.

## acceptance

plain c++ can perform operations equivalent to:

```cpp
auto built = tiles::engine::make_tetromino_state();
tiles::engine::State state = std::move(built).value();

auto first = state.apply(tiles::engine::PlaceCommand {
    tiles::engine::PaletteEntryIndex(0),
    tiles::engine::PaletteOrientationIndex(0),
    tiles::Point {
        tiles::Coordinate::from_raw(0),
        tiles::Coordinate::from_raw(0),
    },
});

auto second = state.apply(tiles::engine::MateFullEdgesCommand {
    first.value(),
    tiles::EdgeIndex(0),
    tiles::engine::PaletteEntryIndex(0),
    tiles::engine::PaletteOrientationIndex(0),
    tiles::EdgeIndex(2),
});

const tiles::Arrangement &arrangement = state.arrangement();
```

the two selected edges above are opposite complete edges of the o tetromino and
must produce a successful exact mating.

all of the following succeed:

```text
scons tests
./build/tests/tiles_tests
scons
headless Godot smoke load
```

the extension still registers only the disposable smoke node. no command or
engine value is registered with Godot.

## explicitly out of scope

act 3-1 does not implement:

- any file beneath `src/game/`;
- any Godot type, node, resource, scene, signal, input, or rendering behavior;
- `Point -> Vector2` projection;
- integer-grid enforcement, snapping, or cell coordinates;
- geometry-tier dispatch;
- palette mutation or runtime orientation compilation;
- selection, held-tile, current-orientation, mouse, or keyboard state;
- automatic edge, vertex, contact, or placement selection;
- partial-edge mating or contact search;
- stored adjacency or join relationships;
- placement removal, replacement, movement, undo, redo, or persistence;
- public insertion of arbitrary placements or oriented prototiles;
- mutable palette or arrangement access;
- target regions, level legality, or solution checking;
- translation rules;
- tier-2 geometry; or
- changes to existing core behavior.

act 3-1 is complete when all arrangement mutation available above the core flows
through palette-resolved, supply-aware, typed `State` commands while state
observation remains read-only.
