# project hail mary act 2-1 alpha: exact arrangement coverage compiler

## immutable implementation contract

this document is immutable once handed to an implementor. do not revise it to
match implementation choices. stop and ask carson if a genuine contradiction
or behavior-changing ambiguity remains.

record results in:

```text
docs/project-hail-mary/PROJECT-HAIL-MARY-ACT-2-1-ALPHA-IMPLEMENTATION-NOTES.md
```

do not mention this plan or project act in code comments or runtime
documentation. do not commit.

## prerequisite and context seed

project-hail-mary acts 0, 1, and 2 must be implemented and passing. this act
consumes only:

- the existing proof-bearing `Arrangement`;
- each stored placement's checked counterclockwise footprint;
- exact points, segment predicates, and doubled area;
- checked `Polygon` and `Region` construction; and
- the existing dependency-free native test harness.

before editing:

1. inspect and preserve concurrent work;
2. read the project-hail-mary roadmap, immutable acts 0, 1, and 2, and every
   implementation note;
3. read the complete arrangement, placement, region, containment, intersection,
   polygon, exact-integer, coordinate, point, and predicate implementations and
   tests;
4. read act 2's blueprint compiler and tests to understand the public producer
   of arrangements without depending on its editor implementation; and
5. establish the current native, GDExtension, resource-integration,
   editor-integration, and ordinary headless baselines.

if act 2's public blueprint compiler or the arrangement invariant is absent or
materially different, stop. do not recreate either contract inside this act.

## goal

publish one pure exact compiler:

```haskell
regionFromArrangement
  :: Arrangement
  -> Result Region ArrangementRegionError
```

which deterministically turns the coverage of one proof-bearing arrangement
into exactly one checked `Region`, including any holes, or returns typed evidence
that the coverage cannot represent one region.

this operation synthesizes the canonical region from the arrangement. it does
not accept an independently authored region claim and compare the two.

the exact flow is:

```text
proof-bearing Arrangement
        │
        ▼
directed source edges
        │
        ▼
exact atomic boundary segments
        │
        ▼
manifold directed boundary cycles
        │
        ▼
one outer boundary + ordered holes
        │
        ▼
checked Region
```

## files and public location

add:

```text
src/core/ArrangementRegion.h
src/core/ArrangementRegion.cpp
tests/test_arrangement_region.cpp
```

publish a free function equivalent to:

```cpp
Result<Region, ArrangementRegionError> region_from_arrangement(
    const Arrangement &p_arrangement);
```

keep the operation separate from `Arrangement.h`. do not introduce an include
cycle or add region knowledge to `Arrangement`.

no existing public constructor or invariant is weakened to implement or test
this operation.

## expected rejection and internal failure

an ordinary public `Arrangement` already proves:

```text
every footprint is a checked simple polygon
∧
every pair of footprint interiors is disjoint
```

under those invariants, author-reachable failure has only three meanings:

```cpp
enum class ArrangementRegionErrorCode {
    empty_arrangement,
    nonmanifold_boundary_vertex,
    disconnected_coverage,
    internal_invariant_failure,
};
```

the distinction is load-bearing:

- `empty_arrangement`, `nonmanifold_boundary_vertex`, and
  `disconnected_coverage` are expected properties of otherwise valid authored
  arrangements;
- `internal_invariant_failure` means a supposedly proven geometric consequence
  did not hold and therefore indicates an implementation defect, a violated
  upstream invariant, or an incorrect theorem assumption.

no expected rejection is logged, asserted, repaired, or flattened to text.
internal failures also return complete typed evidence rather than asserting,
continuing with partial topology, or publishing a sentinel region.

### expected payloads

`empty_arrangement` carries no payload.

`nonmanifold_boundary_vertex` carries:

- the lexicographically least offending exact `Point`;
- its exact retained-boundary indegree; and
- its exact retained-boundary outdegree.

the point is rejected when:

```text
indegree != 1
∨
outdegree != 1
```

`disconnected_coverage` carries the canonical first point of every positive
boundary component in complete lexicographic order. the vector therefore has at
least two elements and proves both the number and deterministic identity of the
components.

### internal failure kinds

publish one nested typed internal-failure discriminator equivalent to:

```cpp
enum class ArrangementRegionInvariantFailureCode {
    unsupported_boundary_crossing,
    invalid_segment_multiplicity,
    open_boundary_walk,
    repeated_boundary_vertex,
    zero_area_boundary,
    no_outer_boundary,
    polygon_construction_failed,
    region_construction_failed,
    area_mismatch,
};
```

preserve only the provenance appropriate to the selected kind:

- `unsupported_boundary_crossing` carries both source-edge references;
- `invalid_segment_multiplicity` carries the canonical atomic segment and every
  contributing source-edge reference in deterministic incidence order;
