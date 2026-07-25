# make an editor act 0: region and level semantics

## implementor context seed

this act introduces exact hole-bearing regions, runtime levels, region-aware
placement legality, direct-placement preview, and exact completion. it changes
the engine from “palette plus arbitrary arrangement” into a constructible legal
play state for one level.

seed the implementation session from the repository. this document fixes the
act-0 contract but does not replace the established domain definitions,
coordinate rules, or completed exact-geometry contracts.

before editing:

1. run `git status --short` and preserve every pre-existing user change;
2. enumerate authored files with `rg --files`, excluding generated build output,
   `Godot.app/`, and `vendor/`;
3. read this document completely;
4. read `README.md`, `DEFINITIONS.md`, `COORDINATES.md`, and
   `docs/make-an-editor/MAKE-AN-EDITOR-ROADMAP.md` completely;
5. read the archived place-tiles roadmap and implementation notes beneath
   `docs/place-tiles/`, especially acts 0, 1, 2, 3, 3-1, 4, and 4-1;
6. read every authored file beneath `src/core/`, `src/engine/`, and `tests/`,
   paying particular attention to:
   - `src/core/Result.h`;
   - `src/core/Arrangement.{h,cpp}`;
   - `src/core/Placement.{h,cpp}`;
   - `src/core/geometry/ExactInteger.{h,cpp}`;
   - `src/core/geometry/Polygon.{h,cpp}`;
   - `src/core/geometry/Predicates.{h,cpp}`;
   - `src/core/geometry/Intersection.{h,cpp}`;
   - `src/engine/Commands.h`;
   - `src/engine/Palette.{h,cpp}`;
   - `src/engine/State.{h,cpp}`;
   - `src/engine/TetrominoState.{h,cpp}`;
   - `tests/test_polygon.cpp`;
   - `tests/test_predicates.cpp`;
   - `tests/test_intersection.cpp`;
   - `tests/test_arrangement.cpp`;
   - `tests/test_commands.cpp`;
   - `tests/test_state.cpp`;
   - `tests/test_tetromino_state.cpp`; and
   - `tests/TestHarness.{h,cpp}`;
7. inspect `src/game/Editor.{h,cpp}` only to preserve its existing build,
   bootstrap, error formatting, and headless behavior;
8. inspect `SConstruct`, `src/register_types.{h,cpp}`,
   `godot-project/main.tscn`, and `godot-project/project.godot`; and
9. establish the baseline by running:

   ```text
   scons tests
   ./build/tests/tiles_tests
   scons
   ./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
   ```

if the baseline does not report the completed place-tiles behavior, the
GDExtension does not load, or the worktree indicates overlapping implementation
work in the files this act needs, stop and ask carson rather than guessing.

after implementation, record exact algorithms, public api decisions, validation
precedence, changed tests, final test totals, build results, and complete
headless output in:

```text
docs/make-an-editor/MAKE-AN-EDITOR-ACT-0-IMPLEMENTATION-NOTES.md
```

do not commit. do not mention this implementation document in code comments or
runtime-facing documentation.

do not launch a visible Godot window. this act has no visual acceptance. native
tests, compilation, and headless execution are the implementor’s verification
surface.

## goal

introduce these exact runtime relationships:

```haskell
data Region = Region
  { outerBoundary   :: Polygon
  , innerBoundaries :: [Polygon]
  }

data Level = Level
  { palette :: Palette
  , region  :: Region
  }

data State = State
  { level       :: Level
  , arrangement :: Arrangement
  }
```

a `Region` is one connected closed polygonal area. its outer polygon contributes
positive area and every inner polygon removes its open interior. every boundary
curve remains part of the region.

a production `State` begins with an empty arrangement. its only placement
mutations resolve candidates from its level palette and prove region
containment, supply, orientation, pairwise interior disjointness, and identifier
availability before insertion. no arbitrary preloaded arrangement can bypass
those proofs.

the new direct-placement preview is the pure counterpart of
`apply(PlaceCommand)`. it is required by the later level player because an empty
level has no placement feature from which a mating proposal can be derived.

completion uses maintained legality plus exact doubled area:

```text
solved(state)
    ⇔ sum(doubledArea(footprint) for footprint in arrangement)
       = doubledArea(level.region)
```

