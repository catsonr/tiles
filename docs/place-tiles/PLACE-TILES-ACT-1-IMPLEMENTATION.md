# place tiles act 1: certified joins

## goal

implement reference-orientation placements, exact polygon interior-overlap
testing, and transactional full-edge joins which preserve `Arrangement`.

this act begins with the completed public contract from
`docs/PLACE-TILES-ACT-0-IMPLEMENTATION.md`:

```text
Coordinate
Point
Polygon { canonical boundary, certified triangulation }
Prototile { identity, translation-normalized polygon }
```

it ends with:

```text
Prototile + translation ──▶ Placement
                                │
                       exact footprint
                                │
              selected full-edge alignment
                                │
            compare against every placement
                                │
                 success ──▶ Arrangement'
                 failure ──▶ Arrangement unchanged
```

`DEFINITIONS.md` is authoritative. an `Arrangement` value must always contain
placements whose footprint interiors are pairwise disjoint. boundary contact
along complete edges, partial edges, or individual points remains legal.

do not implement arbitrary orientations, canonical rotated-polygon generation,
palettes, supplies, targets, regions, solution checking, Godot projection, or
editor commands.

## literature and rationale

the following references motivate the design. they are not dependencies, and
the algorithms and boundary semantics specified below remain authoritative.

- [Ericson, *Real-Time Collision Detection*, §§5.2.1 and 5.2.10][ericson]
  gives the standard separating-axis and triangle-versus-triangle setting.
  act 0's triangulation turns every concave polygon query into exactly those
  convex primitive queries.
- [CGAL, polygon intersection testing][cgal-intersection] names the same
  semantic predicate required here: whether polygon interiors intersect. it
  also identifies exact predicates as the condition for a guaranteed result.
  therefore a shared edge or point is legal, while positive-area overlap is
  not.
- [Ericson, chapters 11 and 12][ericson] covers integer exact arithmetic,
  geometrical robustness, ear cutting, and convex decomposition together. this
  is the engineering motivation for consuming act 0's certified triangles
  instead of reinterpreting concave boundaries during every join.

the implementation only needs a yes/no interior-overlap predicate; it does not
need polygon clipping, intersection construction, or Boolean-set machinery.
weak interval separation in the triangle SAT is the deliberate move that makes
boundary-only contact non-overlap.

[ericson]: https://www.oreilly.com/library/view/real-time-collision-detection/9781558607323/xhtml/c00_08_contents.xhtml
[cgal-intersection]: https://doc.cgal.org/latest/Boolean_set_operations_2/group__boolean__do__intersect.html

## public algebra

the public behavior is equivalent to:

```haskell
makePlacement
  :: Prototile
  -> Point
  -> Result Placement PlacementError

footprint
  :: Placement
  -> Polygon

interiorsOverlap
  :: Polygon
  -> Polygon
  -> Bool

emptyArrangement
  :: Arrangement

tryInsert
  :: Arrangement
  -> Placement
  -> Result PlacementId ArrangementError

tryJoin
  :: Arrangement
  -> PlacementId
  -> EdgeIndex
  -> Prototile
  -> EdgeIndex
  -> Result PlacementId JoinError
```

`tryInsert` and `tryJoin` transactionally mutate their arrangement on success.
on failure they leave its placements, identifier allocator, and observable
ordering unchanged.

all placements in this act use the prototile's reference orientation. this is
the singleton orientation set `{0}`, so the implemented placement remains a
valid restriction of:

```text
placement = prototile × orientation × translation
```

write geometric predicates against `Polygon`, not against assumptions unique
to reference orientation. a later orientation compiler will be able to supply
another canonical polygon without changing alignment or intersection
mathematics.

## source layout

extend the plain c++ core with a separation equivalent to:

```text
src/core/
├── Placement.h
├── Placement.cpp
├── Arrangement.h
├── Arrangement.cpp
└── geometry/
    ├── Intersection.h
    ├── Intersection.cpp
    ├── Alignment.h
    └── Alignment.cpp
```

reuse `Result`, checked arithmetic, exact predicates, and the exact wide
integer support from act 0. do not duplicate a second numeric kernel and do not
introduce Godot types.

## ids and indices

`PlacementId` is a strong value type over `std::uint64_t`. equality and
ordering compare its stored integer. arrangements allocate ids monotonically
beginning at zero.

an id is consumed only after a successful insertion. if no unused id remains,
insertion fails with `identifier_exhausted`.

`EdgeIndex` is a strong value type over `std::size_t`. for polygon vertices
`v`:

```text
edge i = v[i] → v[(i + 1) mod v.size()]
```

the index refers to the canonical vertex order stored by `Polygon`. validate it
before indexing.

