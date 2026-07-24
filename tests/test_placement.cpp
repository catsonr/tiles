#include "TestHarness.h"

#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/geometry/Polygon.h"

#include <cstdint>
#include <utility>
#include <vector>

using namespace tiles;
using tiles_test::raw_pt;
using tiles_test::reference_orientation;

namespace {

Polygon polygon_or_die(std::vector<Point> p_vertices) {
    auto result = Polygon::make(std::move(p_vertices));
    return std::move(result).value();
}

Prototile prototile_or_die(std::uint64_t p_id, std::vector<Point> p_vertices) {
    auto result = Prototile::make(PrototileId(p_id), polygon_or_die(std::move(p_vertices)));
    return std::move(result).value();
}

Point raw(std::int64_t p_x, std::int64_t p_y) {
    return raw_pt(p_x, p_y);
}

// Compile one specific orientation of a prototile and return the oriented value.
OrientedPrototile oriented(const Prototile &p_prototile, Orientation p_orientation) {
    auto compiled = compile_lattice_orientations(p_prototile, { p_orientation });
    return std::move(compiled).value().front();
}

// A reference unit square prototile, already canonical (lexicographically
// smallest vertex first, counterclockwise), so its normalized polygon keeps
// this exact vertex order.
Prototile square() {
    return prototile_or_die(1, { raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) });
}

// An asymmetric right triangle, so a quarter turn produces a visibly different
// canonical polygon and footprint.
Prototile triangle() {
    return prototile_or_die(2, { raw(0, 0), raw(4, 0), raw(0, 2) });
}

// The production Placement::make has no overload that supplies the reference
// orientation implicitly: it accepts only an OrientedPrototile. There is
// therefore no public placement-construction path without an explicit compiled
// orientation, which is a compile-time guarantee rather than a runtime check.

} // namespace

TEST_CASE("placement translation by zero leaves the oriented canonical footprint") {
    const OrientedPrototile proto = reference_orientation(square());
    auto placement = Placement::make(proto, raw(0, 0));
    CHECK(placement.has_value());

    const Polygon::Vertices &canonical = proto.canonical_polygon().vertices();
    const Polygon::Vertices &footprint = placement.value().footprint().vertices();
    CHECK(footprint.size() == canonical.size());
    for (std::size_t i = 0; i < canonical.size(); ++i) {
        CHECK(footprint[i] == canonical[i]);
    }
}

TEST_CASE("placement footprint is the oriented canonical polygon translated once") {
    const OrientedPrototile proto = reference_orientation(square());
    const std::vector<std::pair<std::int64_t, std::int64_t>> shifts = {
        { 7, 3 }, { -11, 5 }, { -4, -9 }, { 1000, -1000 }
    };

    for (const auto &shift : shifts) {
        const Point t = raw(shift.first, shift.second);
        auto placement = Placement::make(proto, t);
        CHECK(placement.has_value());

        const Polygon::Vertices &canonical = proto.canonical_polygon().vertices();
        const Polygon::Vertices &footprint = placement.value().footprint().vertices();
        CHECK(footprint.size() == canonical.size());
        for (std::size_t i = 0; i < canonical.size(); ++i) {
            // Translation is order-preserving: it fixes the lexicographic
            // minimum and winding, so vertex i maps to vertex i exactly.
            CHECK(footprint[i] == raw(canonical[i].x.raw() + shift.first,
                                      canonical[i].y.raw() + shift.second));
        }
    }
}

TEST_CASE("placement footprint triangles are the oriented triangles translated once") {
    const OrientedPrototile proto = reference_orientation(square());
    const Point t = raw(-6, 13);
    auto placement = Placement::make(proto, t);
    CHECK(placement.has_value());

    const Polygon::Triangulation &canonical = proto.canonical_polygon().triangulation();
    const Polygon::Triangulation &footprint = placement.value().footprint().triangulation();
    CHECK(footprint.size() == canonical.size());
    for (std::size_t i = 0; i < canonical.size(); ++i) {
        for (std::size_t v = 0; v < 3; ++v) {
            CHECK(footprint[i].vertices[v] == raw(canonical[i].vertices[v].x.raw() - 6,
                                                  canonical[i].vertices[v].y.raw() + 13));
        }
    }
}

