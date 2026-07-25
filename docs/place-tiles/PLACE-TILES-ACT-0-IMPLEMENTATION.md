# place tiles act 0: prototile kernel

## goal

implement the plain c++ geometry kernel which makes invalid polygons and
prototiles unrepresentable.

the result of this act is:

```text
source value ──quantize once──▶ Coordinate
                                  │
                              Point sequence
                                  │
                    validate and triangulate
                                  │
                               Polygon
                                  │
                         identity + geometry
                                  │
                              Prototile
```

`DEFINITIONS.md` fixes the domain meaning. `COORDINATES.md` fixes the
authoritative lattice and canonical value-type representation. this document
fixes their construction and validation behavior.

do not implement placements, orientations other than the implicit reference
orientation, arrangements, joins, palettes, regions, levels, rendering, or
godot-facing geometry.

## literature and rationale

the following references motivate the design. they are not dependencies, and
they do not replace the contracts in `DEFINITIONS.md` or `COORDINATES.md`.

- [Meisters, “Polygons Have Ears” (1975)][meisters] supplies the existence
  result behind repeated ear removal. it is why a validated simple polygon can
  always be reduced to triangles without inventing vertices.
- [Mount, *CMSC 754 Computational Geometry*, polygon triangulation][mount]
  develops triangulation as a partition of a polygon's interior and establishes
  the `n - 2` triangle invariant used as the certificate's cardinality check.
- [CGAL, “Predicates and Constructions”][cgal-predicates] explains the
  robustness distinction enforced here: geometric predicates control program
  decisions and therefore must be exact; source-to-lattice conversion is an
  explicit construction performed once.

the implementation consequence is small: validate the boundary once, attach a
finite triangulation certificate, and make every later geometric decision over
exact lattice values. do not introduce epsilon tests, floating-point predicate
fallbacks, or CGAL itself.

[meisters]: https://digitalcommons.unl.edu/mathfacpub/54/
[mount]: https://jeffe.cs.illinois.edu/teaching/compgeom/refs/Mount.pdf
[cgal-predicates]: https://doc.cgal.org/Manual/3.1/doc_html/cgal_manual/Kernel_d/Chapter_predicates_constructions_d.html

## public algebra

the public behavior is equivalent to:

```haskell
fromRaw
  :: Int64
  -> Coordinate

quantizeDouble
  :: Double
  -> Result Coordinate QuantizationError

checkedAdd
  :: Coordinate
  -> Coordinate
  -> Result Coordinate ArithmeticError

checkedSubtract
  :: Coordinate
  -> Coordinate
  -> Result Coordinate ArithmeticError

makePolygon
  :: NonEmpty Point
  -> Result Polygon PolygonError

makePrototile
  :: PrototileId
  -> Polygon
  -> Result Prototile PrototileError

vertices
  :: Polygon
  -> NonEmpty Point

triangulation
  :: Polygon
  -> Exactly (vertexCount - 2) Triangle
```

use an expected-like `Result<T, E>` value rather than exceptions, sentinel
values, output error parameters, or log messages. its exact implementation may
use the c++ standard library, but success and failure must be distinct types and
callers must be able to inspect the error without consuming the result.

## source layout

place the authored core beneath `src/core/` and keep it independent of
`godot-cpp`. use a legible separation equivalent to:

```text
src/core/
├── Result.h
├── Prototile.h
├── Prototile.cpp
└── geometry/
    ├── Coordinate.h
    ├── Coordinate.cpp
    ├── Point.h
    ├── Triangle.h
    ├── ExactInteger.h
    ├── ExactInteger.cpp
    ├── Polygon.h
    ├── Polygon.cpp
    ├── Predicates.h
    └── Predicates.cpp
```

private helpers may be consolidated where that makes the code clearer. do not
put domain geometry in a Godot node, singleton, resource, or `Variant`.

## result and identity types

provide a small expected-like `Result<T, E>` suitable for move-only success
values. it must have named success and failure construction, `has_value()`,
const and mutable value access, and const error access. accessing the inactive
alternative is a programmer error and may assert.

`PrototileId` is a strong value type over `std::uint64_t`. every bit pattern is
valid. equality and ordering compare the stored integer. do not use a bare
integer interchangeably with a prototile id.

do not define `Prototile::operator==` in this act. identity equality and
geometric congruence are different relations, and palette deduplication under
rotation is future work.

## coordinates and points

implement `Coordinate` exactly as fixed in `COORDINATES.md`:

```cpp
using Storage = std::int64_t;

static constexpr unsigned FRACTIONAL_BITS = 48;
static constexpr Storage SCALE = Storage { 1 } << FRACTIONAL_BITS;
```

