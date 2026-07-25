# project hail mary act 2-1: arrangement regions and Level persistence

## immutable implementation contract

this document is immutable once handed to an implementor. do not revise it to
match implementation choices. stop and ask carson if a genuine contradiction
or behavior-changing ambiguity remains.

record results in:

```text
docs/project-hail-mary/PROJECT-HAIL-MARY-ACT-2-1-IMPLEMENTATION-NOTES.md
```

do not mention this plan or project act in code comments or runtime
documentation. do not commit.

## prerequisite and context seed

acts 1 and 2 must be implemented and passing. this act consumes:

- domain-aware palette compilation from act 1;
- `engine::BlueprintPlacement` and `compile_blueprint` from act 2;
- the exact in-memory blueprint editor from act 2; and
- existing `Region`, `Level`, `State`, placement commands, exact area, and
  Godot persistence.

before editing:

1. inspect and preserve concurrent work;
2. read the phm roadmap, immutable acts 0, 1, 2, and every implementation note;
3. read the complete arrangement, region, containment, intersection, segment,
   polygon, exact-integer, state, level, and blueprint implementations and
   tests;
4. read the complete act-2 editor, resources, compiler, persistence,
   registration, scenes, and headless runners; and
5. establish all current native and headless baselines.

if act 2's public blueprint contract is missing or materially different, stop.
do not build persistence around an unrecorded implementation detail.

## goal

complete project hail mary by turning an exact blueprint into one persistent,
known-solvable level:

```text
BlueprintPlacement records
        │
        ▼
exact Arrangement
        │
        ▼
exact coverage Region
        │
        ▼
engine::Level(Palette, Region)
        │
        ▼
saved and reloadable LevelResource
```

the editor remains functional rather than fancy. correctness, deterministic
topology, typed failure, and round-trip proof are the deliverable.

## exact arrangement-to-region operation

add:

```text
src/core/ArrangementRegion.h
src/core/ArrangementRegion.cpp
tests/test_arrangement_region.cpp
```

with a free function equivalent to:

```cpp
Result<Region, ArrangementRegionError> region_from_arrangement(
    const Arrangement &p_arrangement);
```

keep it separate from `Arrangement.h` if that avoids an include cycle.

### errors

publish typed codes equivalent to:

```cpp
enum class ArrangementRegionErrorCode {
    empty_arrangement,
    unsupported_boundary_crossing,
    invalid_segment_multiplicity,
    nonmanifold_boundary_vertex,
    open_boundary_walk,
    repeated_boundary_vertex,
    zero_area_boundary,
    no_outer_boundary,
    disconnected_coverage,
    polygon_construction_failed,
    region_construction_failed,
    area_mismatch,
};
```

the error carries optional exact provenance appropriate to its code:

- placement and edge indices for a crossing or multiplicity discovered from
  source edges;
- one exact `Point` for a nonmanifold, open, or repeated boundary vertex;
- one or two boundary-cycle indices for cycle classification;
- `PolygonError` only for polygon construction;
- `RegionError` only for region construction; and
- no unrelated payloads.

act implementation may refine field names, but no error is flattened to text,
logged instead of returned, or represented by a sentinel polygon.

define and test deterministic precedence following the algorithm order below.

## boundary algorithm

the input arrangement already proves pairwise interior-disjoint footprints.
the operation computes their exact union boundary without a general polygon
boolean library.

the expected scale is at most roughly fifty placements. bounded pairwise scans
are intended.

### 1. collect source edges

collect every directed footprint edge in arrangement storage order and polygon
edge order. each validated footprint is counterclockwise, so its interior lies
to the left of each directed edge.

retain source placement and edge indices for error identity. never convert a
coordinate to floating point.

### 2. classify contacts and split points

compare every source-edge pair exactly.

- reject any proper non-collinear boundary crossing as
  `unsupported_boundary_crossing`;
- for collinear overlap, add every endpoint lying on the other segment as a
  split point of that segment;
- for endpoint or vertex-on-edge contact, add the exact touching source vertex
  as a split point wherever it lies on an edge; and
- ignore disjoint pairs.

