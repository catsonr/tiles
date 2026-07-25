# place tiles act 2: lattice orientations and vertex joins

## implementor context seed

this act deliberately changes public contracts established by acts 0 and 1.
before editing, seed the session from the repository itself rather than
inferring those contracts from this document alone.

1. run `git status --short` and preserve every pre-existing user change;
2. enumerate the repository with `rg --files`, excluding generated build output
   and `vendor/`;
3. read this document completely;
4. read `DEFINITIONS.md`, `COORDINATES.md`, and
   `docs/PLACE-TILES-ROADMAP.md` completely;
5. read both previous implementation plans and both implementation-note files:
   - `docs/PLACE-TILES-ACT-0-IMPLEMENTATION.md`;
   - `docs/PLACE-TILES-ACT-0-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-1-IMPLEMENTATION.md`; and
   - `docs/PLACE-TILES-ACT-1-IMPLEMENTATION-NOTES.md`;
6. read every file beneath `src/core/` and `tests/`;
7. inspect `SConstruct` and the minimal GDExtension registration/smoke surface;
   and
8. run `scons tests` and then `./build/tests/tiles_core_tests` before changing
   code, so the baseline binary is known to match the current source.

the repository is intentionally small enough for this complete seed. do not
read through `vendor/godot-cpp` unless a build failure specifically requires
it. do not borrow a rotation, vector, polygon, or transform type from Godot:
this remains a dependency-free plain c++ core act.

after implementation, record verified decisions, reachable and unreachable
error paths, test totals, build results, and any concern for the next
formalizer in:

```text
docs/PLACE-TILES-ACT-2-IMPLEMENTATION-NOTES.md
```

do not commit.

## goal

implement finite exact quarter-turn orientations for polygons already on the
q16.48 lattice, make orientation explicit in every placement, and add
transactional vertex-to-vertex joins alongside the existing full-edge join.

the act begins with the completed act-1 contract:

```text
Prototile { identity, reference polygon }
          + implicit orientation zero
          + translation
          ──────────────────────────────▶ Placement

Arrangement::try_join
          = reference candidate + full-edge alignment + insertion
```

it ends with:

```text
Prototile × finite quarter-turn set
                    │
                    ▼
       distinct OrientedPrototiles
                    │
          selected orientation
                    │
                    ▼
       OrientedPrototile + translation
                    │
                    ▼
                Placement
                    │
           ┌────────┴────────┐
           ▼                 ▼
    full-edge join      vertex join
           └────────┬────────┘
                    ▼
               Arrangement
```

all transformed geometry is derived directly from the prototile's reference
polygon, validated, translation-normalized, and stored as canonical q16.48
geometry before placement. translation remains the only transformation
performed while constructing a footprint.

this act is geometric capability tier 1 from
`docs/PLACE-TILES-ROADMAP.md`. it does not implement tier-2 uniform modules,
45-degree rotation, floating-point rotation, snapping, reflection, scaling,
palettes, targets, or Godot presentation. it preserves the core-wide rule that
all gameplay predicates after model entry are exact.

## authority and supersession

`DEFINITIONS.md` and `COORDINATES.md` remain authoritative.

the act-0 and act-1 plans remain immutable historical documents. this act
explicitly supersedes only their deliberate reference-orientation restrictions:

- act 0's exclusion of orientations other than the reference orientation;
- act 1's `Prototile + Point -> Placement` factory;
- act 1's reference-orientation candidate in full-edge alignment and joining;
  and
- act 1's acceptance requirement that no rotation enter the repository.

the following existing contracts are preserved:

- `Coordinate`, exact checked arithmetic, and the q16.48 interpretation;
- `Polygon` validation, canonical counterclockwise boundary, and certified
  triangulation;
- `Prototile` identity and translation-normalized reference geometry;
- exact whole-footprint interior-overlap classification;
- legal complete-edge, partial-edge, and point boundary contact;
- pairwise interior disjointness for every public `Arrangement`;
- monotonically allocated placement ids;
- transactional insertion and joining;
- value ownership with no dangling geometry; and
- no Godot types in `src/core/`.

