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

// Complete physical identity of two placements: prototile, orientation, exact
// translation, and the whole ordered footprint boundary.
bool same_geometry(const Placement &p_lhs, const Placement &p_rhs) {
    return p_lhs.prototile().id() == p_rhs.prototile().id()
        && p_lhs.orientation() == p_rhs.orientation()
        && p_lhs.translation() == p_rhs.translation()
        && p_lhs.footprint().vertices() == p_rhs.footprint().vertices();
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

// ---------------------------------------------------------------------------
// vertex preview
// ---------------------------------------------------------------------------

TEST_CASE("vertex preview returns the exact placement without mutating anything") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square2(), 0, 0)).value();
    const VertexIndex anchor_vertex =
        find_vertex(arrangement.entries().front().placement.footprint(), raw(2, 2));
    const OrientedPrototile candidate = reference_orientation(square2());
    const VertexIndex candidate_vertex =
        find_vertex(candidate.canonical_polygon(), raw(0, 0));

    const std::size_t before_size = arrangement.entries().size();
    const PlacementId before_next = arrangement.next_id().value();
    const Polygon::Vertices before_candidate = candidate.canonical_polygon().vertices();

    // Deliberately through a const reference: preview is an observation.
    const Arrangement &observed = arrangement;
    auto previewed =
        observed.preview_join_vertices(anchor, anchor_vertex, candidate, candidate_vertex);
    CHECK(previewed.has_value());
    if (!previewed) {
        return;
    }

    // The isolated point contact this act deliberately keeps available.
    CHECK(previewed.value().translation() == raw(2, 2));
    CHECK(previewed.value().prototile().id() == PrototileId(2));
    CHECK(previewed.value().orientation() == Orientation::reference());
    CHECK(footprint_has_vertex(previewed.value(), raw(2, 2)));
    CHECK(footprint_has_vertex(previewed.value(), raw(4, 4)));

    CHECK(arrangement.entries().size() == before_size);
    CHECK(arrangement.entries().front().id == anchor);
    CHECK(arrangement.next_id().value() == before_next);
    CHECK(candidate.orientation() == Orientation::reference());
    CHECK(candidate.canonical_polygon().vertices() == before_candidate);
}

TEST_CASE("repeated vertex preview is identical and agrees with an immediate join") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(c_shape(), 0, 0)).value();
    // The back-of-cavity corner: a vertex mating that yields several contacts.
    const VertexIndex anchor_vertex =
        find_vertex(arrangement.entries().front().placement.footprint(), raw(2, 2));
    const OrientedPrototile candidate = reference_orientation(square2());
    const VertexIndex candidate_vertex =
        find_vertex(candidate.canonical_polygon(), raw(0, 0));

    auto first =
        arrangement.preview_join_vertices(anchor, anchor_vertex, candidate, candidate_vertex);
    auto second =
        arrangement.preview_join_vertices(anchor, anchor_vertex, candidate, candidate_vertex);
    CHECK(first.has_value());
    CHECK(second.has_value());
    if (!first || !second) {
        return;
    }
    CHECK(same_geometry(first.value(), second.value()));

    auto joined =
        arrangement.try_join_vertices(anchor, anchor_vertex, candidate, candidate_vertex);
    CHECK(joined.has_value());
    if (!joined) {
        return;
    }
    CHECK(arrangement.entries().size() == 2);
    CHECK(arrangement.entries().back().id == joined.value());
    CHECK(same_geometry(arrangement.entries().back().placement, first.value()));
}

TEST_CASE("vertex preview retains anchor and vertex precedence") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square2(), 0, 0)).value();
    const VertexIndex good =
        find_vertex(arrangement.entries().front().placement.footprint(), raw(0, 0));
    const OrientedPrototile candidate = reference_orientation(square2());

    auto missing = arrangement.preview_join_vertices(
        PlacementId(999), VertexIndex(50), candidate, VertexIndex(50));
    CHECK(missing.has_value() == false);
    CHECK(missing.error().code == JoinErrorCode::anchor_not_found);

    auto bad_anchor =
        arrangement.preview_join_vertices(anchor, VertexIndex(99), candidate, good);
    CHECK(bad_anchor.has_value() == false);
    CHECK(bad_anchor.error().code == JoinErrorCode::anchor_vertex_out_of_range);

    auto bad_candidate =
        arrangement.preview_join_vertices(anchor, good, candidate, VertexIndex(99));
    CHECK(bad_candidate.has_value() == false);
    CHECK(bad_candidate.error().code == JoinErrorCode::candidate_vertex_out_of_range);

    CHECK(arrangement.entries().size() == 1);
    CHECK(arrangement.next_id().value() == PlacementId(1));
}