no new intersection coordinate may be rounded or divided into existence.
arrangements authored by exact feature mating already express legal contact
breakpoints as source vertices.

each source edge begins with its own two endpoints. deduplicate split points
exactly and order them along the directed edge using coordinate comparison and
the edge direction, not floating parameterization.

emit every nonzero consecutive atomic directed segment.

### 3. cancel internal atomic segments

group atomic segments by an undirected key consisting of their two endpoints in
lexicographic point order.

for each key:

- one incidence is coverage boundary;
- exactly two opposite-directed incidences cancel as an internal shared
  segment;
- two same-directed incidences or more than two incidences fail as
  `invalid_segment_multiplicity`.

process keys in deterministic lexicographic endpoint order. retain a boundary
segment in its original direction, preserving union interior on its left.

this subdivision and cancellation must accept several short tile edges meeting
one long tile edge. whole-source-edge cancellation is forbidden.

### 4. prove a manifold directed boundary graph

build exact incoming and outgoing incidence at every retained endpoint.

every valid boundary vertex has:

```text
indegree = 1
∧ outdegree = 1
```

reject the lexicographically least violating point as
`nonmanifold_boundary_vertex`. this rejects point-connected components,
branches, pinches, and ambiguous continuation.

### 5. stitch cycles

while unused boundary segments remain:

1. choose the unused segment with lexicographically least start point, breaking
   ties by end point;
2. follow the unique outgoing segment;
3. require return to the initial point;
4. reject a missing continuation as `open_boundary_walk`;
5. reject any non-closing repeated vertex as `repeated_boundary_vertex`; and
6. mark every consumed segment exactly once.

cycle discovery order is deterministic.

remove a cycle vertex exactly when its predecessor, itself, and successor are
collinear and it lies strictly inside their combined straight segment. repeat
cyclically until no redundant subdivision vertex remains. do not remove a turn
or use an epsilon.

### 6. classify outer and holes

compute each simplified cycle's exact signed doubled area before
`Polygon::make`.

- zero fails as `zero_area_boundary`;
- positive means counterclockwise outer boundary;
- negative means clockwise hole boundary.

require exactly one positive cycle:

- zero produces `no_outer_boundary`;
- more than one produces `disconnected_coverage`.

reverse each negative cycle into counterclockwise order before polygon
construction. sort holes deterministically by their canonical first point after
construction, with complete boundary lexicographic order as a tie-breaker.

### 7. publish checked Region

construct the outer and every hole through `Polygon::make`. preserve the first
failure and its cycle index.

construct the final value through `Region::make`; preserve its exact error.

independently sum the exact signed doubled area of every footprint. require it
to equal `Region::doubled_area()`. a mismatch returns `area_mismatch`; do not
assert it away.

no partial polygon or region is returned.

## region tests

native tests must include:

- empty arrangement;
- one convex tile;
- one concave tile;
- two complete-edge neighbours;
- several short edges against one long edge;
- lattice polyomino patches with concave outer boundary;
- a connected patch with one hole;
- a connected patch with multiple holes;
- a hex-12 uniform patch;
- input order variants producing the same canonical region;
- point-only connected tiles rejected as nonmanifold;
- two disconnected positive components rejected;
- a removable bridge whose removal makes coverage disconnected;
- a T-junction which is valid after exact subdivision;
- invalid segment multiplicity where reachable through public geometry;
- deterministic error precedence and provenance;
- exact region area equal to summed footprint area; and
- mutation tests showing one-raw-unit disagreement does not cancel.

do not add unchecked arrangement construction solely to fabricate impossible
states. structurally unreachable backstops must be justified in implementation
notes rather than forced through a production test seam.

## resource graph

replace the current region-first graph with:

```text
LevelResource
    ├── geometry_domain
    ├── PaletteResource
    │     └── [PaletteEntryResource]
    └── blueprint
          └── [BlueprintPlacementResource]
```

the resource stores the authoring witness, not region vertices and not player
progress.

### BlueprintPlacementResource

register one custom resource with signed Godot integer properties:

```text
prototile_id
orientation_step
orientation_order
translation_x_raw
translation_y_raw
```

