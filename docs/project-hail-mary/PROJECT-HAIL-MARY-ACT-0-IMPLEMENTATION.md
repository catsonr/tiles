# project hail mary act 0: exact unit hex-12 core

## implementor context seed

this act adds one new source compiler to the exact core. it does not replace
the lattice compiler, integrate content, or touch the application. seed the
implementation session from the repository rather than treating this document
as a substitute for the existing contracts.

before editing:

1. run `git status --short` and preserve every pre-existing user change;
2. verify that the current branch is `project-hail-mary`;
3. enumerate authored files with `rg --files`, excluding generated build output,
   `Godot.app/`, and `vendor/`;
4. read this document completely;
5. read `README.md`, `DEFINITIONS.md`, `COORDINATES.md`, and
   `docs/place-tiles/PLACE-TILES-ROADMAP.md` completely;
6. read the completed place-tiles act-0, act-1, and act-2 implementation plans
   and their implementation notes completely;
7. read every file beneath `src/core/` and every core-facing test, paying
   particular attention to:
   - `Orientation`;
   - `OrientedPrototile`;
   - `Prototile`;
   - `Placement`;
   - `Arrangement`;
   - `Coordinate`;
   - `Polygon`;
   - `Rotation`;
   - `Alignment`;
   - exact predicates and intersection;
   - `tests/test_lattice_orientation.cpp`;
   - `tests/test_join.cpp`;
   - `tests/test_arrangement.cpp`; and
   - `tests/TestHarness.h`;
8. inspect `SConstruct` and the GDExtension registration surface, without
   changing either merely to discover recursively included source;
9. establish the baseline with:

   ```text
   scons tests
   ./build/tests/tiles_tests
   scons
   ./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
   ```

the expected native baseline at formalization is:

```text
4295 checks, 0 failed across 336 cases
```

if the branch is wrong, the baseline does not pass, a required completed
contract is absent, or overlapping user work is changing a file this act must
edit, stop and ask carson rather than guessing against a moving repository.

after implementation, record the exact public api, constant proof, error-path
reachability, symmetry groups, uniform-star construction, test totals, build
results, headless result, and any concern for the integration formalizer in:

```text
docs/project-hail-mary/PROJECT-HAIL-MARY-ACT-0-IMPLEMENTATION-NOTES.md
```

do not commit. do not mention this implementation document or project act in
code comments or runtime-facing documentation.

## goal

add one exact, dependency-free compiler for the unit-side hexagonal
twelve-direction module:

```haskell
compileHex12Orientations
  :: PrototileId
  -> Hex12RegularPolygon
  -> [Orientation]
  -> Result [OrientedPrototile] Hex12CompilationError

data Hex12RegularPolygon
  = Triangle
  | Square
  | Hexagon
  | Dodecagon
```

the compiler publishes the same runtime product as tier 1: a nonempty sequence
of distinct, validated, translation-normalized q16.48
`OrientedPrototile` values. every existing placement, joining, intersection,
containment, arrangement, and rendering consumer can therefore use the result
without learning what hex-12 is.

the act is complete only when the existing core accepts exact vertex stars for
all ten euclidean uniform tilings supported by the module:

```text
3.3.3.3.3.3
4.4.4.4
6.6.6
3.12.12
4.6.12
3.6.3.6
3.4.6.4
3.3.4.3.4
3.3.3.4.4
3.3.3.3.6
```

`4.8.8` is the only euclidean uniform tiling deliberately absent. this act
does not implement the separate square/octagonal direction module.

## authority and supersession

`DEFINITIONS.md` and `COORDINATES.md` remain authoritative.

this act implements the hexagonal-direction portion of tier 2 described in the
place-tiles roadmap. it supersedes no completed act and changes no existing
tier-1 behavior.

preserve without weakening:

- q16.48 as the only runtime coordinate representation;
- exact gameplay predicates after source compilation;
- checked, canonical, counterclockwise polygons with certified triangulation;
- translation-normalized reference and oriented polygons;
- rational `Orientation` identity and angular ordering;
- direct-from-source orientation compilation rather than transforming a
  previously quantized orientation;