## public algebra

the public behavior is equivalent to:

```haskell
makeOrientation
  :: Natural
  -> PositiveNatural
  -> Result Orientation OrientationError

compileLatticeOrientations
  :: Prototile
  -> [Orientation]
  -> Result (NonEmptyList OrientedPrototile) LatticeOrientationError

makePlacement
  :: OrientedPrototile
  -> Point
  -> Result Placement PlacementError

tryJoinFullEdges
  :: Arrangement
  -> PlacementId
  -> EdgeIndex
  -> OrientedPrototile
  -> EdgeIndex
  -> Result PlacementId JoinError

tryJoinVertices
  :: Arrangement
  -> PlacementId
  -> VertexIndex
  -> OrientedPrototile
  -> VertexIndex
  -> Result PlacementId JoinError
```

`try_insert` continues to accept an already-constructed `Placement`.

the lattice compiler accepts only the four quarter turns. the general
`Orientation` representation is intentional: act 3 will add tier-2 orientation
systems with up to twelve turns without replacing placement or arrangement
types.

## source layout

use a separation equivalent to:

```text
src/core/
├── Orientation.h
├── Orientation.cpp
├── OrientedPrototile.h
├── OrientedPrototile.cpp
├── Placement.h
├── Placement.cpp
├── Arrangement.h
├── Arrangement.cpp
└── geometry/
    ├── Rotation.h
    ├── Rotation.cpp
    ├── Alignment.h
    └── Alignment.cpp
```

exact filenames may be consolidated if an existing core boundary becomes
clearer, but retain these conceptual separations:

- rational-turn identity and ordering;
- lattice orientation compilation and deduplication;
- placement from already-oriented canonical geometry;
- pure full-edge and vertex alignment; and
- arrangement mutation through one checked insertion path.

reuse the existing `Result`, `Coordinate`, `Point`, `Polygon`, `Prototile`,
checked arithmetic, exact predicates, and intersection kernel. do not create a
second coordinate representation.

## orientation

`Orientation` is a strong immutable value representing a rational fraction of
one complete counterclockwise turn.

a suitable representation is:

```cpp
class Orientation final {
public:
    using Component = std::uint32_t;

    static Result<Orientation, OrientationError> make(
        Component p_step, Component p_order);

    Component step() const;
    Component order() const;

    friend bool operator==(Orientation p_lhs, Orientation p_rhs);
    friend bool operator!=(Orientation p_lhs, Orientation p_rhs);
    friend bool operator<(Orientation p_lhs, Orientation p_rhs);

private:
    Component step_;
    Component order_;
};
```

use:

```cpp
enum class OrientationError {
    zero_order,
};
```

construction:

1. rejects `p_order == 0`;
2. reduces `p_step` modulo `p_order`;
3. canonicalizes every zero turn to `0/1`; and
4. otherwise divides both components by their greatest common divisor.

therefore:

```text
0/1 == 4/4
1/4 == 2/8
1/2 == 4/8
3/4 == 6/8
```

ordering is angular order within `[0, 1)`. compare normalized fractions with
exact integer cross-products. two `uint32_t` components multiply safely into
`uint64_t`; do not use floating point.

provide legible factories or constants for the reference, quarter, half, and
three-quarter turns if they simplify callers, but those values must still obey
the one canonical representation above.

## lattice orientation set

`compile_lattice_orientations` takes an owned or copied finite sequence of
valid `Orientation` values. the sequence is interpreted as a mathematical set:
input ordering and duplicate angle entries have no meaning.

the compiler:

1. rejects an empty input;
2. sorts the distinct requested orientations into angular order;
3. rejects any orientation other than `0/1`, `1/4`, `1/2`, or `3/4`;
4. compiles every requested orientation directly from the reference polygon;
5. groups compiled results whose canonical polygon boundaries are equal;
6. assigns each group its least requested orientation as representative; and
7. returns groups in representative angular order.

