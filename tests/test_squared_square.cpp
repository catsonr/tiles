#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/geometry/Alignment.h"
#include "core/geometry/Polygon.h"

#include <cstdint>
#include <utility>
#include <vector>

using namespace tiles;
using tiles_test::raw_pt;
using tiles_test::reference_orientation;

namespace {

// Whole game-unit coordinates: one game unit is Coordinate::SCALE raw units.
Point unit(std::int64_t p_gx, std::int64_t p_gy) {
    return raw_pt(p_gx * Coordinate::SCALE, p_gy * Coordinate::SCALE);
}

Prototile proto(std::uint64_t p_id, std::vector<Point> p_vertices) {
    auto polygon = Polygon::make(std::move(p_vertices));
    auto p = Prototile::make(PrototileId(p_id), std::move(polygon).value());
    return std::move(p).value();
}

// A side-2 square and a unit square, both on whole game-unit coordinates.
Prototile side2() {
    return proto(1, { unit(0, 0), unit(2, 0), unit(2, 2), unit(0, 2) });
}
Prototile unit_square() {
    return proto(2, { unit(0, 0), unit(1, 0), unit(1, 1), unit(0, 1) });
}

Placement place(const Prototile &p_proto, std::int64_t p_gx, std::int64_t p_gy) {
    auto placement = Placement::make(reference_orientation(p_proto), unit(p_gx, p_gy));
    return std::move(placement).value();
}

VertexIndex find_vertex(const Polygon &p_polygon, Point p_point) {
    const Polygon::Vertices &v = p_polygon.vertices();
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == p_point) {
            return VertexIndex(i);
        }
    }
    return VertexIndex(v.size());
}

const Polygon &footprint_of(const Arrangement &p_arrangement, PlacementId p_id) {
    for (const Entry &entry : p_arrangement.entries()) {
        if (entry.id == p_id) {
            return entry.placement.footprint();
        }
    }
    return p_arrangement.entries().front().placement.footprint();
}

bool has_vertex(const Polygon &p_polygon, Point p_point) {
    for (const Point &v : p_polygon.vertices()) {
        if (v == p_point) {
            return true;
        }
    }
    return false;
}

// Vertex-join the candidate prototile so that its corner p_candidate_corner meets
// the anchor footprint corner p_anchor_corner.
Result<PlacementId, JoinError> vertex_join(
    Arrangement &p_arrangement,
    PlacementId p_anchor,
    Point p_anchor_corner,
    const Prototile &p_candidate,
    Point p_candidate_corner) {
    const VertexIndex anchor_vertex =
        find_vertex(footprint_of(p_arrangement, p_anchor), p_anchor_corner);
    const OrientedPrototile candidate = reference_orientation(p_candidate);
    const VertexIndex candidate_vertex =
        find_vertex(candidate.canonical_polygon(), p_candidate_corner);
    return p_arrangement.try_join_vertices(
        p_anchor, anchor_vertex, candidate, candidate_vertex);
}

} // namespace

TEST_CASE("two unit squares split a side-2 square's right edge into partial contacts") {
    Arrangement arrangement;
    const auto big = arrangement.try_insert(place(side2(), 0, 0)).value();

    // Lower unit square: anchor corner (2,0) meets candidate corner (0,0); lands
    // at [2,3]x[0,1], sharing the lower half of the anchor's right edge.
    auto lower = vertex_join(arrangement, big, unit(2, 0), unit_square(), unit(0, 0));
    CHECK(lower.has_value());

    // Upper unit square: anchor corner (2,2) meets candidate corner (0,1); lands
    // at [2,3]x[1,2], sharing the upper half of the anchor's right edge and a full
    // edge with the lower unit square.
    auto upper = vertex_join(arrangement, big, unit(2, 2), unit_square(), unit(0, 1));
    CHECK(upper.has_value());

    CHECK(arrangement.entries().size() == 3);

    // The two partial contacts meet exactly at the split point (2,1) on the
    // anchor's right boundary, with no interior overlap (the insertions accepted).
    const Polygon &lower_fp = footprint_of(arrangement, lower.value());
    const Polygon &upper_fp = footprint_of(arrangement, upper.value());
    CHECK(has_vertex(lower_fp, unit(2, 0)));
    CHECK(has_vertex(lower_fp, unit(2, 1)));
    CHECK(has_vertex(upper_fp, unit(2, 1)));
    CHECK(has_vertex(upper_fp, unit(2, 2)));
}

TEST_CASE("a 3x3 squared square composes exactly from one side-2 and five unit squares") {
    Arrangement arrangement;

    // The side-2 square fills the [0,2]^2 quadrant.
    const auto big = arrangement.try_insert(place(side2(), 0, 0)).value();

    // The remaining five unit cells, each vertex-joined into place. Their exact
    // whole-unit translations mean the known cell decomposition composes with no
    // drift and no interior overlap.
    auto c20 = vertex_join(arrangement, big, unit(2, 0), unit_square(), unit(0, 0));
    CHECK(c20.has_value());  // [2,3]x[0,1]

    auto c21 = vertex_join(arrangement, big, unit(2, 2), unit_square(), unit(0, 1));
    CHECK(c21.has_value());  // [2,3]x[1,2]

    auto c22 = vertex_join(arrangement, c21.value(), unit(2, 2), unit_square(), unit(0, 0));
    CHECK(c22.has_value());  // [2,3]x[2,3]

    auto c02 = vertex_join(arrangement, big, unit(0, 2), unit_square(), unit(0, 0));
    CHECK(c02.has_value());  // [0,1]x[2,3]

    auto c12 = vertex_join(arrangement, big, unit(2, 2), unit_square(), unit(1, 0));
    CHECK(c12.has_value());  // [1,2]x[2,3]

    // Six pairwise interior-disjoint placements covering the 3x3 square.
    CHECK(arrangement.entries().size() == 6);

    // The four outer corners of the completed square appear exactly, each on the
    // footprint of the cell that owns it.
    CHECK(has_vertex(footprint_of(arrangement, big), unit(0, 0)));
    CHECK(has_vertex(footprint_of(arrangement, c20.value()), unit(3, 0)));
    CHECK(has_vertex(footprint_of(arrangement, c22.value()), unit(3, 3)));
    CHECK(has_vertex(footprint_of(arrangement, c02.value()), unit(0, 3)));
}
