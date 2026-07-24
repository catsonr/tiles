#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/Orientation.h"
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

Point raw(std::int64_t p_x, std::int64_t p_y) {
    return raw_pt(p_x, p_y);
}

Prototile proto(std::uint64_t p_id, std::vector<Point> p_vertices) {
    auto polygon = Polygon::make(std::move(p_vertices));
    auto p = Prototile::make(PrototileId(p_id), std::move(polygon).value());
    return std::move(p).value();
}

Prototile square4() {
    return proto(1, { raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) });
}

Prototile square2() {
    return proto(2, { raw(0, 0), raw(2, 0), raw(2, 2), raw(0, 2) });
}

// A C-shape opening to the right: outer bounds [0,6]^2 with a cavity at
// x in [2,6], y in [2,4].
Prototile c_shape() {
    return proto(3, {
        raw(0, 0), raw(6, 0), raw(6, 2), raw(2, 2),
        raw(2, 4), raw(6, 4), raw(6, 6), raw(0, 6)
    });
}

Placement place(const Prototile &p_proto, std::int64_t p_x, std::int64_t p_y) {
    auto placement = Placement::make(reference_orientation(p_proto), raw(p_x, p_y));
    return std::move(placement).value();
}

VertexIndex find_vertex(const Polygon &p_polygon, Point p_point) {
    const Polygon::Vertices &v = p_polygon.vertices();
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == p_point) {
            return VertexIndex(i);
        }
    }
    // Out-of-range sentinel if the test asked for a vertex that does not exist.
    return VertexIndex(v.size());
}

bool footprint_has_vertex(const Placement &p_placement, Point p_point) {
    for (const Point &v : p_placement.footprint().vertices()) {
        if (v == p_point) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("vertex join with legal isolated point contact succeeds") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square2(), 0, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();

    // Mate the anchor corner (2,2) with the candidate corner (0,0): the candidate
    // lands at [2,4]^2, touching the anchor only at the single point (2,2).
    const VertexIndex anchor_vertex = find_vertex(anchor_footprint, raw(2, 2));
    const OrientedPrototile candidate = reference_orientation(square2());
    const VertexIndex candidate_vertex = find_vertex(candidate.canonical_polygon(), raw(0, 0));

    auto joined =
        arrangement.try_join_vertices(anchor, anchor_vertex, candidate, candidate_vertex);
    CHECK(joined.has_value());
    CHECK(joined.value() == PlacementId(1));
    CHECK(arrangement.entries().size() == 2);

    const Placement &placed = arrangement.entries().back().placement;
    CHECK(placed.translation() == raw(2, 2));
    CHECK(footprint_has_vertex(placed, raw(2, 2)));
    CHECK(footprint_has_vertex(placed, raw(4, 4)));
}

TEST_CASE("vertex join with a missing anchor fails before vertex validation") {
    Arrangement arrangement;
    arrangement.try_insert(place(square2(), 0, 0));

    auto joined = arrangement.try_join_vertices(
        PlacementId(999), VertexIndex(50), reference_orientation(square2()), VertexIndex(50));
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == JoinErrorCode::anchor_not_found);
}

TEST_CASE("vertex join reports each out-of-range vertex index") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square2(), 0, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();
    const VertexIndex good = find_vertex(anchor_footprint, raw(0, 0));

    auto bad_anchor = arrangement.try_join_vertices(
        anchor, VertexIndex(99), reference_orientation(square2()), good);
    CHECK(bad_anchor.has_value() == false);
    CHECK(bad_anchor.error().code == JoinErrorCode::anchor_vertex_out_of_range);
    CHECK(arrangement.entries().size() == 1);

    auto bad_candidate = arrangement.try_join_vertices(
        anchor, good, reference_orientation(square2()), VertexIndex(99));
    CHECK(bad_candidate.has_value() == false);
    CHECK(bad_candidate.error().code == JoinErrorCode::candidate_vertex_out_of_range);
    CHECK(arrangement.entries().size() == 1);
}