no polygon union, search, rasterization, epsilon, or approximate area enters the
runtime.

## architectural boundary

all new region geometry belongs to `tiles` under `src/core/`. runtime `Level`,
palette ownership, command resolution, and `State` remain in `tiles::engine`.
Godot resources, integer editor snapping, drawing, and interaction belong to
later acts.

the dependency direction remains:

```text
validated Polygon values
          │
          ▼
exact Region and containment
          │
          ▼
engine::Level = Palette × Region
          │
          ▼
engine::State = Level + legal Arrangement
```

`src/core/` and `src/engine/` must remain free of Godot includes, types,
identifiers, and resource concerns.

the editor’s current integer-grid policy does not belong in this act. every
public operation accepts the existing q16.48 polygons and placements, including
fractional raw coordinates.

## exact point location

add one exact polygon point-location operation in a new containment module:

```cpp
enum class PointLocation {
    outside,
    boundary,
    inside,
};

PointLocation locate_point(const Polygon &p_polygon, Point p_point);
```

place it in:

```text
src/core/geometry/Containment.h
src/core/geometry/Containment.cpp
```

the public operation accepts an authoritative q16.48 `Point`.

classification requirements:

- test boundary membership exactly before inside/outside classification;
- a point on a vertex, complete edge, or edge interior is `boundary`;
- use an exact winding-number or equivalent parity algorithm;
- all orientation and horizontal-ray comparisons use integer arithmetic;
- do not divide, construct a floating intersection, cast to `double`, select an
  epsilon, or depend on floating-point rounding mode;
- convexity, winding supplied by the caller, and an axis-aligned boundary must
  not be assumed beyond the existing canonical `Polygon` contract; and
- raw coordinate extrema must not overflow intermediate arithmetic.

reuse `orientation`, `on_segment`, `Int256`, and widened `__int128` differences
where they already express the required proof. do not add a second wide-integer
implementation.

## exact closed-polygon containment

expose:

```cpp
bool contains_closed(
    const Polygon &p_container,
    const Polygon &p_candidate);

bool boundaries_intersect(
    const Polygon &p_lhs,
    const Polygon &p_rhs);
```

`contains_closed(container, candidate)` is true exactly when every closed point
of `candidate` belongs to the closed filled polygon `container`. equality and
all boundary-only contact are contained.

`boundaries_intersect` is true when any boundary segment pair has any
non-disjoint `SegmentRelation`: proper crossing, endpoint touch, or
positive-length collinear overlap. polygon-interior containment with disjoint
boundaries does not make this operation true.

### why vertices and proper crossings are insufficient

do not implement containment as:

```text
all candidate vertices are inside
∧ no boundary pair is a proper crossing
```

a candidate edge may leave and re-enter a concave container exactly through
container vertices. those contacts classify as endpoint touches rather than
proper crossings. likewise, a reflex part of the container boundary may enter
the candidate interior through a tangential boundary contact.

the implementation must classify the open boundary intervals induced by every
such contact.

### required boundary-atom algorithm

implement containment from exact boundary atoms:

1. classify every candidate vertex against the container; any `outside` vertex
   rejects containment;
2. classify every candidate/container boundary-edge pair; any
   `proper_crossing` rejects containment;
3. for each candidate edge, collect:
   - its two endpoints; and
   - every container vertex lying on the closed edge;
4. sort and deduplicate those points in segment order, using the edge’s
   nonconstant raw-coordinate axis;
5. each consecutive distinct pair defines one open candidate-boundary atom;
   classify its exact midpoint against the container;
6. any candidate atom midpoint classified `outside` rejects containment;
7. symmetrically split every container edge at every candidate vertex lying on
   it;
8. classify each resulting open container-boundary atom midpoint against the
   candidate; and
9. any container atom midpoint classified `inside` rejects containment.

if all checks pass, the candidate is contained.

steps 7–9 are load-bearing. they reject concavity cases in which the container
boundary enters the candidate interior without a proper boundary crossing.
do not omit them merely because ordinary rectangles pass without them.

### exact midpoint representation

an atom midpoint need not lie on the q16.48 lattice: two adjacent raw integers
can have a half-raw midpoint. do not round it into a public `Point`.