defaults:

```text
prototile_id       = 0
orientation_step   = 0
orientation_order  = 1
translation_x_raw  = 0
translation_y_raw  = 0
```

setters are transport-only. they preserve every signed value, validate
nothing, compile nothing, and emit `changed` exactly once for an actual change.

translations are authoritative signed q16.48 raw integers. they never pass
through `Vector2`, decimal text, multiplication by `Coordinate::SCALE`, or
quantization.

### LevelResource

replace its region property with:

```text
geometry_domain : int
blueprint       : Array[BlueprintPlacementResource]
```

retain its palette property.

encode:

```text
0 = lattice
1 = hex12
```

the setter preserves other signed values so the compiler can reject malformed
resources. default is lattice.

the resource owns no compiled palette, arrangement, region, level, history,
save result, or path beyond inherited Godot resource identity.

### removed resources

delete `PolygonResource` and `RegionResource`, their registration, compiler
functions, diagnostics, integration checks, and fixtures when no retained code
uses them. do not keep a second region authority or migration adapter.

old development `.tres` compatibility is explicitly absent.

## blueprint resource decoding

add a reusable game compiler equivalent to:

```cpp
Result<std::vector<engine::BlueprintPlacement>, BlueprintResourceError>
compile_blueprint_resource(
    const godot::TypedArray<BlueprintPlacementResource> &p_resources);
```

decode in array order. typed errors preserve index and encoded values.

validation precedence per record:

1. null resource;
2. negative prototile id;
3. orientation step outside `uint32_t`;
4. orientation order outside positive `uint32_t`;
5. `Orientation::make` failure; and
6. raw translations are accepted directly as every `int64_t` bit pattern is a
   valid `Coordinate`.

the orientation is canonicalized by `Orientation::make`. later blueprint
compilation requires that canonical value equal one palette variant's
representative orientation. equivalent but nonrepresentative labels therefore
fail as `orientation_not_in_palette`.

## level resource compilation

replace `compile_level_resource` with the one-way sequence:

1. reject null level resource;
2. decode geometry domain, rejecting any value other than `0` or `1`;
3. compile palette through act 1 using that domain;
4. decode blueprint resource records;
5. compile exact arrangement through act 2's `compile_blueprint`;
6. derive exact region through `region_from_arrangement`; and
7. publish `engine::Level(palette, region)`.

`LevelResourceError` has one typed alternative/payload for each stage:

- missing resource;
- unsupported geometry domain;
- palette invalid;
- blueprint resource invalid;
- blueprint arrangement invalid; and
- arrangement region invalid.

exactly one nested payload is populated. the compiler:

- mutates no resource;
- emits no expected-input diagnostics;
- repairs nothing;
- caches nothing;
- treats no pointer or path as identity;
- returns no blueprint arrangement as runtime state; and
- publishes no partial `Level`.

## editor integration

extend act 2's in-memory document with:

```text
Ref<LevelResource>
optional exact Region
resource path
dirty state
```

records and arrangement remain the active authoring proof.

after every successful addition, removal, or clear, attempt
`region_from_arrangement`:

- success stores the derived region and marks the document exportable;
- expected failure stores the typed reason and leaves the blueprint fully
  editable; and
- no region failure rolls back a valid blueprint edit.

render a successful outer boundary and holes from the exact derived `Region`.
when no region exists, render only the blueprint and concise failure status.

### resource synchronization

construct resource transport from the authoritative document:

- encoded domain;
- current locked `PaletteResource`; and
- one `BlueprintPlacementResource` per record in record order.

orientation writes its canonical representative `step()` and `order()`.
translation writes raw coordinates directly.

the resource is updated transactionally after accepted document edits. no
rendered value is read back.

### save

save and save-as use existing `ResourceSaver`.

before reaching the saver:

1. require blueprint phase;
2. require a nonempty blueprint;
3. require a currently derived region;
4. compile the complete `LevelResource` again through the public compiler; and
5. preserve the document and dirty state on any failure.

new documents require an explicit path. a successfully loaded or saved resource
may reuse its owned path. success clears dirty state; failure does not.

