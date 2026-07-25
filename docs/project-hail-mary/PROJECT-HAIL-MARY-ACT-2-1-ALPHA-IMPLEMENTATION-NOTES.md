# project hail mary act 2-1 alpha — implementation notes

Notes from adding one pure exact compiler which turns an arrangement's coverage
into one checked region. Three new files, nothing else touched.

## verification

| step | result |
| --- | --- |
| `scons tests` | builds clean |
| `./build/tests/tiles_tests` | **9921 checks, 0 failed across 427 cases** |
| standalone c++17 / `-fno-exceptions` / `-Wall -Wextra -Wpedantic` | both new translation units, no warnings |
| `scons` | GDExtension builds, no warnings or errors |
| `res://tests/resource_integration.tscn` | **270 checks passed** (unchanged) |
| `res://tests/level_editor_integration.tscn` | **151 checks passed** (unchanged) |
| ordinary headless load | `[tiles] level editor ready: 37 canonical identities, choose lattice or hex12`, zero error lines |
| `git diff --check` | clean |

Baselines re-established before editing, and what they are now:

```text
native                9801 / 0 / 407  →  9921 / 0 / 427
resource integration  270 checks      →  270 (unchanged)
level editor          151 checks      →  151 (unchanged)
ordinary run          37 identities   →  37 identities (unchanged)
GDExtension build     clean           →  clean
```

`407 + 20 = 427` cases and `9801 + 120 = 9921` checks: every new case is in
`tests/test_arrangement_region.cpp`. No existing test, header, or source was
edited, renamed, weakened, or removed. `SConstruct` needed no change — both
globs discovered the new files, so `src/core/ArrangementRegion.cpp` is compiled
into the shared library as well as the test binary, and it stays Godot-free.

The worktree was clean on arrival and no commit was made.

## files

New, and the complete diff:

```text
src/core/ArrangementRegion.h
src/core/ArrangementRegion.cpp
tests/test_arrangement_region.cpp
```

`region_from_arrangement` has no caller anywhere in `src/`. It is not reachable
from `LevelEditor`, `Session`, `State::is_solved()`, `compile_level_resource`,
or either headless runner; runtime completion is still the exact doubled-area
comparison it was. Beta and gamma are its first consumers.

No Godot include, type, resource name, or identifier entered `src/core/`. The
only match for `Godot` under `src/core/` is a pre-existing sentence in
`ExactInteger.h`.

## the public surface

```cpp
struct SourceEdgeRef { std::size_t placement; std::size_t edge; };
struct CanonicalSegment { Point lesser; Point greater; };

struct BoundaryCrossing { SourceEdgeRef first, second; };
struct SegmentMultiplicity { CanonicalSegment segment;
                             std::vector<SourceEdgeRef> contributing_edges; };
struct PolygonConstructionFailure { std::size_t cycle; PolygonError error; };
struct AreaMismatch { Int256 footprint_doubled_area, region_doubled_area; };
struct NonmanifoldVertex { Point point; std::size_t indegree, outdegree; };

enum class ArrangementRegionInvariantFailureCode { ... };   // the nine kinds
struct ArrangementRegionInvariantFailure { code + one populated alternative };

enum class ArrangementRegionErrorCode {
    empty_arrangement, nonmanifold_boundary_vertex,
    disconnected_coverage, internal_invariant_failure,
};
struct ArrangementRegionError {
    ArrangementRegionErrorCode code;
    std::optional<NonmanifoldVertex> nonmanifold_vertex;
    std::vector<Point> component_points;
    std::optional<ArrangementRegionInvariantFailure> invariant_failure;
};

Result<Region, ArrangementRegionError> region_from_arrangement(const Arrangement &);
```

The representation choice the act left open is **optional whole payloads**,
matching `ArrangementError` and `BlueprintCompilationError` elsewhere in the
tree. Each payload is a struct, so it is present in full or absent in full:
there is no way to read a nonmanifold degree pair without its point, or a
polygon error without its cycle index, and no code can wear another's evidence.
Every payload owns its geometry by value; nothing returned holds a pointer,
reference, iterator, or view into the arrangement or into any intermediate.

`SourceEdgeRef` is `arrangement storage index × footprint polygon edge index`,
where edge `k` of an `n`-vertex footprint runs from vertex `k` to vertex
`(k + 1) mod n`. It is deliberately not a `PlacementId`.

## the algorithm, and where each spec step lives

One file, one pass, numbered comments matching the act:

