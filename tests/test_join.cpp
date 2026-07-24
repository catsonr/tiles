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

// Compile one specific orientation of a prototile.
OrientedPrototile oriented(const Prototile &p_prototile, Orientation p_orientation) {
    auto compiled = compile_lattice_orientations(p_prototile, { p_orientation });
    return std::move(compiled).value().front();
}

Prototile square4() {
    return proto(1, { raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) });
}

Prototile square2() {
    return proto(2, { raw(0, 0), raw(2, 0), raw(2, 2), raw(0, 2) });
}

Prototile square6() {
    return proto(3, { raw(0, 0), raw(6, 0), raw(6, 6), raw(0, 6) });
}

// A 2-wide, 4-tall rectangle: its length-4 sides are vertical in the reference
// orientation and horizontal after a quarter turn.
Prototile rect2x4() {
    return proto(5, { raw(0, 0), raw(2, 0), raw(2, 4), raw(0, 4) });
}

// A C-shape opening to the right: outer bounds [0,6]^2 with a cavity at
// x in [2,6], y in [2,4]. Concave, already canonical.
Prototile c_shape() {
    return proto(4, {
        raw(0, 0), raw(6, 0), raw(6, 2), raw(2, 2),
        raw(2, 4), raw(6, 4), raw(6, 6), raw(0, 6)
    });
}

Placement place(const Prototile &p_proto, std::int64_t p_x, std::int64_t p_y) {
    auto placement = Placement::make(reference_orientation(p_proto), raw(p_x, p_y));
    return std::move(placement).value();
}

// Locate the edge running from p_from to p_to in a polygon's canonical order,
// so tests never depend on where canonicalization places index zero.
EdgeIndex find_edge(const Polygon &p_polygon, Point p_from, Point p_to) {
    const Polygon::Vertices &v = p_polygon.vertices();
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == p_from && v[(i + 1) % v.size()] == p_to) {
            return EdgeIndex(i);
        }
    }
    // The test asked for an edge that does not exist; fail loudly by returning
    // an out-of-range index.
    return EdgeIndex(v.size());
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

TEST_CASE("join with exact opposite edge vectors succeeds") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square4(), 0, 0)).value();

    // Anchor bottom edge (0,0)->(4,0) mates the candidate top edge (4,4)->(0,4).
    const EdgeIndex anchor_edge =
        find_edge(arrangement.entries().front().placement.footprint(), raw(0, 0), raw(4, 0));
    const OrientedPrototile candidate = reference_orientation(square4());
    const EdgeIndex candidate_edge =
        find_edge(candidate.canonical_polygon(), raw(4, 4), raw(0, 4));

    auto joined =
        arrangement.try_join_full_edges(anchor, anchor_edge, candidate, candidate_edge);
    CHECK(joined.has_value());
    CHECK(joined.value() == PlacementId(1));
    CHECK(arrangement.entries().size() == 2);

    // The mated edge endpoints coincide exactly in world space.
    const Placement &placed = arrangement.entries().back().placement;
    CHECK(footprint_has_vertex(placed, raw(0, 0)));
    CHECK(footprint_has_vertex(placed, raw(4, 0)));
    CHECK(placed.translation() == raw(0, -4));
}

TEST_CASE("join with equal-length edges of the same direction is incompatible") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square4(), 0, 0)).value();
    const EdgeIndex anchor_edge =
        find_edge(arrangement.entries().front().placement.footprint(), raw(0, 0), raw(4, 0));
    const OrientedPrototile candidate = reference_orientation(square4());
    // Same direction (4,0), not the opposite: cannot mate.
    const EdgeIndex candidate_edge =
        find_edge(candidate.canonical_polygon(), raw(0, 0), raw(4, 0));

    auto joined =
        arrangement.try_join_full_edges(anchor, anchor_edge, candidate, candidate_edge);
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == JoinErrorCode::incompatible_edges);
    CHECK(arrangement.entries().size() == 1);
}

