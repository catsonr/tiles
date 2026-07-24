# coordinate representation

this document records how the game represents geometric coordinates. it is a
design decision, not an implementation plan.

## fixed-point lattice

each coordinate is a signed 64-bit integer interpreted with 48 fractional bits:

```text
value = raw / 2^48
```

this gives a resolution of about `3.55e-15` game units and a representable
range of `[-32768, 32768)`. levels are expected to occupy no more than roughly
`1000 × 1000` units, leaving ample range for intermediate and out-of-bounds
placements.

a point is a pair of these coordinates. polygons and placement translations
are composed entirely of such points.

fixed point does not represent irrational values exactly. values outside the
lattice are quantized when they enter the model; afterward, the quantized value
is authoritative. equality within the model is exact rather than
tolerance-based.

arithmetic must be deterministic. operations which can leave the lattice round
to nearest, with exact ties away from zero. overflow is an error, never wrapping
or saturation. calculations such as multiplication, cross products, and area
may require a wider intermediate representation.

## canonical c++ types

the coordinate-bearing value types have the following representation:

```cpp
#include <cstdint>
#include <vector>

namespace tiles {

class Coordinate final {
public:
    using Storage = std::int64_t;

    static constexpr unsigned FRACTIONAL_BITS = 48;
    static constexpr Storage SCALE = Storage { 1 } << FRACTIONAL_BITS;

    static constexpr Coordinate from_raw(Storage p_raw) {
        return Coordinate { p_raw };
    }

    constexpr Storage raw() const {
        return raw_;
    }

    friend constexpr bool operator==(Coordinate p_lhs, Coordinate p_rhs) {
        return p_lhs.raw_ == p_rhs.raw_;
    }

    friend constexpr bool operator!=(Coordinate p_lhs, Coordinate p_rhs) {
        return !(p_lhs == p_rhs);
    }

    friend constexpr bool operator<(Coordinate p_lhs, Coordinate p_rhs) {
        return p_lhs.raw_ < p_rhs.raw_;
    }

private:
    Storage raw_;

    explicit constexpr Coordinate(Storage p_raw) :
        raw_(p_raw) {}
};

struct Point final {
    Coordinate x;
    Coordinate y;

    friend constexpr bool operator==(Point p_lhs, Point p_rhs) {
        return p_lhs.x == p_rhs.x && p_lhs.y == p_rhs.y;
    }

    friend constexpr bool operator!=(Point p_lhs, Point p_rhs) {
        return !(p_lhs == p_rhs);
    }
};

class Polygon final {
public:
    using Vertices = std::vector<Point>;

    const Vertices &vertices() const {
        return vertices_;
    }

private:
    Vertices vertices_;

    explicit Polygon(Vertices p_vertices);
};

} // namespace tiles
```

every `Coordinate` bit pattern denotes exactly one lattice value. equality and
ordering compare the stored integer directly.

`Polygon::vertices_` stores one vertex per corner in connection order. the
closing vertex is implicit and must not be repeated at the end. the constructor
is private because only validated vertex sequences may produce a `Polygon`.
the public validation and construction interface is not decided here.

## rotations and placements

the finite palette and finite rotation rule produce finitely many relevant
prototile-orientation pairs. each pair has one canonical polygon quantized onto
the fixed-point lattice.

a placement consists geometrically of one such canonical oriented polygon plus
a fixed-point translation. translations are applied exactly. rotated polygons
are derived from their prototile rather than from previously rotated geometry,
so quantization error does not accumulate through repeated transformations.

when placements are aligned, their translations may be derived by exact
subtraction of already-quantized points. any irrationality in the ideal
construction has already been resolved when the canonical oriented polygons
were produced.

## rendering seam

the fixed-point model is authoritative. godot rendering uses a separate,
necessarily lossy representation derived from that state. gameplay geometry
must never be reconstructed from rendered coordinates.

the form of this conversion and the godot-side rendering types are not decided
here.

## motivation

the fixed lattice provides:

- deterministic coordinates across the game model;
- exact equality for shared vertices and translations;
- stable polygon predicates without pervasive epsilon comparisons;
- much more precision and range than the intended level scale requires; and
- one explicit boundary where ideal geometry becomes finite game state.

## open questions

- the checked-arithmetic and error-reporting interface;
- the representation used for wide intermediate calculations;
- the c++ representations of prototile identity and orientation;
- ownership and lookup among prototiles, canonical oriented polygons, and
  placements;
- how canonical oriented polygons are generated, validated, and stored;
- the authoring and serialization format for fixed-point geometry;
- polygon normalization and validation details;
- the arrangement structures used to compose or relate placements; and
- the eventual conversion from model geometry to godot rendering data.