- `open_boundary_walk` and `repeated_boundary_vertex` carry the exact point;
- `zero_area_boundary` carries the deterministic cycle index;
- `no_outer_boundary` carries no unrelated payload;
- `polygon_construction_failed` carries the cycle index and exact
  `PolygonError`;
- `region_construction_failed` carries the exact `RegionError`; and
- `area_mismatch` carries both exact doubled-area totals.

a source-edge reference is exactly:

```text
arrangement storage index × footprint polygon edge index
```

it is not a `PlacementId`, pointer, rendered coordinate, or reconstructed
identity. source references and incidence vectors preserve collection order
unless a later rule explicitly orders them.

the implementation may choose nested structs, `std::variant`, or an enum with
checked optional payloads. whichever representation it chooses must make every
rule above observable and must not permit one error to masquerade as another.

## deterministic precedence

when several defects coexist, report the first one reached by the complete
algorithm below:

1. empty arrangement;
2. first unsupported crossing in source-edge pair order;
3. first invalid multiplicity in atomic-segment key order;
4. lexicographically least nonmanifold boundary vertex;
5. first boundary-walk failure in cycle discovery order;
6. first zero-area cycle in cycle discovery order;
7. no positive outer boundary;
8. disconnected positive coverage;
9. first polygon-construction failure in cycle discovery order;
10. region-construction failure; and
11. area mismatch.

do not reorder validation merely because another error is easier to detect.

## exact boundary algorithm

the operation computes the exact boundary of the footprint union without a
general polygon boolean library. the expected authored scale remains roughly
fifty placements or fewer, so bounded pairwise scans are intended.

### 1. collect source edges

collect every directed footprint edge in:

```text
arrangement storage order
then footprint polygon edge order
```

each public footprint is canonical counterclockwise, so its filled interior lies
to the left of every directed edge.

retain each edge's source placement and polygon edge indices. never convert a
coordinate to floating point.

### 2. classify contacts and collect split points

compare every source-edge pair exactly in lexicographic source-reference order.

- a proper non-collinear boundary crossing returns
  `unsupported_boundary_crossing`;
- for a collinear overlap, add every endpoint which lies on the other segment as
  a split point of that segment;
- for endpoint or vertex-on-edge contact, add the exact touching source
  endpoint everywhere it lies on a compared segment; and
- disjoint pairs contribute nothing.

each source edge begins with its own endpoints.

no new intersection coordinate may be divided, interpolated, rounded, snapped,
quantized, or inferred. legal arrangement contacts already express every
required breakpoint as a source vertex.

### 3. emit atomic directed segments

deduplicate each edge's split points exactly.

order them along the directed edge using exact raw-coordinate comparison and the
edge direction:

- use x when the edge is not vertical;
- otherwise use y; and
- reverse the scalar ordering when the directed edge decreases on that axis.

do not compute a floating or rational parameter.

emit every nonzero consecutive directed segment.

### 4. cancel internal atomic segments

group atomic segments under the undirected key:

```haskell
segmentKey (a, b) = (minLex a b, maxLex a b)
```

process keys in complete lexicographic endpoint order.

for each key:

- one incidence is retained coverage boundary;
- exactly two opposite-directed incidences cancel as one shared internal
  segment; and
- two same-directed incidences or more than two incidences return
  `invalid_segment_multiplicity`.

retain a surviving segment in its original direction. coverage interior
therefore remains on its left.

whole-source-edge cancellation is forbidden. this subdivision and cancellation
must accept several short footprint edges meeting one longer footprint edge.

### 5. prove the directed boundary graph

build exact incoming and outgoing incidence for every retained endpoint.

visit vertices in complete lexicographic point order and require:

```text
indegree = 1
∧
outdegree = 1
```

return `nonmanifold_boundary_vertex` for the first violation. this rejects
point-connected components, boundary branches, pinches, and ambiguous
continuation.

### 6. stitch boundary cycles

while an unused retained segment remains:

1. choose the segment with lexicographically least start point, breaking ties by
   end point;
2. follow the unique outgoing segment;
3. require return to the initial point;
4. defensively return `open_boundary_walk` for missing continuation;
5. defensively return `repeated_boundary_vertex` for a repeated non-closing
   point; and
6. consume every followed segment exactly once.

the order in which this process discovers cycles is the deterministic cycle
index used by later failures.

### 7. simplify exact collinear subdivision

for each cycle, remove a vertex exactly when:

```text
predecessor, vertex, successor are collinear
∧
vertex lies strictly inside [predecessor, successor]
```

repeat cyclically to a fixed point.

do not remove a turn, remove an endpoint merely because it is collinear outside
the combined segment, use an epsilon, or simplify in rendered space.

### 8. classify outer coverage and holes