### load

load uncached through `ResourceLoader`, require exact `LevelResource` type, and
compile the entire resource before installing anything.

on success:

- restore domain;
- restore palette rows, colors, and supplies;
- lock palette in blueprint phase;
- restore record order;
- retain the compiler-produced exact palette, arrangement, and region;
- restore resource path; and
- begin clean.

on failure preserve the complete active document. do not partially install a
domain or repair malformed transport.

changing domain or starting new still discards the current document as act 2
specifies. a minimal dirty confirmation is optional only if it does not alter
that model; no confirmation is required.

## known-solution replay

add one pure test/helper path which, given a successfully compiled resource:

1. constructs fresh `engine::State` from its `Level`;
2. maps each blueprint id to the exact palette entry index;
3. maps each representative orientation to the exact orientation index;
4. applies one exact `PlaceCommand` with the stored translation in record
   order; and
5. requires every command to succeed and final `State::is_solved()` to be true.

this is proof of the resource/compiler contract. do not store the replayed state
in `LevelResource` or start a future player with it.

runtime completion remains the existing exact doubled-area sum. do not invoke
`region_from_arrangement` from `State::is_solved`.

## headless resource integration

rewrite `ResourceIntegrationRunner` for the new graph. prove:

- new property metadata and defaults;
- setter notification behavior;
- domain encoding and rejection;
- palette compilation in both domains;
- blueprint record decoding and exact raw-coordinate preservation;
- every typed malformed-record error and precedence;
- exact blueprint arrangement compilation;
- exact region derivation;
- complete level compilation;
- colors do not enter exact geometry;
- a later record failure publishes no partial level;
- save/load round trips for lattice and hex-12;
- raw coordinates, rational orientations, ids, order, supplies, and colors
  survive;
- compiled palette, arrangement-derived region, and area survive exactly;
- known-solution replay makes `State::is_solved()` true;
- temporary files are removed by exact path; and
- resources are not mutated by compilation.

replace the old canonical fixture with the new resource graph.

## headless editor integration

extend act 2's runner to prove:

- a valid blueprint immediately derives and renders a region;
- empty and disconnected blueprints remain editable but cannot save;
- a hole is rendered and persisted;
- accepted edits update resource records and dirty state exactly;
- save is gated by complete recompilation;
- failed save preserves path and dirty state;
- successful save clears dirty state;
- loading is transactional;
- lattice and hex-12 documents round trip into locked blueprint phase;
- loaded exact arrangement and region match the pre-save document;
- changing domain/new document discards the loaded document; and
- no old inverse polygon-authoring operation remains.

## manual acceptance

perform a bounded functional pass, not a polish pass:

1. build and save one lattice level containing partial-edge contact;
2. reopen it and verify the arrangement and derived region;
3. build and save one hex-12 level with at least two prototile types;
4. reopen it and verify the arrangement and derived region;
5. create a disconnected blueprint and confirm save is unavailable; and
6. create a hole when practical and confirm its boundary renders.

record exact behavior and any limitation; do not spend act time styling.

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

compile modified non-Godot translation units standalone under c++17,
no-exceptions, and warning flags. run `git diff --check`, inspect the complete
diff, verify temporary artifacts are absent, and record exact totals and full
outcomes in implementation notes.

## boundaries

act 2-1 does not:

- implement a player or player progress;
- privilege the witness over other runtime solutions;
- use boundary extraction for runtime completion;
- retain independently authored region vertices;
- migrate old resources;
- support disconnected targets;
- implement general polygon union;
- add automated tiling search;
- unlock live palette changes;
- add undo/redo;
- add another geometry domain; or
- add nonessential editor polish.

## acceptance summary

project hail mary is complete only when:

```text
exact lattice or hex12 blueprint
∧ exact manifold arrangement boundary
∧ one checked Region with holes
∧ domain + palette + raw blueprint LevelResource
∧ transactional save and load
∧ exact known-solution replay
∧ existing cheap State::is_solved
∧ native and headless round-trip proof
```

the resource contains no independently authored target geometry, the player
would begin empty, and every exported level carries an exact witness that it can
be solved.