retain the complete sorted nonempty set of requested angles represented by
each group. this keeps deduplication honest and leaves future rotation-rule
work able to distinguish "these angles were admitted but share geometry" from
"this angle was never admitted."

a suitable value is:

```haskell
data OrientedPrototile = OrientedPrototile
  { prototile               :: Prototile
  , orientation             :: Orientation
  , equivalentOrientations  :: NonEmptyList Orientation
  , canonicalPolygon        :: Polygon
  }
```

`orientation` is always the first value of `equivalentOrientations`.

`OrientedPrototile` is value-owning and immutable after construction. its
polygon is canonical local geometry, not a footprint and not world-space
state. do not expose a public constructor capable of pairing arbitrary
geometry with a claimed orientation; only checked compilation may publish the
value.

## polygon equality

orientation deduplication needs exact polygon identity. provide exact equality
for `Polygon`, or an equivalently explicit `same_boundary` operation, with the
contract already permitted by act 0:

```text
two polygons are equal exactly when their canonical vertex sequences are equal
```

triangulation is derived evidence and is not part of equality.

do not use polygon equality to define `Prototile` equality or congruence.
prototile identity, polygon identity, and geometric congruence remain distinct
relations.

## exact quarter-turn transformation

all four transforms operate about the prototile's local origin:

```text
0/1: ( x,  y)
1/4: (-y,  x)
1/2: (-x, -y)
3/4: ( y, -x)
```

coordinate negation is checked. negating `INT64_MIN` returns an arithmetic
overflow; it never wraps, saturates, throws, or invokes undefined behavior.
add a checked coordinate-negation helper if that keeps the transform legible.

for each orientation:

1. read only the reference prototile polygon;
2. transform every reference vertex exactly once;
3. construct the rotated boundary through `Polygon::make`;
4. translation-normalize the rotated polygon through the same checked internal
   operation used by `Prototile::make`;
5. retain the resulting normalized polygon as the oriented canonical polygon;
   and
6. compare its canonical boundary against earlier results for deduplication.

factor the current translation-normalization work into a core-internal helper
shared by `Prototile::make` and the orientation compiler. preserve
`Prototile::make`'s public behavior and error contract. do not duplicate the
checked subtraction, and do not construct a second `Prototile` carrying the
same identity with rotated reference geometry merely to reuse its factory.

rotation preserves simplicity, winding magnitude, and triangulation topology.
after representable coordinate transformation, polygon construction failure is
an invariant backstop rather than expected user input. surface it as a typed
error; never assert a public compilation path into existence.

never derive one compiled orientation from another compiled orientation. even
though quarter turns happen to be exact, the direct-from-reference rule is the
contract required by `COORDINATES.md` and by later tiers.

## compilation errors

use an error value capable of preserving at least:

```cpp
enum class LatticeOrientationErrorCode {
    empty_orientation_set,
    unsupported_orientation,
    coordinate_overflow,
    polygon_construction_failed,
    normalization_overflow,
};

struct LatticeOrientationError final {
    LatticeOrientationErrorCode code;
    std::optional<Orientation> orientation;
    std::optional<PolygonError> polygon_error;
};
```

`orientation` is populated for every failure tied to one requested
orientation. `polygon_error` is populated only for
`polygon_construction_failed`.

if any requested orientation fails, compilation returns only the error and no
partially compiled sequence.

## placement contract

replace the act-1 public factory:

```haskell
makePlacement :: Prototile -> Point -> Result Placement PlacementError
```

with:

```haskell
makePlacement
  :: OrientedPrototile
  -> Point
  -> Result Placement PlacementError
```

do not retain an overload which silently supplies the reference orientation.
callers which want the reference orientation must obtain the compiled
reference `OrientedPrototile` explicitly.

`Placement` owns:

- one immutable `OrientedPrototile`;
- one exact fixed-point translation; and
- one derived immutable footprint polygon.

expose read-only access to:

- the underlying `Prototile`;
- the representative `Orientation`;
- the oriented canonical polygon;
- the translation; and
- the footprint.

footprint construction preserves the act-1 algorithm:

1. checked-add the translation to every oriented canonical vertex;
2. fail on any unrepresentable coordinate;
3. rebuild through `Polygon::make`; and
4. publish only the complete placement.

the footprint is always derived from canonical oriented geometry, never from
another placement or footprint. the stored reference prototile and oriented
canonical polygon remain local and untranslated.

## indices

retain `EdgeIndex` as the strong index into one concrete polygon's canonical
vertex sequence.

add:

```cpp
class VertexIndex final {
public:
    using Value = std::size_t;

    explicit constexpr VertexIndex(Value p_value);
    constexpr Value value() const;
    // exact equality
};
```

`VertexIndex` likewise refers to one concrete polygon's canonical vertex
sequence. rotating and re-normalizing a polygon may change which source corner
occupies an index; callers select indices from the actual oriented polygon they
are joining. stable authored feature identities across orientations are later
catalog work and are not part of this act.

## full-edge alignment

change `align_full_edge` so its candidate is an `OrientedPrototile`, and read
candidate vertices from its oriented canonical polygon.

all remaining act-1 mathematics is unchanged:

```text
anchor footprint edge:      a0 -> a1
oriented candidate edge:    b0 -> b1

compatible iff:
a1 - a0 = b0 - b1

translation:
t = a0 - b1
```

verify both mapped endpoints with checked arithmetic, construct the placement
from the same `OrientedPrototile`, and leave whole-footprint legality to
arrangement insertion.

## vertex alignment

add a pure operation equivalent to:

```haskell
alignVertex
  :: Polygon
  -> VertexIndex
  -> OrientedPrototile
  -> VertexIndex
  -> Result Placement VertexAlignmentError
```

with:

```cpp
enum class VertexAlignmentError {
    anchor_vertex_out_of_range,
    candidate_vertex_out_of_range,
    translation_overflow,
    footprint_overflow,
    footprint_construction_failed,
};
```

given anchor footprint vertex `a` and oriented candidate vertex `b`:

```text
t = a - b
```

derive `t` through checked point subtraction, verify `b + t == a` through
checked point addition, then construct the candidate placement from its
canonical oriented geometry and `t`.

alignment proves only the selected vertex equality. after successful
arrangement insertion, the two footprints may also have:

- no other boundary contact;
- other shared vertices;
- partial shared boundary segments;
- complete shared edges; or
- several disconnected boundary contacts.

all are legal if and only if the existing exact whole-footprint predicate finds
no positive-area interior overlap.

do not add vertex-to-edge alignment, nearest-feature search, edge sliding,
automatic join selection, or corrective vertex movement.

## arrangement joins

rename the act-1 full-edge entry point to make its feature contract explicit:

```haskell
tryJoinFullEdges
  :: Arrangement
  -> PlacementId
  -> EdgeIndex
  -> OrientedPrototile
  -> EdgeIndex
  -> Result PlacementId JoinError
```

add:

```haskell
tryJoinVertices
  :: Arrangement
  -> PlacementId
  -> VertexIndex
  -> OrientedPrototile
  -> VertexIndex
  -> Result PlacementId JoinError
```

do not retain an ambiguous `try_join` alias.

both methods:

1. resolve the anchor placement;
2. validate their feature indices through the pure alignment operation;
3. derive one complete candidate placement;
4. delegate the independent whole-footprint proof to `try_insert`; and
5. publish a placement id only on success.

extend `JoinErrorCode` with:

```text
anchor_vertex_out_of_range
candidate_vertex_out_of_range
```

retain every existing full-edge, footprint, overlap, and identifier error.
translate lower-level typed errors without logging or discarding their
meaning. `conflicting_placement` remains populated only for
`interior_overlap`.

both failed join forms leave entries, ids, ordering, the next-id allocator, and
every placement observably unchanged.

## no reflection