TEST_CASE("join with unequal edge vectors is incompatible") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square4(), 0, 0)).value();
    const EdgeIndex anchor_edge =
        find_edge(arrangement.entries().front().placement.footprint(), raw(0, 0), raw(4, 0));
    const OrientedPrototile candidate = reference_orientation(square6());
    // Opposite direction but length 6 versus length 4.
    const EdgeIndex candidate_edge =
        find_edge(candidate.canonical_polygon(), raw(6, 6), raw(0, 6));

    auto joined =
        arrangement.try_join_full_edges(anchor, anchor_edge, candidate, candidate_edge);
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == JoinErrorCode::incompatible_edges);
}

TEST_CASE("join with a missing anchor fails before edge validation") {
    Arrangement arrangement;
    arrangement.try_insert(place(square4(), 0, 0));

    // Nonexistent anchor together with both edge indices out of range: the
    // anchor is resolved first.
    auto joined = arrangement.try_join_full_edges(
        PlacementId(999), EdgeIndex(50), reference_orientation(square4()), EdgeIndex(50));
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == JoinErrorCode::anchor_not_found);
}

TEST_CASE("join reports each out-of-range edge index") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square4(), 0, 0)).value();
    const EdgeIndex good =
        find_edge(arrangement.entries().front().placement.footprint(), raw(0, 0), raw(4, 0));

    auto bad_anchor = arrangement.try_join_full_edges(
        anchor, EdgeIndex(99), reference_orientation(square4()), good);
    CHECK(bad_anchor.has_value() == false);
    CHECK(bad_anchor.error().code == JoinErrorCode::anchor_edge_out_of_range);

    auto bad_candidate = arrangement.try_join_full_edges(
        anchor, good, reference_orientation(square4()), EdgeIndex(99));
    CHECK(bad_candidate.has_value() == false);
    CHECK(bad_candidate.error().code == JoinErrorCode::candidate_edge_out_of_range);
}

TEST_CASE("join reports translation overflow at the lattice bound") {
    Arrangement arrangement;
    // Anchor pinned to the minimum x edge of the lattice.
    const auto anchor = arrangement.try_insert(place(square4(), INT64_MIN, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();
    // Anchor left edge, going down: mating a rightward-up candidate edge derives
    // t = a0 - b1 with t.x = INT64_MIN - 4, which underflows.
    const EdgeIndex anchor_edge =
        find_edge(anchor_footprint, raw(INT64_MIN, 4), raw(INT64_MIN, 0));
    const OrientedPrototile candidate = reference_orientation(square4());
    const EdgeIndex candidate_edge =
        find_edge(candidate.canonical_polygon(), raw(4, 0), raw(4, 4));

    auto joined =
        arrangement.try_join_full_edges(anchor, anchor_edge, candidate, candidate_edge);
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == JoinErrorCode::translation_overflow);
    CHECK(arrangement.entries().size() == 1);
}

TEST_CASE("join that aligns but overlaps the anchor elsewhere is rejected") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(c_shape(), 0, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();

    // Mate the cavity floor (6,2)->(2,2) with a 4x4 square whose bottom edge is
    // compatible. The square rises out of the cavity and overlaps the C's top
    // bar: a valid full-edge alignment that still fails the footprint proof.
    const EdgeIndex anchor_edge = find_edge(anchor_footprint, raw(6, 2), raw(2, 2));
    const OrientedPrototile candidate = reference_orientation(square4());
    const EdgeIndex candidate_edge =
        find_edge(candidate.canonical_polygon(), raw(0, 0), raw(4, 0));

    auto joined =
        arrangement.try_join_full_edges(anchor, anchor_edge, candidate, candidate_edge);
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == JoinErrorCode::interior_overlap);
    CHECK(joined.error().conflicting_placement.has_value());
    CHECK(joined.error().conflicting_placement.value() == anchor);
    // Failed join leaves the arrangement and allocator untouched.
    CHECK(arrangement.entries().size() == 1);
    CHECK(arrangement.next_id().value() == PlacementId(1));
}