TEST_CASE("vertex preview keeps translation and footprint overflow typed") {
    Arrangement translation_bound;
    const auto low = translation_bound.try_insert(place(square2(), INT64_MIN, 0)).value();
    const OrientedPrototile candidate = reference_orientation(square2());
    auto underflow = translation_bound.preview_join_vertices(
        low,
        find_vertex(
            translation_bound.entries().front().placement.footprint(), raw(INT64_MIN, 0)),
        candidate,
        find_vertex(candidate.canonical_polygon(), raw(2, 0)));
    CHECK(underflow.has_value() == false);
    CHECK(underflow.error().code == JoinErrorCode::translation_overflow);
    CHECK(translation_bound.entries().size() == 1);

    Arrangement footprint_bound;
    const auto high = footprint_bound.try_insert(place(square2(), INT64_MAX - 3, 0)).value();
    auto overflow = footprint_bound.preview_join_vertices(
        high,
        find_vertex(
            footprint_bound.entries().front().placement.footprint(), raw(INT64_MAX - 1, 0)),
        candidate,
        find_vertex(candidate.canonical_polygon(), raw(0, 0)));
    CHECK(overflow.has_value() == false);
    CHECK(overflow.error().code == JoinErrorCode::footprint_overflow);
    CHECK(footprint_bound.entries().size() == 1);
}

TEST_CASE("vertex preview names the same conflicting placement as mutation") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square4(), 0, 0)).value();
    const auto blocker = arrangement.try_insert(place(square2(), 5, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();
    const OrientedPrototile candidate = reference_orientation(square4());
    const VertexIndex candidate_origin =
        find_vertex(candidate.canonical_polygon(), raw(0, 0));

    // t = 0 lands the candidate exactly on the anchor.
    auto onto_anchor = arrangement.preview_join_vertices(
        anchor, find_vertex(anchor_footprint, raw(0, 0)), candidate, candidate_origin);
    CHECK(onto_anchor.has_value() == false);
    CHECK(onto_anchor.error().code == JoinErrorCode::interior_overlap);
    CHECK(onto_anchor.error().conflicting_placement.value() == anchor);

    // Clearing the anchor, the candidate still covers the blocker.
    const VertexIndex corner = find_vertex(anchor_footprint, raw(4, 0));
    auto onto_blocker =
        arrangement.preview_join_vertices(anchor, corner, candidate, candidate_origin);
    CHECK(onto_blocker.has_value() == false);
    CHECK(onto_blocker.error().code == JoinErrorCode::interior_overlap);
    CHECK(onto_blocker.error().conflicting_placement.value() == blocker);

    auto joined = arrangement.try_join_vertices(anchor, corner, candidate, candidate_origin);
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == onto_blocker.error().code);
    CHECK(joined.error().conflicting_placement.value() == blocker);

    CHECK(arrangement.entries().size() == 2);
    CHECK(arrangement.next_id().value() == PlacementId(2));
}

TEST_CASE("vertex preview reports identifier exhaustion without mutation") {
    Arrangement arrangement = Arrangement::testing_with_next_id(UINT64_MAX);
    const auto anchor = arrangement.try_insert(place(square2(), 0, 0)).value();
    CHECK(arrangement.next_id().has_value() == false);

    const OrientedPrototile candidate = reference_orientation(square2());
    auto previewed = arrangement.preview_join_vertices(
        anchor,
        find_vertex(arrangement.entries().front().placement.footprint(), raw(2, 2)),
        candidate,
        find_vertex(candidate.canonical_polygon(), raw(0, 0)));
    CHECK(previewed.has_value() == false);
    CHECK(previewed.error().code == JoinErrorCode::identifier_exhausted);
    CHECK(previewed.error().conflicting_placement.has_value() == false);
    CHECK(arrangement.entries().size() == 1);
}
