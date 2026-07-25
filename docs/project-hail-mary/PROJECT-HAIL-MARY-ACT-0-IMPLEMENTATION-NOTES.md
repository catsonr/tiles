# project hail mary act 0 — implementation notes

Notes from implementing the exact unit hex-12 core: one new dependency-free
source compiler publishing the same runtime product tier 1 already publishes.
Everything below is verified against the built test executable, a default
GDExtension build, and a headless load.

## verification

| step | result |
| --- | --- |
| `scons tests` | builds clean |
| `./build/tests/tiles_tests` | **7634 checks, 0 failed across 355 cases** |
| standalone c++17 / `-fno-exceptions` / `-Wall -Wextra -Wpedantic` | no warnings |
| `scons` | GDExtension builds, no warnings or errors |
| headless load | `[tiles] level editor ready: 34 catalog rows`, no error lines |
| `git diff --check` | clean |

The baseline at formalization was **4295 checks, 0 failed across 336 cases**.
That is exactly preserved: `336 + 19 = 355` cases and `4295 + 3339 = 7634`
checks. No existing test was removed, weakened, renamed, or changed to accept a
different tier-1 result — no existing test file was touched at all.

`SConstruct` needed no edit; both the extension glob (`src/**`) and the test glob
(`src/core` + `tests`) discovered the new files.

## files

New:

```text
src/core/Hex12.h
src/core/Hex12.cpp
tests/test_hex12.cpp
```

Modified, and nothing else in the repository:

```text
src/core/OrientedPrototile.h
```

Nothing beneath `src/engine/`, `src/content/`, or `src/game/` changed. No new
type is registered with Godot, and neither `src/core/` nor `src/engine/` gained a
Godot dependency.

## exact public api

```cpp
enum class Hex12RegularPolygon : std::uint8_t {
    triangle, square, hexagon, dodecagon,
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

That is the entire surface. The direction table, the phase mapping, the shape
table, and the boundary walker are all in an anonymous namespace in `Hex12.cpp`;
there is no production accessor for the twelve vectors, so the tests could not
have obtained their expected constants from the implementation even by accident.

**One deliberate refinement of the specified spelling:** the enum has a fixed
underlying type (`: std::uint8_t`). The act requires that "an invalid value
produced by casting an arbitrary integer to `Hex12RegularPolygon` must return
`unsupported_polygon`". Without a fixed underlying type the enumeration's value
range is only `[0, 3]`, so `static_cast<Hex12RegularPolygon>(200)` yields an
*unspecified* value in C++17 and the mandated behavior would not be soundly
testable. Fixing the underlying type makes that cast well defined and the
requirement genuinely provable; nothing else about the type changes. The test
exercises `4`, `5`, and `200`.

## constant proof

```text
q = Coordinate::SCALE = 281474976710656 = 2^48
h = q / 2             = 140737488355328 = 2^47
r                     = 243764480361061
```

`r` is a literal integer. There is no `std::sqrt`, no trigonometry, no
floating-point literal, and no runtime quantization anywhere in `Hex12.cpp` — the
translation unit includes no `<cmath>` and contains no floating-point type.

The nearest-projection proof is carried twice, independently:

- **compile time**, as two `static_assert`s in `Hex12.cpp`; and
- **run time**, as an independent test that re-authors the constants.

Both establish

```text
(2r - 1)^2 < 3q^2 < (2r + 1)^2
```

with exact signed 128-bit products. Concretely:

```text
(2r - 1)^2 = 237684487542791400769778738641
3q^2       = 237684487542793012780631851008
(2r + 1)^2 = 237684487542793350885621627129
```

so `2r` is bracketed within a half unit of `2 · (√3·q/2)`, and `r` is the unique
nearest integer to `√3·q/2 = 243764480361061.3266…`. The test also spells out
uniqueness — the same two products disqualify `r - 1` and `r + 1` — and confirms
`3q² ≠ (2r)²`, i.e. that the projection really is irrational underneath and the
rounding is unavoidable.

A third `static_assert` proves `d[(k+6) mod 12] == -d[k]` for all six pairs by
walking the literal table, so the opposite identity is machine-checked rather
than eyeballed.

## why the geometry closes, and why it does not depend on `r`

Worth recording, because it is the load-bearing structural fact:

**Exact closure needs only two properties of the table — `2h = q`, and the
opposite identity.** The value of `r` never enters.

- For the square, hexagon, and dodecagon (even side counts) the edge multiset is
  closed under `d ↦ -d`, so it cancels pairwise and exactly.
- For the triangle the three edges are `d[k], d[k+4], d[k+8]`, and each of the
  four distinct triples reduces to `q - 2h = 0` or `2h - q = 0` after the two
  `±r` terms cancel against each other.

So the single rounded constant `r` positions the module but cannot open a gap.
This is why `boundary_did_not_close` is a real invariant backstop and not a
latent rounding hazard.

**Every constructed ring is strictly convex**, in every phase, for all four
polygons: the cross product of consecutive edges is one of `qh`, `r² - h²`,
`qr`, `2hr`, `q²`, `r² + h²`, or `h² + r²`, all strictly positive. A strictly
convex closed ring is simple with nonzero area, which is why `Polygon::make`
never rejects a compiled boundary.

**Extents.** The largest magnitude reached by any canonical vertex coordinate
across all four polygons and all twelve phases is `2q + 2r = 1050478914143434`
(the dodecagon's across-flats span of `2 + √3 ≈ 3.732` units), about **8780×**
inside `INT64_MAX`. Cumulative construction still uses the existing checked
`checked_add(Point, Point)`, and any departure from the lattice would surface as
a typed `coordinate_overflow`.

## the compiled polygons are not exactly equilateral

Recorded because the phrase "unit-side" in this act, in the header, and in the
roadmap is slightly stronger than what is actually built.

The twelve module directions do **not** all have the same length:

```text
|d0| = |d3| = |d6| = |d9|  = q                = 281474976710656   (exact)
|d1| = … (the other eight) = sqrt(r² + h²)    ≈ q - 1 raw unit