TEST_CASE("join that clears the anchor but overlaps another placement is rejected") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(c_shape(), 0, 0)).value();
    // A blocker to the right of the C, clear of it.
    const auto blocker = arrangement.try_insert(place(square2(), 7, 4)).value();

    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();
    // Mate the top prong's right face (6,4)->(6,6); the 2x2 candidate lands at
    // [6,8]x[4,6], only touching the anchor but overlapping the blocker.
    const EdgeIndex anchor_edge = find_edge(anchor_footprint, raw(6, 4), raw(6, 6));
    const OrientedPrototile candidate = reference_orientation(square2());
    const EdgeIndex candidate_edge =
        find_edge(candidate.canonical_polygon(), raw(0, 2), raw(0, 0));

    auto joined =
        arrangement.try_join_full_edges(anchor, anchor_edge, candidate, candidate_edge);
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == JoinErrorCode::interior_overlap);
    CHECK(joined.error().conflicting_placement.value() == blocker);
    CHECK(arrangement.entries().size() == 2);
}

TEST_CASE("join with legal contact against non-anchor placements succeeds") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(c_shape(), 0, 0)).value();
    // A neighbor touching the C only at the corner (6,6).
    arrangement.try_insert(place(square2(), 6, 6));

    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();
    const EdgeIndex anchor_edge = find_edge(anchor_footprint, raw(6, 4), raw(6, 6));
    const OrientedPrototile candidate = reference_orientation(square2());
    const EdgeIndex candidate_edge =
        find_edge(candidate.canonical_polygon(), raw(0, 2), raw(0, 0));

    // Candidate lands at [6,8]x[4,6]: mates the anchor edge and shares the edge
    // y == 6 with the neighbor, both legal boundary contact.
    auto joined =
        arrangement.try_join_full_edges(anchor, anchor_edge, candidate, candidate_edge);
    CHECK(joined.has_value());
    CHECK(joined.value() == PlacementId(2));
    CHECK(arrangement.entries().size() == 3);
}

TEST_CASE("a full-edge join incompatible in reference succeeds after a quarter turn") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(square4(), 0, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();
    const EdgeIndex anchor_edge = find_edge(anchor_footprint, raw(0, 0), raw(4, 0));

    // In the reference orientation the rectangle's only length-4 sides are
    // vertical, so no candidate edge opposes the anchor's length-4 bottom.
    const OrientedPrototile reference = reference_orientation(rect2x4());
    const EdgeIndex reference_edge =
        find_edge(reference.canonical_polygon(), raw(2, 4), raw(0, 4));
    auto rejected =
        arrangement.try_join_full_edges(anchor, anchor_edge, reference, reference_edge);
    CHECK(rejected.has_value() == false);
    CHECK(rejected.error().code == JoinErrorCode::incompatible_edges);
    CHECK(arrangement.entries().size() == 1);

    // A quarter turn makes those length-4 sides horizontal; the top edge
    // (4,2)->(0,2) now opposes the anchor bottom exactly.
    const OrientedPrototile turned = oriented(rect2x4(), Orientation::quarter());
    const EdgeIndex turned_edge =
        find_edge(turned.canonical_polygon(), raw(4, 2), raw(0, 2));
    auto joined =
        arrangement.try_join_full_edges(anchor, anchor_edge, turned, turned_edge);
    CHECK(joined.has_value());
    CHECK(arrangement.entries().size() == 2);

    // The candidate drops below the anchor and its mated endpoints coincide with
    // the anchor bottom exactly.
    const Placement &placed = arrangement.entries().back().placement;
    CHECK(placed.orientation() == Orientation::quarter());
    CHECK(placed.translation() == raw(0, -2));
    CHECK(footprint_has_vertex(placed, raw(0, 0)));
    CHECK(footprint_has_vertex(placed, raw(4, 0)));
}