use one private scaled-point representation inside `Containment.cpp`, with a
positive common denominator:

```haskell
data ScaledPoint = ScaledPoint
  { xNumerator :: signed-wide-integer
  , yNumerator :: signed-wide-integer
  , denominator :: positive-small-integer
  }
```

ordinary q16.48 points have denominator one. an atom midpoint has numerator
`a.raw + b.raw` and denominator two. each sum is formed in `__int128`, never in
signed 64-bit.

provide one private exact point-location implementation over this scaled
representation and route public q16.48 point location through it. orientation
against a scaled point uses:

```text
(b - a) × (pointNumerator - denominator × a)
```

with `__int128` differences and `Int256` products. vertical comparisons for the
winding algorithm compare scaled integers after applying the common
denominator. no rational value is converted to floating point or projected
back onto q16.48.

the only non-input split points required are midpoints. a proper intersection
coordinate never needs to be constructed: proper crossings reject immediately,
and every remaining non-disjoint segment relation has an input endpoint on the
other segment.

document the sufficiency argument in the implementation notes:

- candidate atoms prove no candidate boundary interval enters the exterior;
- container atoms prove no excluded concavity of the container enters the
  candidate interior through a tangential contact; and
- simple closed boundaries plus those two directional tests establish closed
  set containment, including equality and boundary overlap.

if implementation reveals a counterexample to this algorithm, stop and report
it to carson. do not silently weaken containment, add a floating fallback, or
restrict accepted polygons to convex or axis-aligned shapes.

## region value

add:

```text
src/core/Region.h
src/core/Region.cpp
```

with the conceptual surface:

```cpp
enum class RegionErrorCode {
    hole_not_strictly_inside_outer,
    holes_not_disjoint,
};

struct RegionError final {
    RegionErrorCode code;
    std::size_t first_hole;
    std::optional<std::size_t> second_hole;
};

class Region final {
public:
    static Result<Region, RegionError> make(
        Polygon p_outer,
        std::vector<Polygon> p_holes);

    const Polygon &outer_boundary() const;
    const std::vector<Polygon> &inner_boundaries() const;
    const Int256 &doubled_area() const;

    bool contains(const Polygon &p_polygon) const;
};
```

equivalent spelling is permitted only when it preserves every semantic and
error payload below.

`Region` owns its outer polygon and holes by value. access is read-only. no
unchecked public constructor, mutable boundary view, pointer ownership, or
parallel coordinate representation is permitted.

hole order is preserved exactly as authored. it has no geometric meaning, but
it provides deterministic rendering and error identity.

### region validation

validation has fixed precedence:

1. visit holes in authored index order;
2. the first hole which is not strictly inside the outer boundary returns
   `hole_not_strictly_inside_outer`, with `first_hole` populated and
   `second_hole` empty;
3. after every hole passes its outer-boundary check, visit pairs in
   lexicographic `(i, j)` order with `i < j`;
4. the first touching or overlapping pair returns `holes_not_disjoint`, with
   both indices populated; and
5. only after all checks pass may the `Region` be constructed.

a hole is strictly inside the outer polygon exactly when:

```text
contains_closed(outer, hole)
∧ not boundaries_intersect(outer, hole)
```

this rejects a hole outside the outer polygon, crossing it, equal to it,
containing it, sharing an edge, or touching it at a point.

two holes are disjoint exactly when:

```text
not boundaries_intersect(a, b)
∧ not interiors_overlap(a, b)
```

this rejects proper crossing, point contact, partial- or complete-edge contact,
identity, positive-area overlap, and one hole contained in another.

because every validated hole lies strictly inside the outer polygon and hole
interiors are pairwise disjoint, their total area is strictly less than the
outer area. still compute the exact result and assert or defensively reject an
impossible nonpositive result rather than publishing a negative `doubled_area`.
do not add an error variant which public valid polygons can reach only through
an internal arithmetic defect.

### region area

compute once during successful construction:

```text
doubled_area
    = signed_double_area(outer.vertices)
    - sum(signed_double_area(hole.vertices))
```

public polygons are canonical counterclockwise, so each operand is positive.
store the resulting `Int256`; do not recompute it in every `solved()` query.