```text
collect_source_edges            1   storage order, then polygon edge order
collect_split_points            2   O(e^2) exact pair classification
emit_atomic_segments            3   raw-scalar ordering along each directed edge
cancel_internal_segments        4   undirected key, 1 retained / 2 opposed cancel
boundary_vertices               -   sorted vertex table, indexes the next two
prove_manifold_boundary         5   indegree = outdegree = 1, least-first
stitch_boundary_cycles          6   deterministic seeds, unique continuation
simplify_collinear_subdivision  7   cyclic, to a fixed point
region_from_arrangement         8-11 classify, construct, order holes, prove area
```

Three details worth recording.

1. **Both contact kinds reduce to one split rule.** The act separates collinear
   overlap from endpoint/vertex-on-edge contact, but the operation they ask for
   is identical: an endpoint lying on the other closed segment becomes a split
   point of that other segment. The implementation classifies the pair (to reject
   proper crossings and skip disjoint pairs) and then applies the one rule to all
   four endpoints. No coordinate is ever constructed, divided, or interpolated,
   so subdivision can only reuse coordinates the arrangement already contains.

2. **Ordering along an edge needs one raw scalar and no parameter.** Every split
   point is collinear with its edge and inside its bounding box, so `x` (or `y`
   for a vertical edge), reversed when the edge decreases on that axis, totally
   orders them — and equal keys therefore mean equal points, which is what makes
   the exact deduplication a plain adjacent-equality pass.

3. **Same-placement edge pairs are compared too.** The act says every pair, and
   a footprint's own adjacent edges only ever contribute their shared vertex,
   which each edge already carries. Nonadjacent pairs within one simple polygon
   are disjoint by `Polygon::make`'s own step 6, so this costs a little time and
   changes nothing.

Cancellation is per atomic segment, never per source edge, so the "several short
edges against one long edge" and T-junction cases work by construction rather
than by special case. `tests/test_arrangement_region.cpp` proves both, and the
one-raw-unit case proves the converse: 2^20 - 1 raw units of a shared edge cancel
while the two leftover single units stay boundary and show up as four extra exact
vertices.

## the internal failure kinds are all structurally unreachable

Every one of the nine invariant failures is implemented, carries the provenance
the act specifies, and returns rather than asserting. **None of them is reachable
through a public `Arrangement`**, which is exactly why they are internal rather
than expected. No test forces one, and no construction seam, mutable footprint
access, or unchecked arrangement path was added to fabricate one — the act
forbids that, and it would have meant testing a different type than the one
shipping.

The justification, kind by kind:

- **`unsupported_boundary_crossing`** — a proper crossing means one footprint
  edge passes through the other footprint's interior locally, and the crossing
  edge's own interior side then lies in both interiors. Pairwise interior
  disjointness excludes it.
- **`invalid_segment_multiplicity`** — an atomic segment has two sides. Two
  same-directed incidences put two filled interiors on the same side; a third
  incidence of any direction puts two on one side. Both are interior overlap.
- **`open_boundary_walk`** and **`repeated_boundary_vertex`** — both are excluded
  by the manifold proof that runs immediately before the walk: outdegree one
  makes continuation exist and be unique, indegree one makes a revisit
  impossible. They are the walk's own defensive statements about its
  precondition.
- **`zero_area_boundary`** — a retained cycle bounds coverage on its left; after
  collinear simplification a degenerate cycle would need a fold, which is a
  repeated vertex the walk already excluded.
- **`no_outer_boundary`** — nonempty coverage has positive area, so at least one
  cycle winds counterclockwise.
- **`polygon_construction_failed`** — a simplified manifold cycle is closed, has
  no repeated vertex, no zero-length edge, no redundant collinear vertex, and no
  self-intersection (a self-intersection is either a crossing or a pinch, both
  already rejected).
- **`region_construction_failed`** — a hole cycle touching or leaving the outer
  boundary is a pinch or a second positive component.
- **`area_mismatch`** — the theorem the act asks to be proven anyway. It is the
  one kind that would fire on a subtle defect in *this* file rather than on a
  broken upstream invariant, which is why it is computed independently from the
  footprints rather than from anything the boundary algorithm produced.

`area_mismatch` is nonetheless *checked* on every success path, and the tests
assert exact equality with the independently summed footprint area on eleven
distinct arrangements, including the hex-12 star and the one-raw-unit case. If
any of the reasoning above is wrong, that check is what says so.

## what the native tests assert

`tests/test_arrangement_region.cpp` (20 cases) covers the act's list in order:
empty; one convex tile; one concave tile; two complete-edge neighbours; several
short edges against one long edge; a T-junction surviving as one boundary vertex
while the straight run beside it is simplified away; a concave lattice patch; a
ring deriving one hole; two omitted interior cells deriving two ordered holes; a
mixed-prototile hex-12 star; storage-order variants producing the same canonical
region; point-only contact rejected as nonmanifold; an edge-connected patch
pinched at one point; two pinch points reported at the lexicographically least;
two separated tiles; component points in lexicographic rather than storage order;
bridge removal turning a valid region into disconnected coverage; nonmanifold
precedence over disconnection; exact area agreement across five fixtures; and the
one-raw-unit mutation.

