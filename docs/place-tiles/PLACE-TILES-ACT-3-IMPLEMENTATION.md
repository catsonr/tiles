# place tiles act 3: engine state

## implementor context seed

this act is deliberately small, but it sits between the completed exact core
and every future Godot-facing feature. seed the implementation session from the
repository rather than treating this document as a substitute for the existing
contracts.

before editing:

1. run `git status --short` and preserve every pre-existing user change;
2. enumerate the repository with `rg --files`, excluding generated build output
   and `vendor/`;
3. read this document completely;
4. read `DEFINITIONS.md`, `COORDINATES.md`, and
   `docs/PLACE-TILES-ROADMAP.md` completely;
5. read every previous implementation plan and note file:
   - `docs/INITIAL-SCAFFOLD.md`;
   - `docs/INITIAL-SCAFFOLD-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-0-IMPLEMENTATION.md`;
   - `docs/PLACE-TILES-ACT-0-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-1-IMPLEMENTATION.md`;
   - `docs/PLACE-TILES-ACT-1-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-2-IMPLEMENTATION.md`; and
   - `docs/PLACE-TILES-ACT-2-IMPLEMENTATION-NOTES.md`;
6. read every file beneath `src/core/` and `tests/`, paying particular
   attention to:
   - `Prototile`;
   - `Orientation`;
   - `OrientedPrototile`;
   - `Placement`;
   - `Arrangement`;
   - `Result`;
   - `tests/test_tetromino.cpp`; and
   - the test harness;
7. inspect `SConstruct`, the GDExtension registration, and the disposable smoke
   node without changing their behavior; and
8. run `scons tests`, `./build/tests/tiles_core_tests`, and `scons` before
   changing code so the completed act-2 baseline is known to match its source.

do not read through `vendor/godot-cpp` unless a build failure specifically
requires it. act 3 contains no Godot-facing implementation.

after implementation, record verified API decisions, test totals, build
results, and anything act 3-1 must know in:

```text
docs/PLACE-TILES-ACT-3-IMPLEMENTATION-NOTES.md
```

do not commit. do not mention this implementation document in code comments or
runtime-facing documentation.

## goal

introduce the dependency-free `tiles::engine` layer and its first runtime
aggregate:

```haskell
data State = State
  { palette     :: Palette
  , arrangement :: Arrangement
  }
```

act 3 gathers existing proof-bearing core values into the shape needed by the
application:

```text
handcrafted tetromino content
             │
             ▼
       tiles factories
             │
             ▼
 PaletteEntry × 7 ──▶ Palette
                         │
                  empty Arrangement
                         │
                         ▼
              tiles::engine::State
```

`State` is intentionally named for what it contains, not for its first
consumer. an internal editor and a future level player both need a palette and
an arrangement. regions, targets, and level rules may extend or accompany this
state later without requiring an editor-specific rename.

this act gives `State` read-only public views. its purpose is nevertheless to
become the mutable application model. act 3-1 will add typed mutation commands;
act 4 will add `tiles::game`, boot into the editor, read `State`, project its
polygons into Godot coordinates, and render them.

## layer contract

the authored dependency direction is:

```text
tiles             exact core geometry and proof-bearing values
  ▲
  │
tiles::engine     application state built from tiles values
  ▲
  │
tiles::game       future Godot input and presentation
```

use:

```text
src/core/    namespace tiles
src/engine/  namespace tiles::engine
src/game/    namespace tiles::game  (future; do not create in this act)
```

act 3 may include any `src/core/` header needed to compose state. no file
beneath `src/engine/` may include a Godot header, mention a Godot runtime type,
or depend on `src/nodes/`.

the engine exposes authoritative core objects and therefore exposes their
coordinates transitively:

```cpp
state.palette().entries()[i].prototile().polygon().vertices();
state.arrangement().entries()[i].placement.footprint().vertices();
```

those vertices remain q16.48 `tiles::Point`s. the engine must not create a
second polygon, coordinate, vector, render-snapshot, or frontend transfer type.
act 4 owns the lossy `tiles::Point -> godot::Vector2` projection.

## authority and preserved contracts

`DEFINITIONS.md` and `COORDINATES.md` remain authoritative. acts 0 through 2
remain immutable and are not superseded.

preserve:

- every existing core API and namespace;
- the q16.48 coordinate representation;
- validated canonical polygons and their certified triangulations;
- prototile identity;
- orientation compilation and geometric-orientation deduplication;
- orientation-bearing placement;
- pairwise interior-disjoint arrangements;
- transactional core insertion and joining;
- the value-owning, non-dangling core design;
- `-fno-exceptions` compatibility in the GDExtension build; and
- a completely Godot-independent native model test executable.

do not move engine concepts into `namespace tiles`. `Supply`, `PaletteEntry`,
`Palette`, `State`, and the handcrafted state factory belong to
`namespace tiles::engine`.

## public algebra

the public behavior is equivalent to:

```haskell
finiteSupply
  :: PositiveNatural
  -> Result Supply SupplyError

unlimitedSupply
  :: Supply

makePaletteEntry
  :: Prototile
  -> Supply
  -> [Orientation]
  -> Result PaletteEntry LatticeOrientationError

makePalette
  :: [PaletteEntry]
  -> Result Palette PaletteError

makeState
  :: Palette
  -> Arrangement
  -> State

makeTetrominoState
  :: Result State TetrominoStateError
```

the engine publishes no state mutation operation in this act.

## source layout

use a separation equivalent to:

```text
src/engine/
├── Supply.h
├── Palette.h
├── Palette.cpp
├── State.h
├── State.cpp
├── TetrominoState.h
└── TetrominoState.cpp
```

exact filenames may be consolidated when doing so makes a value genuinely
clearer. retain these conceptual separations:

- finite-or-unlimited supply;
- construction and read-only observation of palette entries;
- construction and read-only observation of palettes;
- ownership of palette plus arrangement; and
- the one handcrafted seven-tetromino bootstrap.

do not create a generic framework, service locator, command dispatcher,
observer system, event bus, or abstract engine interface.

## supply

implement the domain value:

```haskell
data Supply
  = Finite PositiveNatural
  | Unlimited
```

a suitable c++ surface is:

```cpp
enum class SupplyError {
    zero_finite_amount,
};

class Supply final {
public:
    using Amount = std::uint64_t;

    static Result<Supply, SupplyError> finite(Amount p_amount);
    static Supply unlimited();

    bool is_unlimited() const;
    std::optional<Amount> finite_amount() const;

    friend bool operator==(Supply p_lhs, Supply p_rhs);
    friend bool operator!=(Supply p_lhs, Supply p_rhs);

private:
    // suitable private representation
};
```

the contract is:

- `finite(0)` fails with `zero_finite_amount`;
- every positive `uint64_t` amount is valid;
- `unlimited()` is distinct from every finite amount;
- `finite_amount()` contains the exact amount for finite supply and is empty
  for unlimited supply; and
- supply is a configured capacity, not a mutable remaining-piece counter.

future remaining supply is derived from the configured supply and the
authoritative arrangement. never store a second decrementing count which can
drift from arrangement contents.

## palette entry

one `PaletteEntry` is one prototile slot in the palette:

```haskell
data PaletteEntry = PaletteEntry
  { prototile    :: Prototile
  , supply       :: Supply
  , orientations :: NonEmptyList OrientedPrototile
  }
```

provide a checked factory equivalent to:

```cpp
class PaletteEntry final {
public:
    static Result<PaletteEntry, LatticeOrientationError> make(
        const Prototile &p_prototile,
        Supply p_supply,
        std::vector<Orientation> p_requested_orientations);

    const Prototile &prototile() const;
    Supply supply() const;
    const std::vector<OrientedPrototile> &orientations() const;

private:
    Prototile prototile_;
    Supply supply_;
    std::vector<OrientedPrototile> orientations_;
};
```

construction delegates orientation work directly to
`compile_lattice_orientations`. do not duplicate its sorting, rejection,
rotation, canonicalization, or deduplication logic.

on success:

- `orientations()` is nonempty;
- every oriented value was compiled from `prototile()`;
- its order is the core compiler's deterministic representative-angle order;
- each element is one geometrically distinct admitted orientation; and
- each element retains its complete `equivalent_orientations()` group.

on compilation failure, return the exact `LatticeOrientationError` unchanged.
do not log, partially construct the entry, or replace the error with a generic
engine failure.

the prototile and its compiled oriented values remain value-owned. the
resulting duplication is consistent with the core's deliberate non-dangling
ownership model; do not introduce shared ownership merely to remove these small
copies.

## palette

`Palette` is a finite, nonempty, authored-order collection of palette entries.
the vector order is presentation order, not geometric meaning; the future game
may display entries exactly in this sequence.

a suitable surface is:

```cpp
enum class PaletteError {
    empty,
    duplicate_prototile_id,
};

class Palette final {
public:
    static Result<Palette, PaletteError> make(
        std::vector<PaletteEntry> p_entries);

    const std::vector<PaletteEntry> &entries() const;
    std::size_t order() const;

private:
    std::vector<PaletteEntry> entries_;
};
```

construction:

1. rejects an empty sequence;
2. rejects any repeated exact `PrototileId`;
3. otherwise preserves the input order exactly; and
4. publishes the complete palette or only one error.

`order()` is:

```text
number of PaletteEntry values
```

it is never:

- the sum of finite supplies;
- the number of raw requested orientation labels; or
- the number of distinct `OrientedPrototile` variants.

levels are handcrafted. act 3 does not search for congruent geometry across
different palette entries. authored content is responsible for not submitting
two rotationally congruent prototiles as distinct entries. duplicate-id
rejection is only a bookkeeping guard and must not grow into a congruence
kernel.

`PaletteEntry` is the engine's compiled runtime materialization of the
information a consumer needs together: one prototile, its configured supply,
and its admitted distinct orientations. act 3 does not separately implement
the eventual domain `RotationRule`.

## state

`State` is the only state-owning engine aggregate introduced by this act:

```cpp
class State final {
public:
    State(Palette p_palette, Arrangement p_arrangement);

    const Palette &palette() const;
    const Arrangement &arrangement() const;

private:
    Palette palette_;
    Arrangement arrangement_;
};
```

it:

- owns both values;
- accepts any already-valid arrangement, including an empty one;
- never reconstructs, normalizes, or copies polygons into another form;
- exposes no mutable reference or pointer; and
- has no editor-specific name or behavior.

construction cannot fail because `Palette` and `Arrangement` are already valid
by construction.

do not make the members `const` and do not describe `State` as permanently
immutable. its act-3 surface is read-only so act 3-1 can introduce one deliberate
typed mutation API without first removing arbitrary mutation paths.

act 3 must not anticipate act 3-1 by adding unrequested forwarding operations,
selection state, held-tile state, revision counters, callbacks, or command
types.

## handcrafted tetromino state

provide:

```cpp
Result<State, TetrominoStateError> make_tetromino_state();
```

this is deterministic handcrafted application content, not procedural
generation and not a core geometry feature.

it builds the seven one-sided tetrominoes in this authored palette order:

```text
o, i, t, s, z, j, l
```

use these stable prototile ids:

```text
o = 1
i = 2
t = 3
s = 4
z = 5
j = 6
l = 7
```

one game unit is `Coordinate::SCALE` raw units. author each reference boundary
with only direction-changing corners:

```text
o: (0,0) (2,0) (2,2) (0,2)

i: (0,0) (4,0) (4,1) (0,1)

t: (0,0) (3,0) (3,1) (2,1)
   (2,2) (1,2) (1,1) (0,1)

s: (0,0) (2,0) (2,1) (3,1)
   (3,2) (1,2) (1,1) (0,1)

z: (1,0) (3,0) (3,1) (2,1)
   (2,2) (0,2) (0,1) (1,1)

j: (0,0) (2,0) (2,3) (1,3) (1,1) (0,1)

l: (0,0) (2,0) (2,1) (1,1) (1,3) (0,3)
```

these rings intentionally agree with the independent act-2 tetromino fixtures.
do not add redundant collinear cell corners.

for each piece:

1. construct its q16.48 points;
2. validate with `Polygon::make`;
3. construct its named `Prototile`;
4. assign `Supply::unlimited()`;
5. request reference, quarter, half, and three-quarter orientations through
   `PaletteEntry::make`; and
6. preserve the authored palette order above.

then construct a `Palette` and a `State` with an empty `Arrangement`.

the result is:

```text
palette order: 7

o: 1 distinct oriented variant
i: 2
t: 4
s: 2
z: 2
j: 4
l: 4

total: 19 distinct oriented variants
```

the familiar seven are seven one-sided tetrominoes. reflection remains
forbidden: `s` and `z` are distinct chiral prototiles, as are `j` and `l`.

the factory is fallible because every public core construction path is checked,
even though its authored constants are expected to succeed. define a typed
error which preserves:

- the construction stage;
- the affected prototile id when the failure belongs to one piece; and
- the complete underlying `PolygonError`, `PrototileError`,
  `LatticeOrientationError`, or `PaletteError`.