do not divide by two or convert area to a scalar floating type. doubled area is
the complete comparison value required by this project.

### region containment

`Region::contains(polygon)` is:

```text
contains_closed(outer_boundary, polygon)
∧ for every hole:
      not interiors_overlap(polygon, hole)
```

`interiors_overlap` is precisely the hole exclusion needed here:

- a footprint strictly inside a hole is rejected;
- a footprint containing a hole is rejected;
- a footprint crossing into a hole is rejected;
- positive-area overlap is rejected; and
- point, partial-edge, or complete-edge contact with a hole boundary remains
  legal.

do not reject legal boundary contact by calling `boundaries_intersect` during
placement containment.

## pure insertion preview

add a general pure insertion preview to `Arrangement`:

```cpp
Result<Placement, ArrangementError> preview_insert(
    Placement p_placement) const;
```

it applies the existing insertion rejection precedence:

1. first conflicting placement in storage order; then
2. identifier exhaustion.

on success it returns the supplied placement unchanged by value and mutates
nothing. it predicts no `PlacementId` and reserves no identity.

route `try_insert` through `preview_insert`, then append the returned exact
placement and allocate an identity. route the existing join previews through
the same insertion proof. preserve every completed preview/apply error,
conflicting-id, purity, and immediate-agreement contract.

this refactor creates no new arrangement invariant and must not change accepted
contact semantics.

## engine level

add:

```text
src/engine/Level.h
src/engine/Level.cpp
```

with:

```cpp
class Level final {
public:
    Level(Palette p_palette, Region p_region);

    const Palette &palette() const;
    const Region &region() const;
};
```

both members are already proof-bearing nonempty/valid values, so construction
cannot fail. `Level` owns them by value and exposes read-only views.

the fixed quarter-turn rule does not appear as another member. the palette’s
compiled `PaletteEntry::orientations()` values are the complete runtime rule.
act 0-2 will ensure authored resources request all four quarter turns.

## engine state ownership

change production construction to:

```cpp
explicit State(Level p_level);
```

it always creates an empty valid `Arrangement`.

remove the production constructor which accepts an arbitrary `Palette` and
`Arrangement`. such a constructor can inject placements which are outside the
region, absent from the palette, geometrically inconsistent with a palette id,
in an unoffered orientation, or beyond finite supply. `solved()` cannot be a
theorem if production state admits those bypasses.

retain:

```cpp
const Level &level() const;
const Palette &palette() const;
const Region &region() const;
const Arrangement &arrangement() const;
```

`palette()` and `region()` are convenience views through the owned level.
nothing exposes mutable level, palette, region, or arrangement state.

copy and move construction/assignment may remain ordinary value semantics. act
0-1 will rely on `State` being safely copyable for snapshots.

if an existing identifier-exhaustion engine test requires a state seam which
cannot be reached from a fresh arrangement, add one narrowly named test-only
factory accepting an empty `Arrangement::testing_with_next_id` value. do not
restore a general unchecked production constructor or allow preloaded entries
through that seam.

tests which previously asserted that arbitrary preloaded or foreign placements
are tolerated must be replaced by the new stronger construction invariant.
this is a deliberate contract change, not a compatibility bug.

## region-aware command errors

add:

```cpp
enum class RegionPlacementError {
    outside_region,
};
```

extend errors without flattening existing alternatives:

```cpp
using PlaceCommandError = std::variant<
    CandidateError,
    PlacementError,
    RegionPlacementError,
    ArrangementError>;

using MateCommandError = std::variant<
    CandidateError,
    JoinError,
    RegionPlacementError>;
```

existing core errors remain complete values. do not replace variants with
strings, booleans, log-only failure, exceptions, or a common lossy error enum.

update the temporary Godot error visitor only enough to compile and describe the
new typed alternative. do not add region rendering or new interaction.

## direct-placement preview

add:

```cpp
Result<Placement, PlaceCommandError> State::preview(
    const PlaceCommand &p_command) const;
```

its fixed precedence is:

1. candidate palette entry;
2. candidate distinct orientation;
3. candidate supply;
4. placement footprint construction;
5. region containment; then
6. arrangement overlap and identifier availability.