- exact full-edge and vertex-derived alignment;
- pairwise interior-disjoint arrangements;
- transactional insertion and joining;
- no reflections, scaling, curved edges, or holes within prototiles;
- value ownership with no dangling geometry;
- no Godot dependency in `src/core/` or `src/engine/`; and
- all existing tier-1 public results, errors, ordering, and tests.

in particular, do not broaden `compile_lattice_orientations`. it must continue
to accept only the four quarter turns and reject a twelfth turn exactly as it
does now.

## jam-scale boundary

this is a game geometry compiler, not an analytic-geometry library.

the compiler has:

- one fixed side length of exactly one game unit;
- one fixed table of twelve q16.48 direction vectors;
- four admitted regular source polygons;
- at most twelve requested orientations; and
- no runtime collection larger than a handful of vertices and variants.

the game expects arrangements of roughly fifty placements or fewer. do not add
a symbolic algebra framework, arbitrary radicals, arbitrary regular
`n`-gons, variable side lengths, a spatial index, a congruence solver, a tiling
search, or asymptotic optimization. straightforward bounded scans are the
intended implementation.

## exact hex-12 direction module

let:

```text
q = Coordinate::SCALE = 281474976710656
h = q / 2             = 140737488355328
r = 243764480361061
```

`r` is the q16.48 projection of `sqrt(3) / 2`, rounded to nearest. do not
compute it with `std::sqrt`, trigonometry, a floating-point literal, or runtime
quantization.

the implementation and tests must document and prove with exact wide integer
arithmetic that:

```text
(2r - 1)^2 < 3q^2 < (2r + 1)^2
```

therefore `r` is the unique integer nearest to:

```text
sqrt(3) * q / 2
```

these products fit in signed 128-bit arithmetic. a compile-time assertion is
welcome when it stays legible and supported by the existing compilers; the
native test must independently retain the proof either way.

define the twelve counterclockwise unit direction vectors exactly as:

```text
d0  = ( q,  0)
d1  = ( r,  h)
d2  = ( h,  r)
d3  = ( 0,  q)
d4  = (-h,  r)
d5  = (-r,  h)
d6  = (-q,  0)
d7  = (-r, -h)
d8  = (-h, -r)
d9  = ( 0, -q)
d10 = ( h, -r)
d11 = ( r, -h)
```

the latter six must be defined or verified as exact negatives of the first
six:

```text
d[(k + 6) mod 12] = -d[k]
```

the only approximation is the one fixed projection embodied by `r`. no later
step rounds, snaps, averages, repairs, or reconstructs a point from rendered
coordinates.

## regular boundary construction

use a strong closed enum equivalent to:

```cpp
enum class Hex12RegularPolygon {
    triangle,
    square,
    hexagon,
    dodecagon,
};
```

the corresponding side counts and direction strides are:

```text
polygon       n       stride = 12 / n
triangle      3       4
square        4       3
hexagon       6       2
dodecagon    12       1
```

orientation `k/12` selects phase `k`. the oriented boundary is constructed
directly from the module:

```text
vertex 0 = (0, 0)
edge j   = d[(k + j * stride) mod 12]
vertex j + 1 = vertex j + edge j
```

emit vertices `0` through `n - 1`; the final accumulated endpoint is the
implicit closing vertex and must equal vertex `0` exactly. a failure to close
is a typed internal-module failure, never a silently discarded final point.

construct every ring through `Polygon::make`. then translation-normalize the
polygon through the existing shared helper. do not publish a second polygon
type, a relaxed polygon, or an unchecked triangulation.

all fixed constants and every unit regular-polygon extent are comfortably
inside the coordinate range. nevertheless, use the existing checked point
arithmetic for cumulative construction and preserve any unexpected overflow as
a typed compilation error. never wrap, saturate, throw, or invoke undefined
behavior.

## requested orientations

the requested sequence is a mathematical set:

1. reject an empty input;
2. sort into angular order;
3. remove exact duplicate `Orientation` values;
4. reject the least unsupported angle;
5. compile every retained orientation directly from the hex-12 direction
   module; and
6. return either the complete compiled sequence or one error, never a partial
   result.

an orientation is supported exactly when it equals one of:

```text
0/12, 1/12, 2/12, ..., 11/12
```

comparison uses canonical `Orientation` values. equivalent fractions are
therefore admitted:

```text
1/6 = 2/12
1/4 = 3/12
1/3 = 4/12
1/2 = 6/12
```

do not decide support by converting an angle to floating point. a bounded loop
comparing against the twelve exact `Orientation::make(k, 12)` values is
sufficient.

## reference prototile

the compiler owns construction of the reference `Prototile`; a caller cannot
submit an arbitrary q16.48 polygon and claim that it is a regular hex-12
source.

build phase zero for the selected regular polygon, validate it, and pass it
through:

```cpp
Prototile::make(p_id, reference_polygon)
```

every returned `OrientedPrototile` owns that exact checked reference
`Prototile`. its oriented polygon is the directly constructed polygon for its
requested phase, not a trigonometric rotation of the q16.48 reference and not a
transform of another compiled phase.

if reference orientation zero was requested, its canonical polygon must have
the same exact boundary as the stored reference prototile polygon.

## symmetry grouping

group equal canonical oriented boundaries by the existing `same_boundary`
relation. triangulation is not part of identity.

retain the lattice compiler's established deterministic semantics:

- each output stores the complete sorted nonempty requested-angle subset which
  compiled to that boundary;
- the representative orientation is the least angle in that subset;
- output groups appear in representative angular order; and
- input order and duplicate spellings have no effect.

requesting all twelve orientations must produce:

```text
polygon       distinct groups       labels in each group
triangle             4                       3
square               3                       4
hexagon              2                       6
dodecagon            1                      12
```

the exact equivalence classes are:

```text
triangle:
  {0,4,8}, {1,5,9}, {2,6,10}, {3,7,11}

square:
  {0,3,6,9}, {1,4,7,10}, {2,5,8,11}

hexagon:
  {0,2,4,6,8,10}, {1,3,5,7,9,11}

dodecagon:
  {0,1,2,3,4,5,6,7,8,9,10,11}
```

each number above means `k/12`; stored `Orientation` values remain canonical
fractions, so for example the label at step 6 is observed as `1/2`.

## public api and source layout

use:

```text
src/core/Hex12.h
src/core/Hex12.cpp
tests/test_hex12.cpp
```

`SConstruct` already discovers core and test `.cpp` files recursively. do not
edit it unless verified discovery fails.

publish a surface equivalent to:

```cpp
enum class Hex12RegularPolygon {
    triangle,
    square,
    hexagon,
    dodecagon,
};

enum class Hex12CompilationErrorCode {
    empty_orientation_set,
    unsupported_polygon,
    unsupported_orientation,
    coordinate_overflow,
    boundary_did_not_close,
    polygon_construction_failed,
    prototile_construction_failed,
    normalization_overflow,
};

struct Hex12CompilationError final {
    Hex12CompilationErrorCode code;
    std::optional<Orientation> orientation;
    std::optional<PolygonError> polygon_error;
    std::optional<PrototileError> prototile_error;
};

Result<std::vector<OrientedPrototile>, Hex12CompilationError>
compile_hex12_orientations(
    PrototileId p_id,
    Hex12RegularPolygon p_polygon,
    std::vector<Orientation> p_requested);
```

error precedence is:

1. empty orientation set;
2. unsupported polygon enum value;
3. least unsupported orientation after sorting and deduplication;
4. reference construction;
5. requested orientations in ascending order.

optional payloads obey:

- `orientation` is populated only when one requested or constructed phase is
  responsible; reference construction uses `Orientation::reference()`;
- `polygon_error` is populated only for `polygon_construction_failed`;
- `prototile_error` is populated only for
  `prototile_construction_failed`; and