## placements and footprints

`Placement` is immutable after construction. in this act it owns:

- one immutable `Prototile` value;
- the implicit reference orientation; and
- one fixed-point translation `Point`.

value ownership is intentional for this first core: no placement may contain a
dangling reference to externally owned geometry. later catalog work may share
immutable canonical geometry without changing semantics.

the placement factory:

1. checked-adds the translation to every canonical prototile vertex;
2. fails if any footprint coordinate overflows;
3. constructs the translated boundary through the `Polygon` factory; and
4. stores the resulting footprint as derived immutable geometry.

the stored prototile polygon remains canonical and untranslated. never derive a
new placement by transforming another placement's footprint.

use:

```cpp
enum class PlacementError {
    footprint_overflow,
    footprint_construction_failed,
};
```

`footprint_construction_failed` is an invariant failure: exact translation of
a valid polygon preserves its geometry. retain the error rather than publishing
an incomplete placement.

## exact triangle interior overlap

implement:

```haskell
triangleInteriorsOverlap
  :: Triangle
  -> Triangle
  -> Bool
```

both triangles are nondegenerate and counterclockwise by the `Polygon`
contract.

use the two-dimensional separating-axis theorem. candidate axes are normals to
all three edges of both triangles.

for each axis:

1. derive its components exactly from the edge endpoints;
2. project all vertices of both triangles using exact wide-integer dot
   products;
3. compute each closed projection interval; and
4. report separation when:

```text
max(a) <= min(b) or max(b) <= min(a)
```

the non-strict comparison is required. equality means the closed triangles
touch on a boundary while their open interiors remain disjoint.

if any axis weakly separates the triangles, return false. if every projection
interval overlaps strictly, return true.

do not normalize axes, calculate lengths, divide, construct intersection
points, or use floating point.

tests must exercise axes whose coordinate differences and dot products exceed
signed 128-bit range.

## exact polygon interior overlap

implement:

```haskell
interiorsOverlap
  :: Polygon
  -> Polygon
  -> Bool
```

first, an exact axis-aligned bounding-box check may return false when the boxes
are weakly separated. equality on one axis cannot contain positive-area
intersection.

otherwise compare every triangle from the first polygon's certified
triangulation with every triangle from the second. return true at the first
triangle pair whose interiors overlap; return false if none do.

this is exact because:

- each triangulation covers its polygon;
- triangle interiors within one triangulation are pairwise disjoint; and
- the intersection of two polygon interiors is open, so a nonempty
  intersection contains positive area and therefore intersects some pair of
  triangle interiors.

the predicate is total and symmetric for all public polygons. it must classify:

```text
no shared points                 -> false
isolated shared point            -> false
partial shared boundary segment  -> false
complete shared boundary edge    -> false
positive-area intersection       -> true
strict containment               -> true
identical polygons               -> true
```

polygon union, clipping, rational intersection coordinates, and boundary-cycle
construction are not part of this predicate.

## arrangements

`Arrangement` owns a finite sequence of entries:

```haskell
data Entry = Entry
  { placementId :: PlacementId
  , placement   :: Placement
  }
```

the sequence is storage, not mathematical ordering. expose only a const
read-only view.

a default-constructed arrangement is empty and valid.

`tryInsert` compares the candidate footprint against every existing footprint.
if any `interiorsOverlap` call returns true, fail and identify the conflicting
placement. otherwise allocate the next id and append the placement.

use:

```cpp
enum class ArrangementErrorCode {
    interior_overlap,
    identifier_exhausted,
};

struct ArrangementError final {
    ArrangementErrorCode code;
    std::optional<PlacementId> conflicting_placement;
};
```

`conflicting_placement` is populated only for `interior_overlap`.

the preservation proof is:

```text
existing entries are pairwise interior-disjoint
and candidate is interior-disjoint from every existing entry
therefore existing entries plus candidate are pairwise interior-disjoint
```

no independent arrangement validation or unchecked insertion API is allowed.

## exact full-edge alignment

joining supports one complete edge from an existing anchor footprint and one
complete edge from the candidate's canonical prototile polygon.

write:

```text
anchor footprint edge:    a0 → a1
candidate canonical edge: b0 → b1
```

the edges are compatible exactly when their displacement vectors are
opposites:

```text
a1 - a0 = b0 - b1
```

compare displacement components in the exact wide representation. endpoint
differences may require 65 signed bits and are not themselves `Coordinate`
values.

derive the candidate translation:

```text
t = a0 - b1
```

both translation components must fit `Coordinate`. verify with checked
arithmetic:

```text
b1 + t = a0
b0 + t = a1
```