`Coordinate::from_raw()` is total, `constexpr`, and cannot fail. every storage
value denotes one lattice coordinate.

provide exact equality and ordering. do not add implicit conversions to or from
integer or floating-point types.

`Point` is an aggregate of two coordinates with exact equality. provide an
explicit lexicographic comparison by `(x, y)` for polygon normalization.

provide checked coordinate and point addition and subtraction. overflow returns
`ArithmeticError::overflow`; it never wraps, saturates, throws, or invokes
signed-overflow undefined behavior.

### quantizing a double

`quantizeDouble` is a convenience model-entry path, not the required authoring
format for all future geometry. higher-precision source and orientation
compilers may retain their own precision and call `from_raw()` only after one
final correctly rounded conversion.

for a finite input double, interpret its exact binary value, multiply by
`2^48`, and round to the nearest integer with exact ties away from zero.
performing the conversion from the sign, exponent, and significand is
preferred; the answer must not depend on the active floating-point rounding
mode.

reject:

- positive or negative infinity;
- nan; and
- a rounded result outside signed 64-bit storage.

the negative endpoint `raw == INT64_MIN` is valid. do not negate it in signed
64-bit arithmetic.

use:

```cpp
enum class QuantizationError {
    non_finite,
    out_of_range,
};

enum class ArithmeticError {
    overflow,
};
```

## exact predicate arithmetic

geometric decisions operate on raw integers and must be exact for the entire
`Coordinate` domain.

a difference between two signed 64-bit coordinates may require 65 signed bits.
an orientation determinant may require 129 magnitude bits. a signed shoelace
sum over a vertex collection indexed by 64-bit `std::size_t` fits comfortably
within 256 signed bits.

provide a core-internal exact representation with at least 256 signed bits, or
an equivalent exact implementation of the required operations. later core
geometry may reuse its declared arithmetic interface, but its limb
representation is hidden and it is not a domain or Godot type. only these
operations are needed:

- constructing from signed and unsigned 64-bit values;
- sign, equality, and ordering;
- addition and subtraction;
- multiplication of coordinate values and coordinate differences; and
- exact accumulation.

convert coordinate operands into the exact representation before subtracting
or multiplying them. never form a potentially 65-bit difference in signed
64-bit storage.

do not add a general arbitrary-precision domain API. do not restrict valid
polygon coordinates to make `std::int64_t` or `__int128` predicates appear
safe. `long double`, floating tolerances, and unchecked signed arithmetic are
not acceptable substitutes.

provide exact internal predicates for:

- orientation of three points;
- signed doubled polygon area;
- whether a point lies on a closed segment; and
- complete segment intersection classification.

segment intersection must distinguish at least:

```text
disjoint
proper_crossing
endpoint_touch
collinear_overlap
```

these predicates do not construct intersection coordinates.

## polygon validation

`Polygon` has no public unchecked constructor. its factory accepts a nonempty
owned sequence of already-quantized points and either returns a complete
polygon or one error.

use these errors:

```cpp
enum class PolygonError {
    too_few_vertices,
    repeated_closing_vertex,
    zero_length_edge,
    repeated_vertex,
    redundant_collinear_vertex,
    adjacent_edge_overlap,
    self_intersection,
    zero_area,
    triangulation_failed,
};
```

validate in the listed conceptual order so multiply-invalid input receives a
stable diagnosis:

1. require at least three submitted vertices;
2. reject a final vertex equal to the first;
3. reject equal adjacent vertices, including the implicit closing edge;
4. reject any repeated nonadjacent vertex;
5. classify every consecutive triple:
   - if its middle vertex lies strictly inside the segment between its
     neighbors, return `redundant_collinear_vertex`;
   - if its adjacent edges overlap beyond their common endpoint, return
     `adjacent_edge_overlap`;
6. compare every nonadjacent edge pair and reject every intersection, touch, or
   collinear overlap as `self_intersection`;
7. compute exact signed doubled area and reject zero.

an ordinary quadratic edge-pair algorithm is intentional. polygon sizes are
small, while a sweep-line implementation would add ordering degeneracies which
do not help this milestone.

## polygon normalization

after validation:

1. reverse clockwise input into counterclockwise cartesian winding;
2. cyclically rotate the vertices so the lexicographically smallest point is
   index zero.

there are no repeated vertices, so the canonical first vertex is unique.
reversed winding and every cyclic shift of the same fixed-point boundary must
produce the same stored vertex sequence.

polygon equality, if provided, compares only this canonical cyclic boundary.
triangulation is derived evidence and is not part of polygon identity.