on success it returns the exact commit-ready `Placement`, consumes no supply,
allocates no identity, and changes no observable state.

route `apply(PlaceCommand)` through this preview and insert its returned
placement. against an otherwise unmodified state:

```text
successful preview
    ⇒ immediate apply succeeds
    ∧ stored geometry equals preview geometry
```

the region check must inspect `placement.footprint()`, never merely its
translation, oriented local polygon, vertices, bounding box, or area.

repeated preview is identical. preview failure preserves state and does not
change completion.

## mating preview and apply

retain the existing candidate and join error precedence inside each mating
preview:

1. palette entry, orientation, and supply;
2. anchor identity;
3. selected feature bounds;
4. feature compatibility and translation/footprint construction;
5. arrangement overlap;
6. identifier availability; then
7. region containment.

the existing `Arrangement::preview_join_*` already performs steps 2–6 and
returns the exact candidate placement. `State::preview` must then apply the
region proof before returning it.

route each mutating mating command through its corresponding state preview,
then insert the returned placement directly. do not call the mutating
`Arrangement::try_join_*` after region preview; that would derive the geometry
twice and leave two command paths to keep synchronized.

map the impossible immediate `try_insert` failure back into the existing
complete `JoinError` representation exactly:

- `ArrangementErrorCode::interior_overlap` becomes
  `JoinErrorCode::interior_overlap` and preserves the conflicting id; and
- `ArrangementErrorCode::identifier_exhausted` becomes
  `JoinErrorCode::identifier_exhausted` with no conflicting id.

preview/apply agreement remains conditional on no intervening mutation.

the fact that mating reports an otherwise available arrangement error before
`outside_region` is intentional and must be tested. do not reorder completed
core join errors or duplicate alignment logic merely to make the placement and
mating precedence visually identical.

## exact completion

add:

```cpp
bool State::solved() const;
```

start from zero `Int256`, add
`signed_double_area(entry.placement.footprint().vertices())` for every
arrangement entry, and compare the exact sum with
`level().region().doubled_area()`.

do not:

- compare tile counts;
- compare bounding boxes;
- compare vertex sets;
- require connected tile adjacency;
- rasterize grid cells;
- construct a union polygon;
- use floating area;
- add a tolerance; or
- search for another arrangement.

the proof is:

1. every constructible `State` begins empty;
2. every insertion preserves region containment;
3. `Arrangement` preserves pairwise interior disjointness;
4. deletion, introduced next, can only remove covered area;
5. therefore summed placement area is exactly arrangement coverage area and
   never exceeds region area; and
6. equal area between finite closed polygonal sets under those invariants leaves
   no missing relative-open subset, hence exact region coverage.

an empty state is never solved because a valid region has positive area.

## tetromino bootstrap compatibility

update `make_tetromino_state()` to produce a valid `Level` with:

- the existing seven-entry unlimited tetromino palette;
- one simple no-hole rectangular debug region large enough to contain every
  existing act-4 bootstrap placement and ordinary act-4-1 interaction near that
  fixture; and
- an initially empty arrangement.

use exact integer game-unit coordinates for this temporary debug region. its
size and identity are not a player-facing level contract. record its bounds in
the implementation notes.

extend `TetrominoStateStage` and its populated optional errors only as needed to
observe construction of the debug outer polygon and region. inspect every
`Result`; do not assert hardcoded geometry into existence.

the existing Godot editor must still build its twenty placements through typed
commands and complete headless bootstrap. no region is rendered in this act.

## native tests

add focused files:

```text
tests/test_containment.cpp
tests/test_region.cpp
tests/test_level.cpp
```

update existing arrangement, state, command, and tetromino tests where their
construction surface changes.

### point-location coverage

test:

- inside, outside, edge-interior, and vertex points for a convex polygon;
- the same classifications around a strongly concave polygon;
- horizontal and vertical edges;
- points whose ray passes through polygon vertices;
- both sides of reflex vertices;
- q16.48 fractional raw coordinates;
- common translations; and
- raw coordinate extrema which force wide intermediates.

### closed-containment coverage

test:

- strict containment;
- identical polygons;
- complete-edge, partial-edge, and point boundary contact;
- a candidate with one outside vertex;
- a proper boundary crossing;
- a candidate strictly containing the proposed container;
- a concavity bridge whose candidate vertices are all inside or on the
  container but whose edge leaves it;
