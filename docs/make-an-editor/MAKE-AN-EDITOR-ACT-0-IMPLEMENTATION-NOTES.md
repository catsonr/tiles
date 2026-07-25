# make an editor act 0 — implementation notes

## summary

exact hole-bearing regions, a runtime `Level`, region-aware placement legality,
a pure direct-placement preview, and exact completion are in. the core and
engine remain godot-free, every new predicate is integer-exact, and the existing
godot construction fixture still bootstraps its twenty placements and draws.

one substantive finding is recorded below under **open question for carson**:
the symmetric container-atom test specified in step 7–9 is provably redundant
for hole-free simple polygons, so one required test case ("a case rejected only
by the symmetric container-atom-inside-candidate test") appears to be
unsatisfiable. the code implements the specified algorithm unchanged.

## new files

```text
src/core/geometry/Containment.h
src/core/geometry/Containment.cpp
src/core/Region.h
src/core/Region.cpp
src/engine/Level.h
src/engine/Level.cpp
tests/test_containment.cpp
tests/test_region.cpp
tests/test_level.cpp
```

modified: `src/core/Arrangement.{h,cpp}`, `src/engine/Commands.h`,
`src/engine/State.{h,cpp}`, `src/engine/TetrominoState.{h,cpp}`,
`src/game/Editor.cpp`, `tests/test_arrangement.cpp`, `tests/test_commands.cpp`,
`tests/test_state.cpp`, `tests/test_tetromino_state.cpp`.

`SConstruct`, gdextension registration, `main.tscn`, and `project.godot` are
untouched: recursive discovery picked up every new `.cpp`.

## exact point location

`PointLocation locate_point(const Polygon &, Point)` in
`src/core/geometry/Containment.h`.

the whole module works over one private scaled representation:

```haskell
data ScaledPoint = ScaledPoint
  { x           :: __int128   -- numerator
  , y           :: __int128   -- numerator
  , denominator :: int64      -- 1 for lattice points, 2 for atom midpoints
  }
```

- `exact_point` lifts a q16.48 `Point` with denominator one;
- `exact_midpoint(a, b)` has numerators `a.raw + b.raw` and denominator two,
  both sums formed in `__int128`, never in signed 64-bit;
- `orientation_sign(a, b, p)` computes
  `sign((b - a) × (p_numerator - denominator × a))` with `__int128`
  differences and `Int256` products. the denominator is positive, so scaling
  cannot flip the sign. magnitudes: differences ≤ 2^64, lifted differences
  ≤ 2^65, products ≤ 2^129 — inside `Int256`'s stated range;
- `on_segment_scaled` is `orientation_sign == 0` plus a closed extent test on
  both axes after lifting the segment endpoints by the denominator.

`locate_scaled` then:

1. returns `boundary` if the point lies on any closed edge (tested first, so the
   winding loop never has to reason about an on-edge point);
2. otherwise counts an exact winding number with the half-open convention
   `ay <= py < by` for upward crossings and `by <= py < ay` for downward ones,
   each gated by the exact orientation sign. a vertex the horizontal line passes
   through is therefore counted exactly once across the two edges meeting there,
   and a horizontal edge is never counted;
3. `winding != 0` is `inside`, else `outside`.

public `locate_point` routes through the same implementation with denominator
one, so lattice points and atom midpoints are classified by identical
arithmetic. no division, no floating cast, no epsilon, no constructed
intersection coordinate, no convexity or winding assumption beyond the canonical
`Polygon` contract.

## closed containment

`contains_closed(container, candidate)`, in exactly the specified order:

1. every candidate vertex located against the container; any `outside` rejects;
2. every candidate/container boundary-edge pair classified with
   `classify_segments`; any `proper_crossing` rejects;
3. each candidate edge split at its endpoints plus every container vertex lying
   on the closed edge; split points sorted by the edge's nonconstant raw axis
   (exact integer comparison, since every split point is a lattice point) and
   deduplicated; the exact midpoint of every consecutive distinct pair
   classified against the container; any `outside` rejects;
4. symmetrically, each container edge split at every candidate vertex lying on
   it; each open atom midpoint classified against the candidate; any `inside`
   rejects.

`boundaries_intersect(lhs, rhs)` is true for any edge pair whose
`SegmentRelation` is not `disjoint`.

### sufficiency argument

within one atom the classification is constant. classification along a candidate
edge can only change where the edge meets `∂container`, and every such point is
either a proper crossing (rejected in step 2), a container vertex (a split
point), a candidate edge endpoint (a split point), or an endpoint of a collinear
overlap — whose endpoints are themselves input endpoints of one of the two
segments, hence again split points. so:

- step 2 plus step 3 prove `∂candidate ⊆ closed container`;
- step 4 proves no open interval of `∂container` lies in the candidate's open
  interior;
- a simple closed candidate boundary inside the closed container, with those two
  directional facts, gives closed-set containment including equality and every
  form of boundary overlap.

no intersection coordinate is ever constructed: proper crossings reject
immediately, and every remaining non-disjoint relation already has an input
endpoint on the other segment. the only non-input split values are midpoints,
which is precisely why `ScaledPoint` exists.

### open question for carson

**steps 7–9 (the container-atom direction) cannot reject anything steps 1–6
accept, as long as both operands are hole-free simple polygons.** proof:

suppose steps 1–6 pass, so `∂candidate ⊆ closed container`. suppose some point
`q ∈ ∂container` is strictly inside the candidate. points just outside the
container near `q` are then also strictly inside the candidate. the exterior of
a simple polygon is open, connected, and unbounded, so there is a path from such
a point to infinity lying entirely in the open exterior. the candidate is
bounded, so that path must cross `∂candidate`; but every point of the path is
strictly outside the container while `∂candidate ⊆ closed container` —
contradiction. hence no such `q` exists. ∎

consequences:

- the implementation keeps steps 7–9 exactly as specified. they cost one extra
  `O(n·m)` pass, never produce a false rejection (a container-boundary point
  strictly inside a contained candidate would itself contradict containment),
  and they become genuinely load-bearing the moment the container is a
  hole-bearing set rather than one simple polygon — which is what `Region` will
  eventually want if containment is ever asked about the region directly rather
  than about `outer ∧ ¬holes` as it is today;
- the required test "a case rejected only by the symmetric
  container-atom-inside-candidate test" is, i believe, unsatisfiable. i did not
  weaken containment, add a fallback, or restrict accepted polygons. instead
  `tests/test_containment.cpp` contains
  *"both directional boundary-atom tests agree on the mouth of a concave
  container"*, which pins the agreement of the two directions on the fixture the
  spec had in mind, alongside the two adversarial rejections that a naive
  implementation really does get wrong:
  - *"a concavity bridge with no outside vertex and no proper crossing is
    rejected"* — the mouth rectangle of a concave `C`: every vertex on the
    container boundary, three edges collinear with container edges, the fourth
    meeting the container only at two container vertices. an "all vertices
    inside ∧ no proper crossing" implementation accepts it; the atom test
    rejects it on midpoint `(6, 3)`;
  - *"a reflex bridge crossing excluded space through vertices alone is
    rejected"* — a triangle whose hypotenuse crosses the mouth from one
    container vertex to a reflex vertex, using only endpoint-touch relations.

if you'd rather the symmetric pass be dropped (or kept and documented as
future-proofing, which is what i did), that's a spec decision, not something i
changed unilaterally.