- all other optionals are empty.

`coordinate_overflow`, `boundary_did_not_close`,
`polygon_construction_failed`, `prototile_construction_failed`, and
`normalization_overflow` are expected to be structurally unreachable for the
fixed valid module. retain and document them as checked invariant backstops;
do not fabricate test-only production inputs solely to force them.

an invalid value produced by casting an arbitrary integer to
`Hex12RegularPolygon` must return `unsupported_polygon` without constructing
geometry.

## `OrientedPrototile` construction boundary

retain the private constructor. add only the narrow friendship required for
`compile_hex12_orientations` to publish checked values.

do not create an include cycle merely to friend the public function signature.
if its result and error types make direct forward declaration awkward, a
namespace-scope access class declared in `OrientedPrototile.h`, defined only in
`Hex12.cpp`, and friended solely to call the private constructor is an
acceptable narrow bridge. it must not expose a header-visible unchecked
construction operation.

update the class documentation so it no longer claims that the lattice
compiler is the sole producer or that every oriented polygon is an exact
quarter-turn permutation of its q16.48 reference. the truthful invariant is:

```text
every OrientedPrototile is published by a checked source compiler
and pairs its claimed orientation with that compiler's validated,
translation-normalized canonical q16.48 polygon.
```

do not add a public or broadly reusable factory which accepts an arbitrary
`Prototile`, orientation list, and polygon. do not introduce inheritance,
virtual compiler interfaces, registration, type erasure, or a general geometry
tier dispatcher.

duplicating the small deterministic symmetry-grouping loop from the lattice
compiler is acceptable and preferred over weakening construction access or
refactoring stable tier-1 code during this act.

## native tests

add `tests/test_hex12.cpp`. expected constants in tests must be authored
independently rather than obtained from a production direction-table accessor.

### module and polygon tests

prove:

- the integer inequality establishing `r` as the nearest projection;
- all twelve exact direction vectors;
- every opposite-vector identity;
- exact closure for every polygon and all twelve phases;
- every canonical edge difference belongs to the expected phase/stride
  direction cycle, allowing for canonical cyclic rotation;
- every compiled polygon has its exact side count;
- every triangulation has exactly `n - 2` nondegenerate triangles;
- every canonical polygon's first vertex is exact local origin `(0, 0)` after
  translation normalization;
- reference orientation geometry equals the stored reference prototile
  boundary;
- the requested `PrototileId` survives into every returned oriented value; and
- no test uses an epsilon to establish gameplay identity.

### request, error, and grouping tests

prove:

- empty input returns `empty_orientation_set` with empty payloads;
- an invalid polygon enum returns `unsupported_polygon` at the specified
  precedence;
- representative twelfth-turn, sixth-turn, quarter-turn, third-turn, and
  half-turn spellings are accepted exactly;
- a non-twelfth angle such as `1/5` returns `unsupported_orientation` with the
  offending canonical angle;
- duplicate and equivalent angle spellings collapse before compilation;
- scrambled and sorted inputs produce identical ordered output;
- partial requested sets retain only their admitted equivalence labels;
- the four all-twelve symmetry tables are exactly those specified above; and
- the lattice compiler still rejects `1/12` and retains all existing results.

### exhaustive direction join test

use one compiled unit dodecagon as both anchor and candidate. for each of the
twelve module directions:

1. find the anchor edge with that exact directed displacement;
2. find the candidate edge with its exact opposite displacement;
3. insert one anchor into a fresh `Arrangement`;
4. join the candidate through `try_join_full_edges`;
5. require success;
6. compare both shared endpoints as exact raw `Point` values; and
7. require the two-placement arrangement to remain interior-disjoint.

do not call a new hex-12 compatibility function. the purpose is to prove that
the existing generic full-edge comparison accepts the compiled product.

## uniform vertex-star acceptance

this is the defining acceptance suite, not optional demonstration coverage.

test these ten ordered vertex signatures exactly:

```text
{3,3,3,3,3,3}
{4,4,4,4}
{6,6,6}
{3,12,12}
{4,6,12}
{3,6,3,6}
{3,4,6,4}
{3,3,4,3,4}
{3,3,3,4,4}
{3,3,3,3,6}
```

for a polygon with `n` sides, its interior angle occupies this many
thirty-degree module wedges:

```text
interior_steps(n) = 6 - 12/n
```

thus:

```text
triangle     2
square       3
hexagon      4
dodecagon    5
```

independently verify in the test that each complete signature sums to exactly
twelve interior steps.

construct each star around exact world point `(0, 0)`:

1. begin with dividing ray step zero;
2. for each side count in signature order, let the next dividing ray be the
   current ray plus `interior_steps(n)`;
3. among that polygon's distinct all-twelve compiled variants, find the unique
   vertex whose two neighbour displacement vectors are the independently
   authored unit directions for the current and next rays, in either neighbour
   order;
4. translate that exact vertex to `(0, 0)` through checked core arithmetic;
5. construct a `Placement` from the selected `OrientedPrototile` and exact
   translation;
6. insert it into one fresh `Arrangement`; and
7. advance to the next dividing ray.

require:

- every required wedge has exactly one matching compiled
  variant-and-vertex choice;
- every placement construction succeeds;
- every insertion succeeds through the ordinary arrangement api;
- the final arrangement entry count equals the signature length;
- no pair of stored footprints has positive-area interior overlap;
- every dividing unit segment from `(0, 0)` to its exact module direction is a
  complete boundary edge of the two adjacent footprints; and
- the final dividing ray is step twelve, identical to step zero, so the last
  and first placements share the closing unit edge exactly.

the fixture may use simple fixed test-only prototile ids keyed by side count.
it must consume only public production results and existing core operations.
it must not use floating point, screen coordinates, an epsilon, raster cells,
area sampling, a new overlap predicate, or a hand-authored compatibility
table.

`Arrangement`'s existing pairwise scan is the intended proof. these stars
contain at most six placements, and eventual game arrangements contain roughly
fifty; do not add acceleration structures.

## verification

after implementation:

1. run `scons tests`;
2. run `./build/tests/tiles_tests`;
3. compile each new or modified translation unit standalone under the
   repository's c++17, no-exceptions, and warning flags;
4. run `scons`;
5. run the existing headless boot command;
6. verify `git diff --check`;
7. inspect `git diff --stat` and the complete diff;
8. verify that no existing test was removed, weakened, renamed to evade
   discovery, or changed to accept a different tier-1 result; and
9. verify that no authored file beneath `src/engine/`, `src/content/`, or
   `src/game/` changed.

the implementation is accepted only when:

```text
existing 4295 checks
∧ all new hex-12 checks
∧ all ten uniform vertex stars
∧ all twelve full-edge joins
∧ native build
∧ gdextension build
∧ headless load
```

all pass together with zero failures, errors, or warnings.

## out of scope

- square/octagonal `4.8.8`;
- a square-8 direction module;
- arbitrary side lengths or scaling;
- arbitrary regular polygons;
- runtime trigonometry or floating-point geometry;
- a general symbolic-number or radical system;
- changing q16.48;
- snapping or post-construction vertex repair;
- reflections;
- prototile congruence search;
- content catalog entries or ids;
- palette and supply integration;
- geometry-domain selection;
- engine commands or state;
- level resources or persistence;
- region authoring;
- editor or player controls;
- rendering changes;
- target generation, polygon union, or tiling search;
- spatial indexing or performance work; and
- any Godot-facing type or behavior.

## handoff to the next formalizer

on success, the core product is intentionally sufficient for a later act to:

```text
add unit triangle / hexagon / dodecagon catalog sources
reuse the existing unit square identity
select hex-12 as a level geometry domain
construct PaletteEntry from already-compiled oriented values
preserve exact region vertices through persistence and authoring
```

none of those integration decisions belong in this act. do not anticipate them
with catalog metadata, an engine abstraction, a resource schema, or a Godot
adapter.