q²       = 79228162514264337593543950336
r² + h²  = 79228162514264178355311033305
```

So the axis-aligned directions are exactly one unit long and the other eight are
short by one raw lattice unit — `3.55e-15` game units, one ulp of the
representation. The compiled hexagon's six sides therefore run `q, s, s, q, s, s`
rather than six equal lengths, and the dodecagon alternates likewise. The four
phase variants of a given polygon *are* mutually congruent (each has the same
cyclic multiset of side lengths), so the symmetry grouping is unaffected — but no
compiled polygon is exactly regular.

**This is harmless for tiling, and the reason is worth stating precisely:**
matching is always direction-by-direction. Two tiles meet along a specific
`d[k]`, and every edge in that direction — in every polygon, in every phase —
carries the identical displacement vector, whose exact negative is `d[k+6]`. The
tiles are exactly *consistent* with one another even though none is exactly
regular. Nothing in placement, alignment, joining, or arrangement ever compares
two lengths.

It matters for exactly one thing: **do not compute or compare areas against ideal
values.** The compiled area of a "unit" hexagon is not `3√3/2`, and a future
coverage check must be geometric, never `Σ area == target area` against an
authored constant.

## closure survives the rounding at any patch size

This is the property that decides whether the module is usable for real levels,
and the act only required proving it locally (one vertex star, at most six
tiles). It holds globally, for a structural reason.

Write a candidate closed walk as integer coefficients `c[0..11]` over the twelve
directions. Summing the *lattice* vectors gives, identically:

```text
lattice_sum.x = h · (2c0 - 2c6 + c2 - c4 - c8 + c10) + r · (c1 - c5 - c7 + c11)
lattice_sum.y = h · (c1 + 2c3 + c5 - c7 - 2c9 - c11) + r · (c2 + c4 - c8 - c10)
```

The two bracketed expressions are exactly the rational part and the `√3` part of
the same walk taken over the *ideal* unit vectors. A walk closes in the ideal
euclidean tiling precisely when both vanish. Therefore:

> **`r` — the only rounded quantity in the system — is only ever multiplied by
> the `√3`-coefficient equation, which is zero for every relation that closes
> ideally. Any cycle that closes in the ideal tiling closes bit for bit on the
> q16.48 lattice, at any size.**

Quantization error cannot accumulate along a chain of joins because it cannot
appear in a closed one at all. Verified two ways: the identity above was checked
against 200,000 generated integer relations from the rank-8 lattice of ideal
closure relations (zero identity violations, zero closure failures), and the
extended-patch test below closes 30 independent cycles in a 31-tile arrangement
with bit-exact geometry.

A corollary worth knowing: this makes *subtle* drift structurally impossible.
Perturbing any direction vector either breaks the opposite identity (caught by
`static_assert`) or breaks closure (caught as `boundary_did_not_close`). There is
no perturbation that keeps the polygons closing but shifts them slightly — which
is why the module fails loudly rather than quietly. See the mutation section.

## symmetry groups

Requesting all twelve orientations produces exactly the specified tables,
asserted label by label:

```text
polygon      groups   equivalence classes (each number is k/12)
triangle        4     {0,4,8} {1,5,9} {2,6,10} {3,7,11}
square          3     {0,3,6,9} {1,4,7,10} {2,5,8,11}
hexagon         2     {0,2,4,6,8,10} {1,3,5,7,9,11}
dodecagon       1     {0,…,11}
```

The mechanism is structural, not coincidental: phase `k` and phase `k + stride`
walk the *same* cyclic edge sequence from a different starting corner, so their
vertex rings are equal up to rotation and translation. `Polygon::make` rotates to
the lexicographic minimum and `translation_normalize` moves it to the local
origin, so the two canonical boundaries are bit-identical and `same_boundary`
groups them. Stored labels stay canonical fractions — step 6 is observed as
`1/2`, step 3 as `1/4`, step 4 as `1/3` — and the tests assert the canonical
spelling, not a re-spelled `k/12`.

The grouping loop is duplicated from the lattice compiler verbatim, as the act
prefers, rather than refactoring stable tier-1 code or widening construction
access.

## the construction boundary

`OrientedPrototile`'s constructor stays private. The bridge is a namespace-scope
access class:

```cpp
// declared in OrientedPrototile.h
class Hex12OrientedPrototileAccess;
// befriended in the private section, defined only in Hex12.cpp
```

It has one static `publish` that forwards to the private constructor. The header
therefore gains no unchecked construction operation, no public factory accepting
an arbitrary `Prototile` + orientation list + polygon, no inheritance, no virtual
compiler interface, no registration, no type erasure, and no tier dispatcher. The
direct-friendship alternative was rejected because naming
`Result<std::vector<OrientedPrototile>, Hex12CompilationError>` in
`OrientedPrototile.h` would require an include cycle.

The class documentation was corrected. It previously claimed
`compile_lattice_orientations` was the sole producer and that a stored polygon is
"always an exact rotation of its prototile's reference polygon" — both now false.
It states the truthful invariant the act specifies, and notes that the lattice
compiler permutes its q16.48 reference while a direction-module compiler
constructs each phase directly, with neither deriving one published orientation
from another.

`compile_lattice_orientations` is untouched. It still accepts only the four
quarter turns; `test_hex12.cpp` asserts directly that it rejects `1/12`, `2/12`,
`5/12`, `7/12`, and `11/12` while still accepting `{0/12, 3/12, 6/12, 9/12}` as
the four quarter turns with the existing single-group result.

## error-path reachability

**Reachable and positively tested:**

- `empty_orientation_set` — empty input, all optionals empty.
- `unsupported_polygon` — casts of `4`, `5`, `200`, all optionals empty. Also
  tested at precedence: an empty set answers *before* the polygon is inspected,
  and the polygon answers *before* any orientation is validated.
- `unsupported_orientation` — `1/5`, `1/7`, `1/8`, `1/24`, `7/24`, carrying the
  offending canonical angle. Tested as the *least* unsupported angle, not the
  first seen: given `{1/5, 1/8}` it reports `1/8`.

**Structurally unreachable, retained as checked invariant backstops** (no
test-only production input was fabricated to force them, as the act directs):

- `coordinate_overflow` — the worst cumulative coordinate is ~8780× inside the
  lattice bound (above).
- `boundary_did_not_close` — closure follows from `2h = q` and the opposite
  identity alone, both `static_assert`ed (above).
- `polygon_construction_failed` — every compiled ring is strictly convex, hence
  simple with nonzero area (above). This is the same honest gap the lattice
  compiler documents for its own copy of this error.
- `prototile_construction_failed` — `Prototile::make` fails only on
  normalization overflow, and it is handed an already-normalized phase-zero
  polygon whose lexicographic minimum is exactly `(0, 0)`, so its subtraction is
  by the zero vector.
- `normalization_overflow` — same bound as `coordinate_overflow`.

The three unreachable *geometric* ones are all consequences of the module being a
fixed valid table; they would only become reachable if a future act admitted a
variable side length, a different projection, or a non-closing direction set.
They are surfaced as typed errors rather than asserted away so that such an act
cannot silently publish broken geometry.

## test coverage actually asserted

19 new cases, 3339 new checks. No test uses an epsilon, a tolerance, floating
point for gameplay identity, screen coordinates, raster cells, area sampling, a
new overlap predicate, or a hand-authored compatibility table.

**Module and polygon**

- the nearest-projection inequality, its uniqueness, and the irrationality of the
  underlying value;
- all twelve opposite-vector identities;
- all twelve direction vectors, recovered from compiled dodecagon geometry —
  each appearing exactly once, in counterclockwise cyclic order;
- exact closure for all four polygons × all twelve phases (edge displacements
  summing to the zero vector);
- every canonical edge difference belonging to the expected phase/stride cycle,
  allowing for canonical cyclic rotation, including that the recovered starting
  step is congruent to the requested phase modulo the stride;
- exact side counts; `n - 2` nondegenerate counterclockwise triangles drawn from
  the polygon's own vertices;
- canonical first vertex exactly `(0, 0)` after translation normalization;
- reference-orientation geometry equal to the stored reference prototile
  boundary, and `0/12` reaching the same geometry as `0/1`; and
- the requested `PrototileId` surviving into every returned oriented value.

**Requests, errors, grouping**

- the three reachable errors and their precedence (above);
- twelfth-, sixth-, quarter-, third-, and half-turn spellings accepted exactly,
  plus `2/3`, `3/4`, `5/6` — with the stored angle staying canonical;
- duplicate and equivalent spellings (`1/12`, `2/24`, `5/60`) collapsing before
  compilation, checked against a polygon with no symmetry at that angle so a
  survivor could not hide;
- scrambled input (including duplicates and equivalents) producing output
  identical to sorted input, for all four polygons, down to the whole equivalent-
  orientation vector and boundary;
- partial requests retaining only their admitted labels, and a lone non-least
  member becoming its own representative with the class's boundary; and
- the four all-twelve symmetry tables, plus the cross-check that no two groups of
  one polygon share a boundary.

**Exhaustive direction join** — one compiled unit dodecagon as both anchor and
candidate, for each of the twelve module directions: locate the anchor edge with
that exact directed displacement and the candidate edge with its exact opposite,
insert the anchor into a fresh `Arrangement`, join through
`try_join_full_edges`, require success, and compare both shared endpoints as
exact raw `Point` values in *both* footprints — including that the shared edge
appears in opposite senses in the two boundaries — then confirm the pair is still
interior-disjoint via `interiors_overlap`. No hex-12-specific compatibility
function exists or was called: the point is that the existing generic full-edge
comparison accepts the compiled product unchanged.

**Uniform vertex stars** — all ten signatures, each built around exact world
point `(0, 0)`:

```text
3.3.3.3.3.3   4.4.4.4   6.6.6   3.12.12   4.6.12
3.6.3.6   3.4.6.4   3.3.4.3.4   3.3.3.4.4   3.3.3.3.6
```

Each star independently verifies that its signature sums to exactly twelve
interior wedges, then walks the dividing rays: for each side count it scans every
distinct compiled variant and every vertex, requires **exactly one** match whose
two neighbour displacements are the independently authored unit directions of the
current and next rays (in either neighbour order), translates that vertex to the
origin through `checked_subtract`, builds a `Placement`, and inserts it through
the ordinary `Arrangement` api. Then: the final ray is step twelve ≡ step zero;
the entry count equals the signature length; no pair of stored footprints has
positive-area interior overlap; and every dividing unit segment from `(0, 0)` to
its module direction is a complete boundary edge of *both* adjacent footprints in
opposite senses, cyclically — so the last and first placements share the closing
unit edge exactly. `Arrangement`'s existing pairwise scan is the proof; no
acceleration structure was added.

A separate case proves the wedge table `interior_steps(n) = 6 - 12/n` is not just
arithmetic but the actual compiled geometry: at every vertex of every variant of
every polygon, the two neighbour displacements are module directions exactly
`interior_steps(n)` apart.

**Extended 3.6.3.6 patch** — one case beyond what the act required, added to test
the global closure claim above at gameplay scale rather than only around a single
point. In `3.6.3.6` every hexagon edge meets a triangle and every triangle edge
meets a hexagon, so the neighbour type is forced and the patch needs no authored
layout. The test seeds one hexagon and grows breadth-first to depth three; every
tile after the seed is positioned *only* by `try_join_full_edges` against a tile
that was itself joined, so error would have every opportunity to accumulate.

Results, all asserted:

```text
tiles                             31   (7 hexagons, 24 triangles)
joins accepted                    30
joins refused: cell already filled 30
  ...of which bit-exact           30