- a reflex-vertex case which crosses excluded space using only endpoint-touch
  segment relations;
- a case rejected only by the symmetric container-atom-inside-candidate test;
- convex and concave candidates;
- fractional raw coordinates;
- operand vertex order independence through canonical polygons; and
- translated and near-limit fixtures.

include at least one regression which would pass an all-vertices plus
no-proper-crossing implementation.

### region-construction coverage

test:

- no holes;
- one and several valid holes;
- preserved hole order;
- a hole outside the outer polygon;
- a hole crossing the outer polygon;
- a hole equal to or containing the outer polygon;
- point and edge contact with the outer boundary;
- two holes with separated boundaries and interiors;
- hole point contact;
- hole edge contact;
- proper crossing;
- positive-area overlap;
- identical holes;
- one hole inside another;
- deterministic first-hole and first-pair error indices; and
- exact outer-minus-holes doubled area.

### region-containment coverage

test:

- footprint strictly in positive area;
- legal outer-boundary point and edge contact;
- footprint partly beyond the outer boundary;
- footprint strictly inside a hole;
- footprint crossing a hole boundary;
- footprint containing a hole;
- legal point, partial-edge, and complete-edge contact with a hole boundary;
- interaction with several holes; and
- concave outer, hole, and footprint shapes.

### arrangement preview coverage

test:

- successful pure direct insertion preview;
- overlap and identifier-exhaustion errors with existing precedence;
- repeated preview identity;
- no id allocation or entry mutation;
- immediate preview/insert agreement; and
- unchanged join-preview behavior after routing through the shared proof.

### level and state coverage

test:

- `Level` owns and exposes its palette and region;
- new `State` owns its level and begins empty;
- no production construction path accepts a preloaded arrangement;
- palette convenience access still names the level palette;
- region convenience access names the level region;
- place preview accepts a contained footprint and is pure;
- place preview rejects outer and hole violations;
- place preview preserves candidate, placement, overlap, and exhaustion
  alternatives and fixed precedence;
- place preview and immediate apply agree exactly;
- failed place apply leaves entries, ids, allocator, supply, and solved state
  unchanged;
- both mating previews reject region violations;
- both mating applies route through state preview;
- mating preserves completed candidate/join precedence before region failure;
- exact boundary contact remains legal;
- empty and partially covered states are not solved;
- one no-hole region becomes solved on its final exact tile;
- one holed region becomes solved on its final exact tile;
- a multi-hole region becomes solved on its final exact tile;
- no earlier contained arrangement with smaller area is solved; and
- fractional q16.48 area equality works without conversion.

construct the holed completion fixture explicitly. for example, a `3 × 3`
outer square with a centered `1 × 1` hole can be covered by eight unit-square
placements. assert `solved() == false` after each of the first seven successful
insertions and `true` after the eighth.

do not weaken or remove unrelated completed tests to preserve the total. update
fixtures to construct a sufficiently large valid level around their existing
placements.

## source and build layout

the expected new authored files are:

```text
src/core/Region.h
src/core/Region.cpp
src/core/geometry/Containment.h
src/core/geometry/Containment.cpp
src/engine/Level.h
src/engine/Level.cpp
tests/test_containment.cpp
tests/test_region.cpp
tests/test_level.cpp
docs/make-an-editor/MAKE-AN-EDITOR-ACT-0-IMPLEMENTATION-NOTES.md
```

expected modifications include:

```text
src/core/Arrangement.h
src/core/Arrangement.cpp
src/engine/Commands.h
src/engine/State.h
src/engine/State.cpp
src/engine/TetrominoState.h
src/engine/TetrominoState.cpp
src/game/Editor.cpp
tests/test_arrangement.cpp
tests/test_commands.cpp
tests/test_state.cpp
tests/test_tetromino_state.cpp
```

small changes to shared test fixtures are permitted when required by the new
level-owned state constructor.

do not change `SConstruct`: recursive production and native-test discovery
already include new `.cpp` files beneath `src/core`, `src/engine`, and `tests`.

do not change GDExtension registration, the main scene, project settings, or
the current visible interface.