### independent cross-check

beyond the unit tests, i ran a throwaway sampling oracle in the scratchpad (not
committed): for the concave `C` container scaled ×4, every axis-aligned
rectangle and every concave "L" (rectangle minus its top-right cell) on the
integer grid `[-2, 8]²` — 3025 candidates — comparing `contains_closed` against
"no lattice sample point of the candidate is outside the container", sampled at
quarter-cell resolution. **0 mismatches.**

## region

```cpp
Result<Region, RegionError> Region::make(Polygon p_outer, std::vector<Polygon> p_holes);
const Polygon &outer_boundary() const;
const std::vector<Polygon> &inner_boundaries() const;
const Int256 &doubled_area() const;
bool contains(const Polygon &) const;
```

owned by value, read-only access, no unchecked public constructor, hole order
preserved exactly as authored.

validation precedence, as specified:

1. every hole against the outer boundary in authored index order. strictly
   inside is `contains_closed(outer, hole) ∧ ¬boundaries_intersect(outer, hole)`;
   the first failure returns `hole_not_strictly_inside_outer` with `first_hole`
   set and `second_hole` empty;
2. only then pairs in lexicographic `(i, j)`, `i < j`. disjoint is
   `¬boundaries_intersect(a, b) ∧ ¬interiors_overlap(a, b)`; the first failure
   returns `holes_not_disjoint` with both indices;
3. area is computed only after both passes succeed.

`doubled_area = signed_double_area(outer) - Σ signed_double_area(hole)`,
computed once at construction and stored as `Int256`. every operand is positive
because public polygons are canonical counterclockwise. a nonpositive result is
impossible for validated input, so it is an `assert`, not a public error
variant — no error alternative was added that valid polygons could only reach
through an internal defect.

`contains(polygon)` is `contains_closed(outer, polygon) ∧ ∀ hole:
¬interiors_overlap(polygon, hole)`. hole exclusion is deliberately interior
overlap, never `boundaries_intersect`: point, partial-edge, and complete-edge
contact with a hole boundary stays legal.

## arrangement

```cpp
Result<Placement, ArrangementError> preview_insert(Placement) const;
```