joins refused for any other reason  0
mating (variant, edge) ambiguous    0   — always exactly one choice
interior overlaps                   0
complete-edge pairs                42
single-vertex pairs                42
distinct vertices                  42   (degree 1/2/3/4 = 12/0/18/12)
```

The load-bearing line is **bit-exact 30 of 30**. Each refusal is a cell some
earlier chain of joins already filled — that is, a cycle in the join graph. The
test recovers the placement the refused join *would* have made (through the pure
`align_full_edge`, which has no overlap check) and requires it to be
`same_boundary` with the tile already there. Thirty independent cycles, thirty
exact closures. Merely overlapping would not pass: a duplicate translated by a
single raw lattice unit matches nothing, which was confirmed separately.

The vertex census is a second, independent drift probe. A `3.6.3.6` vertex is
surrounded by exactly four tiles, and 12 vertices of this patch are shared by
four footprints at identical raw coordinates. Any drift would split those
coincidences apart and collapse the degree-four count to zero. The census also
self-checks: `12·1 + 0·2 + 18·3 + 12·4 = 114 = 7·6 + 24·3`, every corner of every
tile accounted for.

## the suite is not vacuous

Confirmed by mutation rather than assumed. Two perturbations were injected, each
chosen so the opposite-identity `static_assert` still passes:

- `d2 = (h, r+1)`, `d8 = (-h, -(r+1))` — **60 distinct check failures** across
  the closure, direction-cycle, symmetry-table, join, and vertex-star cases.
- `d1 = (r+1, h)`, `d7 = (-(r+1), -h)` — **54 check failures**, then `SIGABRT`
  (exit 134).

The abort is not a defect in the suite; it is the corollary from the closure
section showing up in practice. A perturbed module does not produce
slightly-shifted polygons — it produces polygons that **do not close at all**, so
`compile_hex12_orientations` returns `boundary_did_not_close` and the fixture
helper's `Result::value()` assertion fires. The mutant dies at the closure test
before execution ever reaches the patch test. Worth stating plainly: **this
module cannot be perturbed into subtle drift, only into loud failure.** That is a
property of the design, not of the tests.

Because of that, the patch test's bit-exactness claim was validated a different
way: re-deriving each refused cell translated by one raw lattice unit and
confirming it is `same_boundary` with **nothing** in the arrangement. The thirty
matches are exact, not tolerant.

Both tables were restored and the suite returned to 7634/0/355.

## not done (correctly out of scope)

No `4.8.8` and no square-8 direction module; no arbitrary side lengths, scaling,
or arbitrary regular `n`-gons; no runtime trigonometry or floating-point
geometry; no symbolic-number or radical system; no change to q16.48; no snapping
or post-construction vertex repair; no reflections; no congruence search; no
catalog entries or ids; no palette, supply, or geometry-domain selection; no
engine commands or state; no level resources, persistence, or region authoring;
no editor, player, or rendering change; no target generation, polygon union, or
tiling search; no spatial indexing or performance work; and no Godot-facing type
or behavior.

## for the integration formalizer

Five things worth knowing before the integration act is written. None is a
decision made here.

1. **The compiler owns identity, so the catalog must supply `PrototileId`s that
   do not collide.** `compile_hex12_orientations` takes an id and builds the
   reference `Prototile` itself; it does not consult a registry, and the core
   still does not dedupe ids or search for congruence. A unit triangle compiled
   twice under two ids yields two prototiles the core happily treats as distinct
   types — which `DEFINITIONS.md` says must not happen inside one palette. That
   check belongs to the catalog.

2. **Distinct-variant counts are fixed and small: 4 / 3 / 2 / 1** for triangle /
   square / hexagon / dodecagon. That is the natural length of a "rotate" cycle
   in the editor, and it differs per prototile — a hexagon has two rotations, a
   dodecagon exactly one. Any ui that assumes four (the tier-1 lattice count)
   will be wrong here. Call the compiler **once** per prototile and cache the
   returned vector, exactly as act 2's notes recommend for the lattice compiler.

3. **The hex-12 square's phase zero is exactly the axis-aligned `q × q` lattice
   square** — its four edges are `d0, d3, d6, d9`, i.e. `(±q, 0)` and `(0, ±q)`
   — so the act's handoff suggestion to "reuse the existing unit square
   identity" is geometrically sound at orientation zero. It is *not* sound for
   the rotation set: the hex-12 square's other two variants are phases 1 and 2,
   whose edge directions (`d1, d4, d7, d10` and `d2, d5, d8, d11`) are not
   axis-aligned and share no edge direction with any quarter-turn lattice tile.
   So a lattice-compiled square and a hex-12-compiled square agree on reference
   geometry but carry different admitted orientations and different variant
   counts (3 versus 1 for a square, since all four quarter turns of a square are
   one lattice group). They are different `OrientedPrototile` sets, and the
   catalog must choose one. More generally, prototiles from different domains can
   be inserted into one `Arrangement` and will simply fail to mate; the roadmap's
   "geometry domain" selection is the place to decide whether that is prevented
   or merely unhelpful, since the core deliberately does not know which domain a
   value came from.

4. **Never compare areas against ideal values, and never author a target region
   by hand.** The compiled polygons are exactly consistent but not exactly
   regular (see the side-length section): a "unit" hexagon's area is not
   `3√3/2`, and eight of the twelve directions are one raw unit short of unit
   length. Any future coverage or solution check must be geometric. Target
   regions for a hex-12 level must be built from the same compiled geometry —
   a region typed in from a drawing or from `√3` decimals will miss by ulps and
   nothing will ever tile it. The good news is the converse: a region assembled
   from module vertices is exact forever, at any patch size, per the closure
   theorem.

5. **`4.8.8` is genuinely absent, not merely untested.** The eleventh euclidean
   uniform tiling needs a forty-five-degree module, and `45°` is not a multiple
   of `30°`. It requires the separate square/octagonal module with its own
   projection constant (`√2/2`), not a wider `Hex12RegularPolygon` enum. Nothing
   in this act should be stretched to reach it.