## diagnostics

retain existing concise bootstrap and proposal diagnostics. update error
formatting for:

- region construction failures during the temporary tetromino-state bootstrap;
- `RegionPlacementError::outside_region`; and
- any changed state-construction stage.

do not emit one diagnostic per containment edge pair, boundary atom, hole pair,
or successful placement proof. exact geometry belongs in tests and typed
results, not runtime log spam.

the headless bootstrap should continue reporting its existing arrangement,
selection, proposal, and draw-path facts. the new temporary debug region may be
reported once with its outer bounds and hole count.

## verification

run and record:

```text
scons tests
./build/tests/tiles_tests
scons
./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
```

also independently compile any new core/engine translation unit under the
repository’s established c++17 and no-exception assumptions if the normal build
does not make warning provenance clear.

grep-verify that no Godot include, type, resource name, or identifier entered
`src/core/` or `src/engine/`.

the native suite must prove containment and region behavior rather than merely
exercise happy-path rectangles.

the extension build must compile every changed error visitor and state
construction path.

the headless run must:

- load the GDExtension without registration or symbol errors;
- construct the tetromino palette and temporary region;
- build the existing arrangement fixture entirely through legal typed commands;
- preserve selection and proposal initialization;
- reach `_draw()`;
- contain no unexpected error or warning; and
- exit successfully.

## acceptance

act 0 is complete when all of the following hold:

- exact point location distinguishes outside, boundary, and inside;
- point location uses no floating arithmetic or tolerance;
- closed-polygon containment handles arbitrary valid convex and concave
  polygons;
- containment rejects vertex-only and reflex-touch counterexamples;
- containment admits all closed boundary contact;
- a validated `Region` owns one outer polygon and ordered zero-or-more holes;
- every hole is strictly inside and boundary-disjoint from the outer polygon;
- holes neither touch nor overlap one another;
- region errors identify the first offending hole or pair deterministically;
- region doubled area is exact outer area minus exact hole areas;
- region containment admits outer and hole boundary contact but no excluded
  positive area;
- `Arrangement` exposes pure direct insertion preview;
- insertion and both join previews share the existing insertion proof;
- `engine::Level` owns one palette and one region;
- production `State` begins from an empty arrangement and cannot accept
  unchecked preloaded placements;
- state observation remains read-only;
- command errors preserve prior alternatives and add a typed outside-region
  alternative;
- `State::preview(PlaceCommand)` is pure and commit-ready;
- place preview/apply share candidate construction, region proof, overlap proof,
  and error precedence;
- mating preview/apply add region proof without changing earlier join
  precedence;
- every successful state insertion is inside the region;
- `State::solved()` compares exact summed doubled area with exact region doubled
  area;
- exact completion is covered for no-hole, one-hole, and multiple-hole regions;
- the core and engine remain Godot-free;
- all native tests pass;
- the GDExtension builds cleanly;
- the existing headless construction surface still initializes and draws; and
- implementation notes contain the verified algorithm, totals, and commands.

## explicitly out of scope

act 0 does not implement:

- Godot resources or serialization;
- integer-grid enforcement;
- editor region drawing or negative-mode input;
- rendering an outer boundary or holes;
- palette rendering;
- deletion, undo, redo, reset, or history;
- placement hit testing;
- the final direct-placement mouse policy;
- level-player controls or completion presentation;
- multiple disconnected positive regions;
- a `Target` set of regions;
- a hole touching or cutting through the outer boundary;
- holes touching or merging with one another;
- arbitrary polygon boolean union, intersection, or subtraction;
- polygon clipping or construction of intersection coordinates;
- a coverage-union polygon;
- automated tiling search or solvability proof;
- a stored known solution;
- tier-2 or tier-3 geometry;
- non-quarter orientation compilation;
- changes to prototile geometry or placement transforms;
- optimization structures beyond obvious exact bounding-box prefilters;
- a general computational-geometry library; or
- weakening any existing exact-runtime invariant.

if the required containment predicate cannot be implemented exactly with the
specified boundary-atom method, stop and report the smallest counterexample and
the additional exact operation required. do not reinterpret “integer editor
grid” as permission to rasterize the core or restrict public polygons.