TEST_CASE("explicit reference-orientation placement records the reference angle") {
    const OrientedPrototile proto = reference_orientation(square());
    auto placement = Placement::make(proto, raw(3, 3));
    CHECK(placement.has_value());
    CHECK(placement.value().orientation() == Orientation::reference());
    CHECK(placement.value().prototile().id() == PrototileId(1));
}

TEST_CASE("a non-reference orientation footprint differs from the reference footprint") {
    const Prototile proto = triangle();
    auto reference = Placement::make(reference_orientation(proto), raw(0, 0));
    auto quarter = Placement::make(oriented(proto, Orientation::quarter()), raw(0, 0));
    CHECK(reference.has_value());
    CHECK(quarter.has_value());

    CHECK(quarter.value().orientation() == Orientation::quarter());

    // Same three corners, but the rotated canonical boundary is not the
    // reference boundary, so the two footprints are not vertex-equal.
    CHECK(!same_boundary(reference.value().footprint(), quarter.value().footprint()));

    // The quarter turn maps the reference right triangle onto its exact rotated,
    // re-normalized boundary (0,0),(2,0),(2,4).
    const Polygon::Vertices &q = quarter.value().footprint().vertices();
    CHECK(q.size() == 3);
    CHECK(q[0] == raw(0, 0));
    CHECK(q[1] == raw(2, 0));
    CHECK(q[2] == raw(2, 4));
}

TEST_CASE("placement leaves the oriented and reference polygons canonical and untranslated") {
    const OrientedPrototile proto = oriented(triangle(), Orientation::quarter());
    auto placement = Placement::make(proto, raw(500, -250));
    CHECK(placement.has_value());

    // The placement owns its oriented prototile; both its oriented canonical
    // polygon and the underlying reference polygon stay at the local origin,
    // never carrying the placement translation.
    const Polygon::Vertices &oriented_local = placement.value().oriented_polygon().vertices();
    CHECK(oriented_local.front() == raw(0, 0));
    const Polygon::Vertices &reference_local =
        placement.value().prototile().polygon().vertices();
    CHECK(reference_local.front() == raw(0, 0));

    // The stored oriented polygon still matches the value we compiled.
    const Polygon::Vertices &original = proto.canonical_polygon().vertices();
    CHECK(oriented_local.size() == original.size());
    for (std::size_t i = 0; i < original.size(); ++i) {
        CHECK(oriented_local[i] == original[i]);
    }
    CHECK(placement.value().translation() == raw(500, -250));
}

TEST_CASE("placement footprint overflow high on x") {
    // A vertex with x > 0 pushed past INT64_MAX.
    auto placement = Placement::make(reference_orientation(square()), raw(INT64_MAX, 0));
    CHECK(placement.has_value() == false);
    CHECK(placement.error() == PlacementError::footprint_overflow);
}

TEST_CASE("placement footprint overflow high on y") {
    auto placement = Placement::make(reference_orientation(square()), raw(0, INT64_MAX));
    CHECK(placement.has_value() == false);
    CHECK(placement.error() == PlacementError::footprint_overflow);
}

TEST_CASE("placement footprint overflow low on y using an oriented fixture") {
    // This prototile has a canonical vertex with negative y (it is not the
    // lexicographic minimum), so a maximally negative y translation underflows.
    const Prototile proto = prototile_or_die(3, { raw(0, 0), raw(6, -2), raw(2, 3) });
    auto placement = Placement::make(reference_orientation(proto), raw(0, INT64_MIN));
    CHECK(placement.has_value() == false);
    CHECK(placement.error() == PlacementError::footprint_overflow);
}

TEST_CASE("placement cannot underflow x because every oriented polygon has minimum x zero") {
    // Translation-normalization fixes the canonical first vertex at the origin
    // for every compiled orientation, so the minimum x coordinate of any oriented
    // local polygon is exactly zero. Adding the most negative representable x can
    // therefore never drop a vertex below INT64_MIN: a low-x footprint overflow
    // is structurally unreachable, and this maximally negative translation
    // succeeds even for a rotated fixture.
    const Prototile proto = prototile_or_die(4, { raw(0, 0), raw(6, -2), raw(2, 3) });
    auto reference = Placement::make(reference_orientation(proto), raw(INT64_MIN, 0));
    CHECK(reference.has_value());
    auto quarter = Placement::make(oriented(proto, Orientation::quarter()), raw(INT64_MIN, 0));
    CHECK(quarter.has_value());
    CHECK(quarter.value().oriented_polygon().vertices().front().x == Coordinate::from_raw(0));
}