there is no reflection flag, transform, compiler branch, placement operation,
or join option in this act.

the quarter-turn transforms all have positive orientation. canonical polygon
winding normalization must not be confused with reflection: reversing a
clockwise input boundary into counterclockwise storage changes representation,
not geometry.

the lattice compiler operates on one existing `Prototile`; it may only publish
rotations of that prototile. in particular, rotations of the `l` tetromino
never become `j`, and rotations of `s` never become `z`.

this does not make arbitrary mirrored polygon source input impossible:
`Polygon::make` must continue accepting either submitted winding, and a caller
may intentionally author a reflected shape as a distinct prototile. the
contract is that placement and orientation compilation never reflect a
prototile.

## native tests

extend the dependency-free native test executable. update every existing
placement and join test to compile and pass an explicit oriented value; do not
weaken or delete act-0 or act-1 behavioral coverage merely because their public
signatures change.

test helpers may compile a reference-only orientation for concise fixtures.
there is no equivalent implicit convenience in the production API.

### orientation value

- rejection of order zero;
- canonical zero from several whole-turn inputs;
- reduction of equivalent fractions;
- exact inequality and angular ordering;
- distinct quarter, half, and three-quarter turns; and
- input components near their unsigned limits without cross-product overflow.

### lattice compilation

- empty orientation-set rejection;
- exact duplicate angle removal;
- rejection of `1/8`, `1/3`, and other non-quarter turns;
- zero, quarter, half, and three-quarter transforms on an asymmetric polygon;
- positive and negative source coordinates;
- transform overflow from every reachable `INT64_MIN` negation position;
- normalization overflow after a representable transform;
- direct-from-reference results;
- valid reconstructed boundaries and triangulations;
- deterministic angular output order independent of input order; and
- complete equivalent-orientation groups after geometric deduplication.

### tetrominoes

author the seven one-sided tetrominoes as seven distinct prototiles on whole
q16.48 game-unit coordinates:

```text
o, i, t, s, z, j, l
```

each fixture boundary contains only corners at which its direction changes.
omit intermediate collinear cell-boundary points, which are invalid redundant
vertices under the existing `Polygon` contract.

compile all four quarter turns and require:

```text
o: 1
i: 2
t: 4
s: 2
z: 2
j: 4
l: 4
total: 19
```

require these equivalent-orientation partitions:

```text
o:       {0, 1/4, 1/2, 3/4}
i, s, z: {0, 1/2}, {1/4, 3/4}
t, j, l: {0}, {1/4}, {1/2}, {3/4}
```

for every distinct orientation:

- the boundary is a valid simple polygon;
- the triangulation certificate remains valid;
- the exact doubled area equals the `Int256` returned by
  `Int256::multiply(static_cast<__int128>(8) * Coordinate::SCALE, Coordinate::SCALE)`;
- compiling from a cyclic shift or reversed input winding gives the same
  variants; and
- applying the named transform to reference vertices and translation-
  normalizing gives the stored canonical boundary.

require that no `l` orientation equals any `j` orientation and no `s`
orientation equals any `z` orientation.

### placement

- explicit reference-orientation placement;
- a non-reference orientation whose footprint differs from reference;
- exact translated oriented vertices and triangles;
- reachable high-x, high-y, and low-y footprint overflow cases using oriented
  fixtures, plus an explicit test that low-x overflow remains structurally
  unreachable because every oriented local polygon is translation-normalized
  to minimum x equal to zero;
- local reference and oriented polygons remain unchanged; and
- no public placement construction path without an `OrientedPrototile`.

### full-edge joins

retain all act-1 full-edge cases with explicit oriented candidates, then add:

- a join which is incompatible in reference orientation and succeeds after one
  quarter turn;
- exact selected endpoints after a rotated join;
- a mixed-orientation repeated join chain with no raw-coordinate drift; and
- failure preserving the candidate orientation and the arrangement.

### vertex joins