the private `reject_insertion` (first conflicting placement in storage order,
then identifier exhaustion) is now reached **only** through `preview_insert`.
`try_insert` calls it and then appends the returned placement and allocates the
id; both `preview_join_full_edges` and `preview_join_vertices` call it and map
`ArrangementError` into the existing `JoinError` representation. no new
invariant, no changed contact semantics, no new error value.

## engine level and state

`engine::Level` is `Palette × Region`, owned by value, read-only, infallible —
both members already carry their own proofs. no rotation-rule member: the
palette entries' compiled distinct orientations are the whole runtime rule.

`State`:

- `explicit State(Level)` is the only production constructor; it always starts
  from an empty `Arrangement`. the old `State(Palette, Arrangement)` is gone;
- `static State testing_with_empty_arrangement(Level, Arrangement)` is the
  narrow test seam. it asserts the arrangement is empty, so it can only preset
  the id allocator (used by the identifier-exhaustion tests);
- `level()`, `palette()`, `region()`, `arrangement()` are all const references;
  `palette()`/`region()` address the level-owned values (a test asserts the
  addresses match — they are views, not copies);
- copy/move remain ordinary value semantics for act 0-1 snapshots.

### errors

```cpp
enum class RegionPlacementError { outside_region };

using PlaceCommandError =
    std::variant<CandidateError, PlacementError, RegionPlacementError, ArrangementError>;
using MateCommandError =
    std::variant<CandidateError, JoinError, RegionPlacementError>;
```

every prior alternative and payload is preserved unchanged.

### direct-placement preview

`Result<Placement, PlaceCommandError> State::preview(const PlaceCommand &) const`
with precedence:

1. palette entry → 2. distinct orientation → 3. supply →
4. `Placement::make` (footprint construction/overflow) →
5. `region().contains(placement.footprint())` →
6. `arrangement_.preview_insert` (overlap, then identifier availability).

`apply(PlaceCommand)` is `preview` followed by `arrangement_.try_insert` of the
returned placement — one derivation, one region proof, one insertion proof. the
region check inspects the authoritative footprint polygon, never the
translation, oriented local polygon, vertex subset, bounding box, or area.

### mating preview and apply

each mating preview keeps the completed core precedence (candidate → anchor →
feature bounds → compatibility/derivation → overlap → identifier) and applies
the region proof **last**, so an available `JoinError` is still reported before
`outside_region`. this ordering is deliberate and is tested
(*"mating reports an available core join error before the region"*).

each mating apply routes through its state preview and inserts the returned
placement directly; the impossible immediate `try_insert` failure is mapped back
to `JoinError` exactly (`interior_overlap` keeps its conflicting id,
`identifier_exhausted` carries none). `Arrangement::try_join_*` is no longer
called from the engine, so mating geometry is derived exactly once.

### completion

```cpp
bool State::solved() const;   // Σ signed_double_area(footprint) == region().doubled_area()
```

accumulated in `Int256` from zero, compared exactly against the stored region
area. no union, search, rasterization, tile count, adjacency requirement,
bounding box, floating area, or tolerance. an empty state is never solved
because a valid region has positive area.

## tetromino bootstrap

`make_tetromino_state()` now builds the seven-entry unlimited palette exactly as
before, plus one temporary no-hole rectangular debug region, and returns
`State(Level(palette, region))` with an empty arrangement.

**debug region bounds: x ∈ [-8, 36], y ∈ [-30, 8] game units, no holes**
(exact whole game-unit coordinates; doubled area `2 · 44 · 38 = 3344` square
game units).

rationale: the orientation grid spans x ∈ [0, 28], y ∈ [-16, 0]; the overlap
fixture sits at y ∈ [-22, -20]; a mated tetromino reaches at most four units
past an anchor. the bounds leave a clear margin around all of it, and the
initial proposal count is unchanged from the act-4-1 baseline (123 offered), so
the region rejects none of the existing interaction. its size and identity are a
construction fixture, not a player-facing level contract.

`TetrominoStateStage` gained `region_outer_polygon` and `region`;
`TetrominoStateError` gained `region_error`. every construction result is
inspected — no geometry is asserted into existence.

## editor (presentation only)

- `describe(RegionPlacementError)` added, wired into `describe_place_error` and
  `report_mate_failure`;
- the two new tetromino stages are described;
- the debug region is reported **once** at bootstrap with its outer bounds and
  hole count. no per-placement, per-edge-pair, per-atom, or per-hole diagnostic
  was added, and nothing renders the region in this act.

## tests