Comparisons are exact throughout: complete canonical vertex sequences, hole
count and hole order, exact error codes, exact payload points and degrees, and
`Int256` doubled areas. Nothing compares projected coordinates, approximate
areas, or unordered vertex sets.

Fixture notes:

- **Nothing is fabricated.** Every prototile comes from `Polygon::make` →
  `Prototile::make`, every oriented value from `compile_lattice_orientations` or
  `compile_hex12_orientations`, every placement from `Placement::make`, and every
  arrangement from `try_insert` / `try_join_full_edges` / `try_remove`.
- **The hex-12 star authors no hex coordinate.** A hexagon is placed at exact
  origin and six triangles are grown onto its edges through ordinary exact joins;
  the test then asserts the derived boundary has twelve vertices, no holes, and
  area equal to the seven summed footprints — the star, not the hexagon.
- **The pinch fixture is genuinely edge-connected.** It is a C of seven cells
  (a 3 x 3 block less its corner and its centre) whose two ends meet diagonally
  at one point. Every cell shares a positive-length edge with the next, so the
  rejection is caused by the pinch alone, not by an isolated tile.

## interpretive decisions, recorded

Three, none of them contradictions in the act.

1. **The repeated-vertex guard is per walk, not per run.** With indegree and
   outdegree one already proven, a vertex belongs to exactly one cycle, so the
   "already stood here" marks are cleared when each walk ends. Reaching a
   previous cycle's vertex is then diagnosed as `open_boundary_walk` (its one
   outgoing segment is consumed) rather than as `repeated_boundary_vertex`, which
   is the honest description of what would have gone wrong. Both are unreachable;
   this only decides which unreachable evidence would be produced.

2. **Holes are sorted with `std::sort`, not `std::stable_sort`.** Two distinct
   hole cycles cannot have equal canonical boundaries — they would be the same
   cycle — so the comparator is a strict total order on the values being sorted
   and stability cannot change the result. The comparator is the act's rule
   exactly: corresponding vertices lexicographically, sequence length only after
   the common prefix.

3. **Collinear simplification restarts its cyclic scan after each removal.** It
   is the plainest expression of "repeat cyclically to a fixed point" and is
   quadratic in cycle length; the pass is dominated by the intentionally
   quadratic pair classification, so nothing was traded for it.

## complexity and ownership

Pair classification is `O(e^2)` in the number of source footprint edges, as
intended. Everything after it is `O(n log n)` in the number of produced atomic
segments — sorting for cancellation, the sorted vertex table with binary-search
indexing for degrees and continuation — except collinear simplification, noted
above. No spatial index, sweep line, polygon-union dependency, rasterizer, or
cache was added, and no placement-count limit was imposed.

All intermediate topology is local value-owned state inside one function and its
file-local helpers. Nothing escapes.

## boundaries respected

No Godot resource; no geometry domain, palette, or blueprint transport; no
colour, supply, editor state, resource identity, file path, or persistence in
core; no change to `Arrangement`, `Polygon`, or `Region` construction; no change
to placement, joining, overlap, containment, command, or completion semantics; no
use of boundary extraction in runtime completion; no support for disconnected
targets; no general polygon union; no automated tiling search; no editor
rendering or controls; no new geometry domain; and no beta, gamma, or
binary-export work.

## for the beta implementor

Four things.

1. **The compiler is pure and total.** `region_from_arrangement` reads a
   `const Arrangement &` and returns a `Result`; it logs nothing, asserts
   nothing, and mutates nothing. Calling it on every candidate document is
   cheap enough at authored scale and safe to call repeatedly.

2. **`empty_arrangement` is the emptiness rejection act 2 deferred.**
   `compile_blueprint` still succeeds on an empty record vector by its own spec.
   Beta's stage order — palette, records, arrangement, region — is what turns an
   empty blueprint into a typed failure.

3. **`disconnected_coverage` and `nonmanifold_boundary_vertex` are ordinary
   authoring outcomes, not defects.** Removing a bridge placement reaches the
   first (a native test does exactly that), and the act-2 editor's contact filter
   constrains only what is *offered*. Anything beta or gamma surfaces to an
   author should distinguish these two from `internal_invariant_failure`, which
   means this file or an upstream invariant is broken and deserves louder
   treatment.

4. **The derived region is canonical.** It depends on the coverage alone, not on
   arrangement storage order, placement identity, or insertion history — outer
   boundary, hole set, and hole order included. Two documents with the same
   coverage export the same exact region, so a round-trip proof can compare
   complete canonical boundaries rather than areas.