a tagged union or a code plus appropriately populated optional fields is
acceptable. do not assert the authored content into existence, discard the
underlying error, log-and-continue, or publish a partial palette.

## no current orientation

act 3 stores the distinct admitted orientations of each palette entry, but it
does not select one.

future cycling operates within one entry:

```text
o: one visible state
i, s, z: two visible states
t, j, l: four visible states
```

do not cycle through the raw `equivalent_orientations()` labels. those labels
describe angles which compile to the same visible geometry. do not add sibling
pointers or a graph; deterministic vector adjacency already represents the
cycle.

selection, a held tile, the current orientation index, mouse state, and key
input belong to later acts.

## build integration

the default GDExtension source discovery already compiles every `.cpp` under
`src/`, so new engine sources must enter the shared library without adding an
engine-specific source list.

extend the native test source discovery from:

```text
src/core + tests
```

to:

```text
src/core + src/engine + tests
```

update the surrounding `SConstruct` comments so the executable is described as
the dependency-free native core-and-engine test suite.

rename its output from:

```text
build/tests/tiles_core_tests
```

to:

```text
build/tests/tiles_tests
```

the `scons tests` alias remains unchanged.

## native tests

retain every existing act-0, act-1, and act-2 test. do not weaken core coverage
or make core test files depend on engine fixtures merely to remove duplicated
tetromino literals.

add engine-focused tests equivalent to:

### supply

- finite zero is rejected;
- finite one succeeds;
- the maximum `uint64_t` finite amount succeeds unchanged;
- unlimited is distinct from every finite supply;
- `finite_amount()` is populated exactly for finite values; and
- equality distinguishes kind and amount correctly.

### palette entry

- empty requested orientations propagate
  `LatticeOrientationErrorCode::empty_orientation_set`;
- an unsupported orientation propagates the complete core error;
- a successful entry retains its prototile id and supply;
- its orientations are nonempty and deterministically ordered; and
- a symmetric piece stores distinct geometry groups rather than four raw angle
  labels.

### palette

- empty input is rejected;
- duplicate exact prototile ids are rejected even when their entry geometry or
  supply differs;
- distinct ids preserve authored order;
- `order()` counts entries rather than supply or oriented variants; and
- failed construction publishes no palette.

do not add general congruence tests or congruence-search code.

### state

- state owns and exposes the supplied palette;
- state owns and exposes an empty arrangement;
- state can own and expose an independently constructed nonempty arrangement;
- polygon vertices remain reachable as exact core points through the read-only
  access chains; and
- callers receive no mutable palette or arrangement reference.

### handcrafted tetromino state

- construction succeeds;
- the arrangement is empty;
- palette order and ids are exactly `o,i,t,s,z,j,l` / `1,2,3,4,5,6,7`;
- every supply is unlimited;
- the per-entry distinct orientation counts are `1,2,4,2,2,4,4`;
- the total distinct orientation count is nineteen;
- representative orientations and equivalent-orientation groups agree with the
  core compiler; and
- every reference and oriented polygon remains valid core geometry.

the existing core tetromino tests already prove the rotation and chirality
kernel. engine tests prove aggregation and content assembly, not a second
rotation implementation.

## acceptance

after implementation, plain c++ can:

```cpp
auto built = tiles::engine::make_tetromino_state();
const tiles::engine::State &state = built.value();

state.palette().order();                              // 7
state.palette().entries()[1].orientations().size();  // 2: i
state.arrangement().entries().size();                // 0

const tiles::Polygon::Vertices &vertices =
    state.palette().entries()[0].prototile().polygon().vertices();
```

and all of the following succeed:

```text
scons tests
./build/tests/tiles_tests
scons
headless Godot smoke load
```

the extension still registers only the existing disposable smoke node. no
engine value is registered with Godot.

## explicitly out of scope

act 3 does not implement:

- any `tiles::game` file or Godot type;
- rendering or coordinate projection;
- nodes, resources, scenes, or registration;
- state mutation commands;
- mutable access to the palette or arrangement;
- placement selection, held-tile state, or current orientation;
- supply consumption or enforcement;
- removal, undo, redo, or persistence;
- regions, targets, rotation-rule values, levels, or solution checking;
- arbitrary palette congruence detection;
- procedural content generation;
- serialized content loading;
- scene navigation, `SceneMachine`, or a title screen;
- tier-2 uniform geometry; or
- changes to existing core behavior.

act 3 is complete when the engine can construct, own, and expose the exact
state which act 3-1 will mutate and act 4 will render.