- exact vertex-to-vertex success;
- missing anchor;
- anchor vertex out of range;
- candidate vertex out of range;
- translation overflow;
- footprint overflow;
- a candidate which aligns at the selected vertex but overlaps its anchor;
- a candidate which clears its anchor but overlaps another placement;
- legal isolated point contact;
- legal additional full-edge contact;
- legal additional partial-edge contact;
- legal contact at several boundary features;
- identifier exhaustion; and
- every failure preserving entries, ordering, ids, and allocator state.

### unequal-square constructions

use whole game-unit q16.48 coordinates to construct:

1. a side-2 square at `[0, 2] x [0, 2]`;
2. a unit square at `[2, 3] x [0, 1]`, placed through a selected vertex join;
   and
3. a second unit square at `[2, 3] x [1, 2]`, likewise joined.

require that the unit squares split the large square's right boundary into two
legal partial-edge contacts, all selected vertices agree exactly, and no
interiors overlap. this is the local non-edge-to-edge behavior needed by
pythagorean-style square arrangements; no special partial-edge join exists.

construct a complete `3 x 3` squared-square fixture from:

- one side-2 square covering `[0, 2] x [0, 2]`; and
- five unit squares covering the remaining unit cells.

require pairwise interior disjointness and exact outer corner coordinates
`(0,0)`, `(3,0)`, `(3,3)`, and `(0,3)`. do not implement union, target
coverage, or solution checking merely to assert completion; the fixture proves
exact composability through its known cell decomposition.

## build and integration

all new code remains beneath `src/core/` and is discovered by the existing
recursive SCons walks.

verify:

```sh
scons tests
./build/tests/tiles_core_tests
scons
```

also perform the same bounded headless Godot smoke run used by the preceding
acts:

```sh
./Godot.app/Contents/MacOS/Godot \
  --headless --path godot-project --quit-after 2
```

require:

```text
[tiles] gdextension smoke test ready
```

do not register `Orientation`, `OrientedPrototile`, `VertexIndex`, placement
values, or compiler errors with Godot. the disposable smoke node remains
disposable and unchanged unless a build-compatibility correction is strictly
necessary.

## out of scope

- 45-degree, 30-degree, 60-degree, or arbitrary-angle rotation;
- tier-2 shared direction modules and regular-polygon factories;
- new floating-point or trigonometric rotation, construction, or predicate
  logic; the existing checked `quantize_double` model-entry path is unchanged;
- vertex deformation, runtime snapping, or error budgets;
- reflection or scaling;
- vertex-to-edge, partial-edge, or automatic joins;
- stable authored feature identities across oriented canonicalization;
- palette, supply, and rotation-rule enforcement;
- regions, targets, coverage union, or solution checking;
- geometry serialization or production authoring tools;
- Godot projection, rendering, input, and editor commands; and
- aperiodic coordinate systems or content.

## acceptance

this act is complete when:

1. every public placement owns an explicit compiled orientation;
2. the lattice compiler accepts any nonempty subset of
   `{0, 1/4, 1/2, 3/4}`, rejects every other orientation, and derives every
   result directly from reference geometry;
3. geometrically identical requested orientations are grouped
   deterministically without losing their admitted angle set;
4. every compiled oriented polygon remains validated, triangulated,
   translation-normalized q16.48 geometry;
5. the seven one-sided tetrominoes compile to exactly nineteen distinct fixed
   orientations without reflection;
6. full-edge joins consume oriented candidates and preserve all act-1
   guarantees;
7. vertex joins derive one exact translation and preserve arrangement
   invariants transactionally;
8. unequal-square partial contact and the squared-square fixture compose
   exactly without a special partial-edge join;
9. no reflection, scaling, runtime approximate predicate, runtime snapping,
   tier-2 module, palette, target, or Godot surface enters the implementation;
10. all native tests pass;
11. the GDExtension builds and the bounded headless smoke run succeeds; and
12. `docs/PLACE-TILES-ACT-2-IMPLEMENTATION-NOTES.md` records the verified
    implementation honestly.