then construct the candidate placement from its canonical prototile and `t`.

use:

```cpp
enum class AlignmentError {
    anchor_edge_out_of_range,
    candidate_edge_out_of_range,
    incompatible_edges,
    translation_overflow,
    footprint_overflow,
    footprint_construction_failed,
};
```

alignment proves only exact selected-edge agreement. arrangement insertion
performs the independent whole-footprint proof.

partial-edge alignment, reflection, scaling, approximate edge length, and
automatic edge selection are excluded.

## transactional join

`tryJoin` accepts:

- an existing `PlacementId`;
- its anchor `EdgeIndex`;
- a candidate reference-orientation `Prototile`; and
- the candidate `EdgeIndex`.

it:

1. resolves the anchor placement;
2. validates both edge indices;
3. derives one exact candidate placement through full-edge alignment;
4. compares its footprint against every arrangement placement through
   `tryInsert`; and
5. publishes the new placement id only on success.

use:

```cpp
enum class JoinErrorCode {
    anchor_not_found,
    anchor_edge_out_of_range,
    candidate_edge_out_of_range,
    incompatible_edges,
    translation_overflow,
    footprint_overflow,
    footprint_construction_failed,
    interior_overlap,
    identifier_exhausted,
};

struct JoinError final {
    JoinErrorCode code;
    std::optional<PlacementId> conflicting_placement;
};
```

translate lower-level typed errors without logging or discarding their
meaning. populate `conflicting_placement` only for `interior_overlap`.

a failed join leaves the arrangement byte-for-byte equivalent in observable
state: same entries, same ids, same next-id allocation, and same ordering.

## native tests

extend the dependency-free core test executable introduced in act 0.

### placements

- translation by zero;
- positive and negative translations;
- translated canonical vertices exactly equal footprint vertices;
- overflow independently on all four coordinate directions;
- footprint triangle vertices translated exactly once;
- the canonical prototile polygon remains unchanged; and
- reconstruction failure cannot publish a placement.

### triangle overlap

- positive-area overlap;
- strict containment in both operand orders;
- identical triangles;
- separated triangles;
- a shared complete edge;
- a shared partial edge;
- one shared vertex;
- vertex-on-edge contact;
- collinear edges with same-side interior overlap;
- symmetry under operand exchange;
- invariance under a common representable translation; and
- raw-extreme cases requiring more than signed 128-bit intermediates.

### polygon overlap

- separated convex and concave polygons;
- isolated point contact;
- full shared edges;
- partial shared edges and t-junctions;
- identical polygons;
- strict containment;
- proper boundary crossings;
- positive-area collinear overlap;
- concave interlocking polygons;
- contact at several disconnected boundary features;
- operand symmetry;
- common-translation invariance; and
- cases where overlap is found only by non-first triangles.

### arrangements

- the empty arrangement;
- inserting one placement;
- several independent placements;
- legal point and edge contact;
- rejection against each possible existing placement;
- failure preserving entries, ordering, and next id;
- successful ids increasing monotonically; and
- explicit identifier exhaustion through a test-only construction seam which
  cannot create an invalid public arrangement.

### joins

- exact opposite edge vectors;
- equal-length edges with incompatible direction;
- unequal edge vectors;
- invalid anchor and both invalid edge indices;
- translation overflow;
- a candidate which aligns with its anchor but overlaps that anchor elsewhere;
- a candidate which clears its anchor but overlaps a different placement;
- legal additional contact with non-anchor placements;
- selected world-space endpoints exactly equal after success;
- failure preserving the arrangement and id allocator; and
- a long chain of repeated joins whose shared raw endpoints never drift.

## integration

all new code remains in the plain c++ core. do not register `Placement`,
`Arrangement`, ids, errors, or geometry helpers with Godot.

plain `scons`, the native core tests, and the existing bounded Godot smoke run
must all continue to pass. the smoke run must still print:

```text
[tiles] gdextension smoke test ready
```

## acceptance

this act is complete when:

1. every public `Placement` has a fully representable exact footprint;
2. `interiorsOverlap` exactly distinguishes positive-area interior
   intersection from every legal boundary contact;
3. every full-edge alignment derives one exact fixed-point translation;
4. every public `Arrangement` satisfies pairwise interior disjointness by
   construction;
5. every failed insertion or join preserves arrangement state;
6. repeated joins exhibit no coordinate drift;
7. all required native tests pass;
8. the GDExtension still builds and loads; and
9. no arbitrary rotation, palette, target, region, renderer, or Godot command
   surface has entered the repository.

do not commit.

record implementation decisions and verification results in
`docs/PLACE-TILES-ACT-1-IMPLEMENTATION-NOTES.md`.