TEST_CASE("vertex join reports translation overflow at the lattice bound") {
    Arrangement arrangement;
    // Anchor pinned to the minimum x edge of the lattice.
    const auto anchor = arrangement.try_insert(place(square2(), INT64_MIN, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();

    // t = a - b with a.x = INT64_MIN and b.x = 2 underflows before any footprint
    // is built.
    const VertexIndex anchor_vertex = find_vertex(anchor_footprint, raw(INT64_MIN, 0));
    const OrientedPrototile candidate = reference_orientation(square2());
    const VertexIndex candidate_vertex = find_vertex(candidate.canonical_polygon(), raw(2, 0));

    auto joined =
        arrangement.try_join_vertices(anchor, anchor_vertex, candidate, candidate_vertex);
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == JoinErrorCode::translation_overflow);
    CHECK(arrangement.entries().size() == 1);
}

TEST_CASE("vertex join reports footprint overflow past the lattice bound") {
    Arrangement arrangement;
    // Anchor whose corner (INT64_MAX-1, 0) sits at the top of the range.
    const auto anchor = arrangement.try_insert(place(square2(), INT64_MAX - 3, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();

    // Mate that corner with the candidate corner (0,0): t is representable, but
    // translating the candidate corner (2,0) then lands past INT64_MAX.
    const VertexIndex anchor_vertex = find_vertex(anchor_footprint, raw(INT64_MAX - 1, 0));
    const OrientedPrototile candidate = reference_orientation(square2());
    const VertexIndex candidate_vertex = find_vertex(candidate.canonical_polygon(), raw(0, 0));

    auto joined =
        arrangement.try_join_vertices(anchor, anchor_vertex, candidate, candidate_vertex);
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == JoinErrorCode::footprint_overflow);
    CHECK(arrangement.entries().size() == 1);
}

TEST_CASE("vertex join that aligns but overlaps the anchor is rejected") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square4(), 0, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();

    // Mate the anchor corner (0,0) with the candidate corner (0,0): t = 0, so the
    // candidate footprint coincides with the anchor and overlaps it entirely.
    const VertexIndex anchor_vertex = find_vertex(anchor_footprint, raw(0, 0));
    const OrientedPrototile candidate = reference_orientation(square4());
    const VertexIndex candidate_vertex = find_vertex(candidate.canonical_polygon(), raw(0, 0));

    auto joined =
        arrangement.try_join_vertices(anchor, anchor_vertex, candidate, candidate_vertex);
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == JoinErrorCode::interior_overlap);
    CHECK(joined.error().conflicting_placement.value() == anchor);
    CHECK(arrangement.entries().size() == 1);
    CHECK(arrangement.next_id().value() == PlacementId(1));
}

TEST_CASE("vertex join that clears the anchor but overlaps another placement is rejected") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square4(), 0, 0)).value();
    // A blocker clear of the anchor, to its right.
    const auto blocker = arrangement.try_insert(place(square2(), 5, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();

    // Mate the anchor corner (4,0) with the candidate corner (0,0): t = (4,0), so
    // the 4x4 candidate lands at [4,8]x[0,4], sharing only edge x=4 with the
    // anchor but covering the blocker at [5,7]x[0,2].
    const VertexIndex anchor_vertex = find_vertex(anchor_footprint, raw(4, 0));
    const OrientedPrototile candidate = reference_orientation(square4());
    const VertexIndex candidate_vertex = find_vertex(candidate.canonical_polygon(), raw(0, 0));

    auto joined =
        arrangement.try_join_vertices(anchor, anchor_vertex, candidate, candidate_vertex);
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == JoinErrorCode::interior_overlap);
    CHECK(joined.error().conflicting_placement.value() == blocker);
    CHECK(arrangement.entries().size() == 2);
}

TEST_CASE("vertex join yielding an additional full-edge contact is legal") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square4(), 0, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();

    // Mate the anchor corner (4,0) with the candidate corner (0,0): the 4x4
    // candidate lands at [4,8]x[0,4], sharing the entire edge x=4 with the anchor.
    const VertexIndex anchor_vertex = find_vertex(anchor_footprint, raw(4, 0));
    const OrientedPrototile candidate = reference_orientation(square4());
    const VertexIndex candidate_vertex = find_vertex(candidate.canonical_polygon(), raw(0, 0));

    auto joined =
        arrangement.try_join_vertices(anchor, anchor_vertex, candidate, candidate_vertex);
    CHECK(joined.has_value());
    CHECK(arrangement.entries().size() == 2);
    const Placement &placed = arrangement.entries().back().placement;
    CHECK(footprint_has_vertex(placed, raw(4, 0)));
    CHECK(footprint_has_vertex(placed, raw(4, 4)));
}