## triangulation

every accepted polygon owns a triangulation before construction succeeds.
triangulation happens only after the boundary has been proven simple, so the
two-ears theorem applies.

use deterministic ear clipping:

1. maintain the active canonical vertex indices in cyclic order;
2. a candidate ear is a strictly counterclockwise consecutive triple;
3. no other active polygon vertex may lie inside or on the candidate triangle;
4. scan active vertices in canonical order and clip the first valid ear;
5. continue until one final triangle remains.

each triangle:

- contains exactly three polygon vertices;
- is stored counterclockwise;
- has nonzero area; and
- introduces no new coordinate.

an `n`-vertex polygon stores exactly `n - 2` triangles. the ear-removal
construction guarantees that triangle interiors are pairwise disjoint and
their union is the polygon.

if a boundary which passed validation cannot produce this result, return
`triangulation_failed`. this signals an implementation defect or an omitted
degeneracy; never publish a polygon without its certificate.

expose the immutable triangulation to plain c++ core consumers. it must not be
bound to Godot in this act.

## prototile construction

`Prototile` is an immutable value containing a `PrototileId` and one `Polygon`.

the polygon submitted to the prototile factory is translation-normalized:

1. take canonical vertex zero as the local origin;
2. subtract it from every polygon vertex with checked arithmetic;
3. reconstruct the translated polygon through the validated polygon factory.

this makes lattice translations of one canonical boundary produce identical
local prototile vertices. rotation is deliberately not normalized: it is a
placement dimension, and compiling canonical oriented polygons is later work.

if any translated component is not representable, return:

```cpp
enum class PrototileError {
    normalization_overflow,
};
```

translation preserves simplicity and triangulation topology, but reconstruct
through the factory so `Prototile` never needs privileged access to an
incomplete polygon.

## native tests

add a dependency-free native test executable beneath `tests/`. keep its build
and output separate from the GDExtension library. extend scons with an explicit
test target while preserving plain `scons` as the extension build.

a suitable interface is:

```sh
scons tests
./build/tests/tiles_core_tests
```

ignore generated test output. do not add a downloaded test framework merely
for assertion syntax.

tests must cover:

### coordinates

- `from_raw()` at `INT64_MIN`, zero, and `INT64_MAX`;
- exact equality and ordering;
- checked addition and subtraction at both limits;
- point arithmetic with one failing component;
- finite double quantization on both signs;
- values below, at, and above positive and negative half-lattice ties;
- both representable endpoints and just-out-of-range values;
- positive infinity, negative infinity, and every nan class used by the test
  platform; and
- independence from each supported floating-point rounding mode.

### predicates and validation

- clockwise, counterclockwise, and collinear orientation near raw extrema;
- all segment intersection classifications;
- valid triangles, convex polygons, and concave polygons;
- both windings and every cyclic shift of each valid polygon;
- every `PolygonError`;
- nonadjacent vertex-on-edge contact;
- partial and complete collinear edge overlap;
- a bow tie and other zero-area/self-intersecting loops; and
- large-magnitude polygons whose determinant exceeds signed 128-bit range.

### triangulation

- exactly `n - 2` triangles;
- triangle vertices drawn only from the polygon;
- strictly counterclockwise nondegenerate triangles;
- exact equality between polygon doubled area and the sum of triangle doubled
  areas;
- convex and strongly concave polygons;
- polygons whose valid ears change after each removal; and
- deterministic output across winding and cyclic input variants.

### prototiles

- identity preservation;
- local vertex zero equal to `(0, 0)`;
- identical local geometry from several lattice translations;
- normalization at coordinate limits; and
- `normalization_overflow`.

## integration

the new `.cpp` files must be found by the existing recursive scons discovery.
do not register core value types with Godot and do not replace the disposable
smoke-test node.

plain `scons` must still build the extension. the existing bounded headless
smoke run must still print:

```text
[tiles] gdextension smoke test ready
```

## acceptance

this act is complete when:

1. every public `Coordinate` value is one exact q16.48 lattice coordinate;
2. every public `Polygon` satisfies `DEFINITIONS.md`;
3. every polygon owns an exact `n - 2` triangle decomposition;
4. every `Prototile` has stable identity and translation-normalized local
   geometry;
5. all invalid construction paths return typed errors;
6. all required native tests pass;
7. the GDExtension still builds and loads; and
8. no placement, arrangement, joining, orientation compilation, palette, or
   rendering behavior has entered the repository.

do not commit.

record implementation decisions and verification results in
`docs/PLACE-TILES-ACT-0-IMPLEMENTATION-NOTES.md`.