TEST_CASE("a mixed-orientation join chain shares raw endpoints without drift") {
    Arrangement arrangement;
    PlacementId anchor = arrangement.try_insert(place(square4(), 0, 0)).value();

    // Cycle the requested orientation each step. A square's four quarter turns
    // are geometrically identical, so every candidate mates the same way while
    // carrying a different representative orientation: the chain proves exact
    // coordinates regardless of the orientation label.
    const Orientation cycle[4] = {
        Orientation::reference(), Orientation::quarter(),
        Orientation::half(), Orientation::three_quarter()
    };

    const int steps = 32;
    for (int k = 0; k < steps; ++k) {
        const std::int64_t x = static_cast<std::int64_t>(k) * 4;
        const Polygon &anchor_footprint = arrangement.entries().back().placement.footprint();
        const EdgeIndex anchor_edge =
            find_edge(anchor_footprint, raw(x + 4, 0), raw(x + 4, 4));
        const OrientedPrototile candidate = oriented(square4(), cycle[k % 4]);
        const EdgeIndex candidate_edge =
            find_edge(candidate.canonical_polygon(), raw(0, 4), raw(0, 0));

        auto joined =
            arrangement.try_join_full_edges(anchor, anchor_edge, candidate, candidate_edge);
        CHECK(joined.has_value());
        anchor = joined.value();

        const Placement &placed = arrangement.entries().back().placement;
        CHECK(placed.orientation() == cycle[k % 4]);
        CHECK(footprint_has_vertex(placed, raw(x + 4, 0)));
        CHECK(footprint_has_vertex(placed, raw(x + 8, 0)));
    }

    CHECK(arrangement.entries().size() == static_cast<std::size_t>(steps + 1));
    const Placement &last = arrangement.entries().back().placement;
    CHECK(last.translation() == raw(static_cast<std::int64_t>(steps) * 4, 0));
}

TEST_CASE("a failed full-edge join preserves the candidate orientation and the arrangement") {
    Arrangement arrangement;
    const auto anchor = arrangement.try_insert(place(c_shape(), 0, 0)).value();
    const Polygon &anchor_footprint = arrangement.entries().front().placement.footprint();

    // A quarter-turned square (geometrically identical to the reference square)
    // mates the cavity floor but overlaps the C's top bar.
    const OrientedPrototile candidate = oriented(square4(), Orientation::quarter());
    const EdgeIndex anchor_edge = find_edge(anchor_footprint, raw(6, 2), raw(2, 2));
    const EdgeIndex candidate_edge =
        find_edge(candidate.canonical_polygon(), raw(0, 0), raw(4, 0));

    auto joined =
        arrangement.try_join_full_edges(anchor, anchor_edge, candidate, candidate_edge);
    CHECK(joined.has_value() == false);
    CHECK(joined.error().code == JoinErrorCode::interior_overlap);

    // The candidate value is untouched by a failed join and remains reusable.
    CHECK(candidate.orientation() == Orientation::quarter());
    CHECK(arrangement.entries().size() == 1);
    CHECK(arrangement.next_id().value() == PlacementId(1));
    auto reused = Placement::make(candidate, raw(50, 50));
    CHECK(reused.has_value());
    CHECK(reused.value().orientation() == Orientation::quarter());
}

TEST_CASE("a long join chain shares raw endpoints without drift") {
    Arrangement arrangement;
    PlacementId anchor = arrangement.try_insert(place(square4(), 0, 0)).value();

    const int steps = 64;
    for (int k = 0; k < steps; ++k) {
        const std::int64_t x = static_cast<std::int64_t>(k) * 4;
        // The current anchor's right edge and the next square's left edge.
        const Polygon &anchor_footprint = arrangement.entries().back().placement.footprint();
        const EdgeIndex anchor_edge =
            find_edge(anchor_footprint, raw(x + 4, 0), raw(x + 4, 4));
        const OrientedPrototile candidate = reference_orientation(square4());
        const EdgeIndex candidate_edge =
            find_edge(candidate.canonical_polygon(), raw(0, 4), raw(0, 0));

        auto joined =
            arrangement.try_join_full_edges(anchor, anchor_edge, candidate, candidate_edge);
        CHECK(joined.has_value());
        anchor = joined.value();

        // Every tile's shared corners are exact integer multiples of the step:
        // no accumulated transform or quantization drift.
        const Placement &placed = arrangement.entries().back().placement;
        CHECK(footprint_has_vertex(placed, raw(x + 4, 0)));
        CHECK(footprint_has_vertex(placed, raw(x + 4, 4)));
        CHECK(footprint_has_vertex(placed, raw(x + 8, 0)));
    }

    CHECK(arrangement.entries().size() == static_cast<std::size_t>(steps + 1));
    // The final tile sits exactly where exact arithmetic places it.
    const Placement &last = arrangement.entries().back().placement;
    CHECK(last.translation() == raw(static_cast<std::int64_t>(steps) * 4, 0));
}