new: `tests/test_containment.cpp` (point location: convex, strongly concave,
horizontal/vertical/diagonal edges, rays through vertices, both sides of both
reflex vertices, fractional q16.48 coordinates, translation invariance, raw
extrema at ±2^62; containment: strict, equality, complete/partial/point contact,
outside vertex, proper crossing, candidate containing container, the two
concavity-bridge regressions, concave candidates and containers, vertex-order
independence, fractional and translated fixtures, near-limit fixtures;
`boundaries_intersect` across every contact kind).

new: `tests/test_region.cpp` (no holes, one and several holes, preserved order,
hole outside/crossing/equal/containing, point and edge contact with the outer
boundary, deterministic first-hole index, outer-check-before-pair-check
precedence, all six pair violations, deterministic lexicographic first pair,
exact areas including a concave outer boundary; containment: positive area,
outer contact, beyond the outer boundary, inside/crossing/containing a hole,
every legal hole-boundary contact, several holes, concave outer/hole/footprint).

new: `tests/test_level.cpp` (level ownership; place preview purity, outer and
hole rejections, full precedence including region-before-arrangement and
supply/overflow-before-region, preview/apply agreement, failed-apply
invariance; both mating previews rejecting region violations and both applies
routing through them; completion for a no-hole region, a one-hole region
(8 tiles), a multi-hole region (13 tiles), a 15/16-covered region, and a
fractional half-unit fixture).

updated:

- `tests/test_arrangement.cpp` — three new cases for `preview_insert` (purity,
  repeated identity, rejection precedence and conflict identity, exhaustion,
  preview/insert agreement);
- `tests/test_commands.cpp` — fixtures now build `State(Level(palette,
  big_region()))` over one ±32 game-unit region so command tests still exercise
  candidates, supply, geometry, and mating rather than the region; the
  identifier-exhaustion case uses the new state seam. the two tests asserting
  that preloaded/foreign placements are tolerated were **replaced** (deliberate
  contract change) by *"supply counts placements whichever command added them"*
  and *"every placement a state holds came from its own palette"*;
- `tests/test_state.cpp` — rewritten around the level-owned state: level
  ownership, convenience views addressing the level's own values, exact region
  area, empty-and-unsolved initial state, static assertions that no
  `(Palette, Arrangement)`, `(Level, Arrangement)`, `(Palette)`, or default
  construction exists, the test seam, and const-only accessors;
- `tests/test_tetromino_state.cpp` — the debug region's shape, bounds, exact
  area, unsolved empty state, and that it contains the debug arrangement extent
  plus its interaction margin but not the space beyond its authored bounds.

no unrelated completed test was weakened or removed.

## verification

```text
$ scons tests
$ ./build/tests/tiles_tests
2204 checks, 0 failed across 250 cases        (baseline before this act: 1738 / 192)

$ scons
scons: `godot-project/bin/macos/libtiles.macos.template_debug.dylib' is up to date.
scons: done building targets.
```

each new translation unit also compiles clean standalone under
`c++ -std=c++17 -fno-exceptions -Wall -Wextra -Wpedantic -Isrc`.

grep over `src/core/` and `src/engine/` finds no godot include, type, resource
name, or identifier (the only hits are the words "invariant" and
`std::variant`).

```text
$ ./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
Godot Engine v4.7.1.stable.official.a13da4feb - https://godotengine.org

[tiles] handcrafted tetromino state constructed: palette order 7
[tiles] debug region: outer bounds x [-8.0, 36.0], y [-30.0, 8.0], 0 holes
[tiles] debug grid: 19 distinct palette orientations placed across 7 columns
[tiles] overlap fixture: second o rejected with interior_overlap against placement 19 (expected)
[tiles] overlap fixture: entry count and next_id unchanged by the rejection
[tiles] bootstrap complete: 20 authoritative placements available to draw
[tiles] selection: palette entry 0 (prototile id 1), orientation 0 of 1
[tiles] proposals (initial): full-edge 512 attempted / 28 succeeded, vertex 512 attempted / 243 succeeded, 168 distinct after collapsing 103 duplicate proofs, 45 dropped as point-only contact, 123 offered
[tiles] active proposal: index 4 of 123, handle (36.0, 40.0), cursor (0.0, 0.0)
[tiles] first draw: 20 footprints projected, selection preview projected, active ghost projected
```

no errors, no warnings, exit success, `_draw()` reached, proposal counts
identical to the act-4-1 baseline. no visible window was launched.

## notes for the next act

- `State` is copyable by value (level + arrangement), which is what act 0-1's
  snapshot history needs;
- `Region::contains` is the only region-legality entry point; deletion in act
  0-1 cannot violate it, so `solved()` stays a theorem;
- the debug region and `make_tetromino_state` are still a fixture. act 0-2's
  resource compiler should replace them rather than grow them;
- if act 0-2 or later ever wants "is this polygon inside this *region*" as a
  single containment call against a hole-bearing set, the symmetric
  container-atom pass in `contains_closed` is the piece that stops being
  redundant. see the open question above.
