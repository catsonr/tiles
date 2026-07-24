#include "TestHarness.h"

#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/geometry/Polygon.h"

#include <cstdint>
#include <utility>
#include <vector>

using namespace tiles;
using tiles_test::raw_pt;

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

// A reference unit square prototile, already canonical (lexicographically
// smallest vertex first, counterclockwise), so its normalized polygon keeps
// this exact vertex order.
Prototile square() {
    return prototile_or_die(1, { raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) });
}

} // namespace

TEST_CASE("placement translation by zero leaves the canonical footprint") {
    const Prototile proto = square();
    auto placement = Placement::make(proto, raw(0, 0));
    CHECK(placement.has_value());

    const Polygon::Vertices &canonical = proto.polygon().vertices();
    const Polygon::Vertices &footprint = placement.value().footprint().vertices();
    CHECK(footprint.size() == canonical.size());
    for (std::size_t i = 0; i < canonical.size(); ++i) {
        CHECK(footprint[i] == canonical[i]);
    }
}

TEST_CASE("placement footprint is the canonical polygon translated exactly once") {
    const Prototile proto = square();
    const std::vector<std::pair<std::int64_t, std::int64_t>> shifts = {
        { 7, 3 }, { -11, 5 }, { -4, -9 }, { 1000, -1000 }
    };

    for (const auto &shift : shifts) {
        const Point t = raw(shift.first, shift.second);
        auto placement = Placement::make(proto, t);
        CHECK(placement.has_value());

        const Polygon::Vertices &canonical = proto.polygon().vertices();
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

TEST_CASE("placement footprint triangles are the canonical triangles translated once") {
    const Prototile proto = square();
    const Point t = raw(-6, 13);
    auto placement = Placement::make(proto, t);
    CHECK(placement.has_value());

    const Polygon::Triangulation &canonical = proto.polygon().triangulation();
    const Polygon::Triangulation &footprint = placement.value().footprint().triangulation();
    CHECK(footprint.size() == canonical.size());
    for (std::size_t i = 0; i < canonical.size(); ++i) {
        for (std::size_t v = 0; v < 3; ++v) {
            CHECK(footprint[i].vertices[v] == raw(canonical[i].vertices[v].x.raw() - 6,
                                                  canonical[i].vertices[v].y.raw() + 13));
        }
    }
}

TEST_CASE("placement leaves the prototile polygon canonical and untranslated") {
    const Prototile proto = square();
    auto placement = Placement::make(proto, raw(500, -250));
    CHECK(placement.has_value());

    // The placement owns its prototile; that prototile's polygon must stay at
    // the local origin, never carrying the placement translation.
    const Polygon::Vertices &stored = placement.value().prototile().polygon().vertices();
    CHECK(stored.front() == raw(0, 0));
    const Polygon::Vertices &original = proto.polygon().vertices();
    CHECK(stored.size() == original.size());
    for (std::size_t i = 0; i < original.size(); ++i) {
        CHECK(stored[i] == original[i]);
    }
    CHECK(placement.value().translation() == raw(500, -250));
}

TEST_CASE("placement footprint overflow high on x") {
    // A vertex with x > 0 pushed past INT64_MAX.
    auto placement = Placement::make(square(), raw(INT64_MAX, 0));
    CHECK(placement.has_value() == false);
    CHECK(placement.error() == PlacementError::footprint_overflow);
}

TEST_CASE("placement footprint overflow high on y") {
    auto placement = Placement::make(square(), raw(0, INT64_MAX));
    CHECK(placement.has_value() == false);
    CHECK(placement.error() == PlacementError::footprint_overflow);
}

TEST_CASE("placement footprint overflow low on y") {
    // This prototile has a canonical vertex with negative y (it is not the
    // lexicographic minimum), so a maximally negative y translation underflows.
    const Prototile proto = prototile_or_die(2, { raw(0, 0), raw(6, -2), raw(2, 3) });
    auto placement = Placement::make(proto, raw(0, INT64_MIN));
    CHECK(placement.has_value() == false);
    CHECK(placement.error() == PlacementError::footprint_overflow);
}

TEST_CASE("placement cannot underflow x through the reference-orientation path") {
    // Normalization fixes the canonical first vertex at the origin, so the
    // minimum x coordinate of any prototile polygon is exactly zero. Adding the
    // most negative representable x can therefore never drop a vertex below
    // INT64_MIN: a low-x footprint overflow is structurally unreachable here,
    // and this maximally negative translation succeeds.
    const Prototile proto = prototile_or_die(3, { raw(0, 0), raw(6, -2), raw(2, 3) });
    auto placement = Placement::make(proto, raw(INT64_MIN, 0));
    CHECK(placement.has_value());
}