TEST_CASE("vertex join yielding an additional partial-edge contact is legal") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square4(), 0, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();

    // Mate the anchor corner (4,0) with a 2x2 candidate corner (0,0): the smaller
    // candidate lands at [4,6]x[0,2], sharing only the lower half of the anchor's
    // right edge - a partial-edge contact.
    const VertexIndex anchor_vertex = find_vertex(anchor_footprint, raw(4, 0));
    const OrientedPrototile candidate = reference_orientation(square2());
    const VertexIndex candidate_vertex = find_vertex(candidate.canonical_polygon(), raw(0, 0));

    auto joined =
        arrangement.try_join_vertices(anchor, anchor_vertex, candidate, candidate_vertex);
    CHECK(joined.has_value());
    CHECK(arrangement.entries().size() == 2);
    const Placement &placed = arrangement.entries().back().placement;
    CHECK(footprint_has_vertex(placed, raw(4, 0)));
    CHECK(footprint_has_vertex(placed, raw(6, 2)));
}

TEST_CASE("vertex join with contact at several boundary features is legal") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(c_shape(), 0, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();

    // Mate the back-of-cavity corner (2,2) with the candidate corner (0,0): the
    // 2x2 candidate fills the left half of the cavity at [2,4]x[2,4], touching the
    // back wall (x=2), the floor (y=2), and the ceiling (y=4) all at once, with no
    // interior overlap.
    const VertexIndex anchor_vertex = find_vertex(anchor_footprint, raw(2, 2));
    const OrientedPrototile candidate = reference_orientation(square2());
    const VertexIndex candidate_vertex = find_vertex(candidate.canonical_polygon(), raw(0, 0));

    auto joined =
        arrangement.try_join_vertices(anchor, anchor_vertex, candidate, candidate_vertex);
    CHECK(joined.has_value());
    CHECK(arrangement.entries().size() == 2);
    const Placement &placed = arrangement.entries().back().placement;
    CHECK(placed.translation() == raw(2, 2));
    CHECK(footprint_has_vertex(placed, raw(2, 2)));
    CHECK(footprint_has_vertex(placed, raw(4, 4)));
}

TEST_CASE("vertex join reports identifier exhaustion without corrupting the arrangement") {
    Arrangement arrangement = Arrangement::testing_with_next_id(UINT64_MAX);
    // Consume the final id on the anchor itself.
    const auto anchor = arrangement.try_insert(place(square2(), 0, 0)).value();
    CHECK(anchor == PlacementId(UINT64_MAX));
    CHECK(arrangement.next_id().has_value() == false);

    // A legal point-contact vertex join now has no id to allocate.
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();
    const VertexIndex anchor_vertex = find_vertex(anchor_footprint, raw(2, 2));
    const OrientedPrototile candidate = reference_orientation(square2());
    const VertexIndex candidate_vertex = find_vertex(candidate.canonical_polygon(), raw(0, 0));

    auto joined =
        arrangement.try_join_vertices(anchor, anchor_vertex, candidate, candidate_vertex);
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == JoinErrorCode::identifier_exhausted);
    CHECK(joined.error().conflicting_placement.has_value() == false);
    CHECK(arrangement.entries().size() == 1);
}

TEST_CASE("a failed vertex join leaves entries, ordering, ids, and the allocator unchanged") {
    Arrangement arrangement;
    const auto id0 = arrangement.try_insert(place(square4(), 0, 0)).value();
    const auto id1 = arrangement.try_insert(place(square2(), 10, 0)).value();

    const std::size_t before_size = arrangement.entries().size();
    const PlacementId before_next = arrangement.next_id().value();

    // An overlapping vertex join (t = 0 onto the anchor) must change nothing.
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();
    const VertexIndex anchor_vertex = find_vertex(anchor_footprint, raw(0, 0));
    const OrientedPrototile candidate = reference_orientation(square4());
    const VertexIndex candidate_vertex = find_vertex(candidate.canonical_polygon(), raw(0, 0));

    auto rejected =
        arrangement.try_join_vertices(id0, anchor_vertex, candidate, candidate_vertex);
    CHECK(rejected.has_value() == false);

    CHECK(arrangement.entries().size() == before_size);
    CHECK(arrangement.next_id().value() == before_next);
    CHECK(arrangement.entries()[0].id == id0);
    CHECK(arrangement.entries()[1].id == id1);
}