compute every simplified cycle's exact signed doubled area before
`Polygon::make`.

```text
positive  = counterclockwise outer component
negative  = clockwise hole
zero      = zero_area_boundary
```

require exactly one positive cycle:

- zero returns the internal `no_outer_boundary` failure;
- more than one returns expected `disconnected_coverage`.

the `disconnected_coverage` payload is formed from the positive cycles'
lexicographically least points and sorted by complete lexicographic point order.

reverse each negative cycle into counterclockwise order before polygon
construction.

### 9. construct checked polygons

construct every classified cycle through `Polygon::make` in deterministic cycle
discovery order. preserve the first failure and its cycle index.

the unique positive polygon becomes the outer boundary.

sort successfully constructed holes by their complete canonical boundary
sequence, comparing corresponding points lexicographically and using sequence
length only after the common prefix. do not sort by area, discovery order, source
placement, pointer, or only the first point.

this makes the published `Region` independent of arrangement storage order even
when two holes share the same canonical first point prefix.

### 10. construct the checked region

construct the final value through `Region::make`. preserve its exact error as
`region_construction_failed`.

no partial polygon collection or region is returned.

### 11. prove exact area agreement

independently sum the exact signed doubled area of every counterclockwise
footprint.

require:

```text
sum footprint doubled areas == Region::doubled_area()
```

a mismatch returns both exact totals as `area_mismatch`. do not assert it away,
halve either value, convert either value to floating point, or continue with the
region.

## native tests

native tests must cover:

- an empty arrangement;
- one convex tile;
- one concave tile;
- two complete-edge neighbours;
- several short edges against one long edge;
- a valid T-junction after exact subdivision;
- lattice patches with concave outer boundaries;
- a connected patch with one hole;
- a connected patch with multiple holes;
- a mixed-prototile hex-12 patch;
- arrangement-storage-order variants producing the same canonical region;
- point-only connected tiles rejected as nonmanifold;
- a boundary pinch rejected at its lexicographically least point;
- two disconnected positive components;
- removal of a bridge producing disconnected coverage;
- one arrangement containing both a nonmanifold component and a separate
  positive component, proving nonmanifold precedence;
- exact region area equal to the summed footprint area; and
- a mutation in which one raw q16.48 unit of disagreement remains boundary
  rather than cancelling.

tests compare exact points, complete canonical polygon boundaries, hole order,
exact error codes and payloads, and exact doubled areas. visual resemblance,
projected coordinates, approximate scalar area, and unordered vertex sets are
insufficient.

do not add unchecked arrangement construction, mutable footprint access, or
another production seam solely to fabricate internal invariant failures.
structurally unreachable failures must be justified in implementation notes
rather than forced through invalid public geometry.

## complexity and ownership

let `e` be the total number of source footprint edges.

pair classification is intentionally `O(e²)`. subsequent sorting and grouping
may be `O(e log e)` in the number of produced atomic segments. do not add a
spatial index, sweep line, polygon-union dependency, rasterizer, or cache.

alpha imposes no public placement-count limit. a later resource boundary may
cap externally supplied blueprint records before invoking this operation.

all intermediate topology is local value-owned state. no output or error retains
a pointer, reference, iterator, or view into the input arrangement or a temporary
container.

## verification

run:

```text
scons tests
./build/tests/tiles_tests
scons
./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
    res://tests/resource_integration.tscn
./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
    res://tests/level_editor_integration.tscn
./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
```

compile both new non-Godot translation units standalone under c++17,
no-exceptions, and warning flags. run `git diff --check`, inspect the complete
diff, and record exact native totals and every unchanged headless total in the
implementation notes.

verify explicitly that no Godot include, type, resource name, or identifier
entered `src/core/`, and that no existing runtime completion path calls
`region_from_arrangement`.

## boundaries

alpha does not:

- add or modify a Godot resource;
- compile a geometry domain, palette, or blueprint transport;
- add color, supply, editor state, resource identity, file paths, or
  persistence to core;
- change `Arrangement`, `Polygon`, or `Region` construction invariants;
- change placement, joining, overlap, containment, command, or completion
  semantics;
- use boundary extraction during runtime completion;
- support disconnected target regions;
- implement a general polygon union;
- add a placement-count limit;
- add automated tiling search;
- add editor rendering or controls;
- add another geometry domain; or
- implement beta, gamma, or binary-export work.

## acceptance summary

alpha is complete exactly when:

```text
exact proof-bearing Arrangement
∧ deterministic atomic boundary cancellation
∧ manifold directed cycle proof
∧ exactly one positive coverage component
∧ zero or more canonically ordered holes
∧ checked Polygon and Region construction
∧ exact footprint/region area agreement
∧ typed expected rejection
∧ typed internal invariant evidence
```

and all native and unchanged headless baselines pass together.
