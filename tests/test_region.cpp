#include "TestHarness.h"

#include "core/Region.h"
#include "core/geometry/ExactInteger.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"

#include <cstdint>
#include <utility>
#include <vector>

using namespace tiles;
using tiles_test::raw_pt;

namespace {

Polygon poly(std::vector<Point> p_vertices) {
    auto made = Polygon::make(std::move(p_vertices));
    return std::move(made).value();
}

// An axis-aligned rectangle on raw lattice units, given by opposite corners.
Polygon box(std::int64_t p_min_x, std::int64_t p_min_y, std::int64_t p_max_x, std::int64_t p_max_y) {
    return poly({
        raw_pt(p_min_x, p_min_y),
        raw_pt(p_max_x, p_min_y),
        raw_pt(p_max_x, p_max_y),
        raw_pt(p_min_x, p_max_y),
    });
}

// The 20 x 20 outer boundary every fixture below shares.
Polygon outer20() {
    return box(0, 0, 20, 20);
}

// A strongly concave outer boundary: the same C used by the containment tests,
// scaled up so holes and footprints fit inside its bars.
Polygon concave_outer() {
    return poly({
        raw_pt(0, 0),
        raw_pt(24, 0),
        raw_pt(24, 8),
        raw_pt(8, 8),
        raw_pt(8, 16),
        raw_pt(24, 16),
        raw_pt(24, 24),
        raw_pt(0, 24),
    });
}

Region region_of(Polygon p_outer, std::vector<Polygon> p_holes) {
    auto made = Region::make(std::move(p_outer), std::move(p_holes));
    return std::move(made).value();
}

bool same_vertices(const Polygon &p_lhs, const Polygon &p_rhs) {
    return p_lhs.vertices() == p_rhs.vertices();
}

} // namespace

// ---------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------

TEST_CASE("a region with no holes is valid and owns its outer boundary") {
    auto made = Region::make(outer20(), {});
    CHECK(bool(made));
    if (!made) {
        return;
    }
    const Region &region = made.value();
    CHECK(same_vertices(region.outer_boundary(), outer20()));
    CHECK(region.inner_boundaries().empty());
    // 20 x 20 = 400, doubled.
    CHECK(region.doubled_area() == Int256::from_i64(800));
}

TEST_CASE("valid holes are accepted and their authored order is preserved") {
    std::vector<Polygon> holes;
    holes.push_back(box(12, 12, 16, 16));
    holes.push_back(box(4, 4, 8, 8));
    holes.push_back(box(4, 12, 6, 14));

    auto made = Region::make(outer20(), std::move(holes));
    CHECK(bool(made));
    if (!made) {
        return;
    }
    const Region &region = made.value();
    CHECK(region.inner_boundaries().size() == 3);
    // Exactly as authored: the order is deterministic, not sorted or normalized.
    CHECK(same_vertices(region.inner_boundaries()[0], box(12, 12, 16, 16)));
    CHECK(same_vertices(region.inner_boundaries()[1], box(4, 4, 8, 8)));
    CHECK(same_vertices(region.inner_boundaries()[2], box(4, 12, 6, 14)));
}

TEST_CASE("region doubled area is exact outer area minus every hole area") {
    std::vector<Polygon> one;
    one.push_back(box(4, 4, 8, 8));
    auto single = Region::make(outer20(), std::move(one));
    CHECK(bool(single));
    if (single) {
        // 800 - 2 * (4 x 4).
        CHECK(single.value().doubled_area() == Int256::from_i64(800 - 32));
    }

    std::vector<Polygon> several;
    several.push_back(box(4, 4, 8, 8));
    several.push_back(box(12, 12, 16, 16));
    several.push_back(box(2, 14, 4, 18));
    auto many = Region::make(outer20(), std::move(several));
    CHECK(bool(many));
    if (many) {
        CHECK(many.value().doubled_area() == Int256::from_i64(800 - 32 - 32 - 16));
    }

    // A concave outer boundary with a hole: 24 x 24 minus the 16 x 8 mouth is
    // 576 - 128 = 448, doubled 896, less a doubled 4 x 4 hole.
    std::vector<Polygon> concave_hole;
    concave_hole.push_back(box(2, 10, 6, 14));
    auto concave = Region::make(concave_outer(), std::move(concave_hole));
    CHECK(bool(concave));
    if (concave) {
        CHECK(concave.value().doubled_area() == Int256::from_i64(896 - 32));
    }
}

TEST_CASE("a hole outside, crossing, equal to, or containing the outer boundary is rejected") {
    const auto rejected = [](Polygon p_hole) {
        std::vector<Polygon> holes;
        holes.push_back(std::move(p_hole));
        return Region::make(outer20(), std::move(holes));
    };

    // Entirely outside.
    auto outside = rejected(box(30, 30, 34, 34));
    CHECK(!outside);
    if (!outside) {
        CHECK(outside.error().code == RegionErrorCode::hole_not_strictly_inside_outer);
        CHECK(outside.error().first_hole == 0);
        CHECK(outside.error().second_hole.has_value() == false);
    }

    // Crossing the boundary.
    auto crossing = rejected(box(16, 16, 24, 24));
    CHECK(!crossing);
    if (!crossing) {
        CHECK(crossing.error().code == RegionErrorCode::hole_not_strictly_inside_outer);
    }

    // Equal to the outer boundary.
    auto equal = rejected(box(0, 0, 20, 20));
    CHECK(!equal);
    if (!equal) {
        CHECK(equal.error().code == RegionErrorCode::hole_not_strictly_inside_outer);
    }

    // Containing the outer boundary.
    auto containing = rejected(box(-4, -4, 24, 24));
    CHECK(!containing);
    if (!containing) {
        CHECK(containing.error().code == RegionErrorCode::hole_not_strictly_inside_outer);
    }
}

TEST_CASE("a hole touching the outer boundary at a point or an edge is rejected") {
    const auto rejected = [](Polygon p_hole) {
        std::vector<Polygon> holes;
        holes.push_back(std::move(p_hole));
        return Region::make(outer20(), std::move(holes));
    };

    // A single shared point at the corner (0, 0).
    auto corner = rejected(poly({ raw_pt(0, 0), raw_pt(4, 2), raw_pt(2, 4) }));
    CHECK(!corner);
    if (!corner) {
        CHECK(corner.error().code == RegionErrorCode::hole_not_strictly_inside_outer);
    }

    // A complete shared edge is equally rejected: the hole would open onto the
    // outer boundary and disconnect the positive area.
    auto complete_edge = rejected(box(0, 0, 20, 4));
    CHECK(!complete_edge);
    if (!complete_edge) {
        CHECK(complete_edge.error().code == RegionErrorCode::hole_not_strictly_inside_outer);
    }

    // And so is a partial shared edge.
    auto partial_edge = rejected(box(0, 4, 4, 8));
    CHECK(!partial_edge);
    if (!partial_edge) {
        CHECK(partial_edge.error().code == RegionErrorCode::hole_not_strictly_inside_outer);
    }
}

TEST_CASE("the first hole failing the outer-boundary check is reported deterministically") {
    std::vector<Polygon> holes;
    holes.push_back(box(2, 2, 4, 4));   // valid
    holes.push_back(box(18, 18, 26, 26)); // crosses the outer boundary
    holes.push_back(box(40, 40, 44, 44)); // entirely outside

    auto made = Region::make(outer20(), std::move(holes));
    CHECK(!made);
    if (!made) {
        CHECK(made.error().code == RegionErrorCode::hole_not_strictly_inside_outer);
        CHECK(made.error().first_hole == 1);
        CHECK(made.error().second_hole.has_value() == false);
    }
}

TEST_CASE("holes with separated boundaries and interiors are accepted") {
    std::vector<Polygon> holes;
    holes.push_back(box(2, 2, 6, 6));
    holes.push_back(box(10, 10, 14, 14));
    holes.push_back(box(2, 14, 6, 18));
    auto made = Region::make(outer20(), std::move(holes));
    CHECK(bool(made));
    if (made) {
        CHECK(made.value().inner_boundaries().size() == 3);
    }
}

TEST_CASE("every kind of hole-to-hole contact or overlap is rejected") {
    const auto rejected = [](Polygon p_first, Polygon p_second) {
        std::vector<Polygon> holes;
        holes.push_back(std::move(p_first));
        holes.push_back(std::move(p_second));
        return Region::make(outer20(), std::move(holes));
    };

    const auto check_pair = [](const Result<Region, RegionError> &p_made) {
        CHECK(!p_made);
        if (!p_made) {
            CHECK(p_made.error().code == RegionErrorCode::holes_not_disjoint);
            CHECK(p_made.error().first_hole == 0);
            CHECK(p_made.error().second_hole.has_value());
            if (p_made.error().second_hole.has_value()) {
                CHECK(p_made.error().second_hole.value() == 1);
            }
        }
    };

    // A single shared corner point.
    check_pair(rejected(box(4, 4, 8, 8), box(8, 8, 12, 12)));
    // A complete shared edge.
    check_pair(rejected(box(4, 4, 8, 8), box(8, 4, 12, 8)));
    // A partial shared edge.
    check_pair(rejected(box(4, 4, 8, 8), box(8, 5, 12, 7)));
    // A proper boundary crossing with positive-area overlap.
    check_pair(rejected(box(4, 4, 8, 8), box(6, 6, 10, 10)));
    // Identical holes.
    check_pair(rejected(box(4, 4, 8, 8), box(4, 4, 8, 8)));
    // One hole strictly inside another: disjoint boundaries, overlapping
    // interiors, so only the interior test can reject it.
    check_pair(rejected(box(4, 4, 12, 12), box(6, 6, 10, 10)));
    // And in the other nesting order.
    check_pair(rejected(box(6, 6, 10, 10), box(4, 4, 12, 12)));
}

TEST_CASE("the first offending hole pair is reported in lexicographic order") {
    std::vector<Polygon> holes;
    holes.push_back(box(2, 2, 6, 6));
    holes.push_back(box(14, 2, 18, 6));
    // Overlaps hole 0 and hole 1, so pairs (0, 2) and (1, 2) both fail; the
    // lexicographically first one is reported.
    holes.push_back(box(4, 4, 16, 12));

    auto made = Region::make(outer20(), std::move(holes));
    CHECK(!made);
    if (!made) {
        CHECK(made.error().code == RegionErrorCode::holes_not_disjoint);
        CHECK(made.error().first_hole == 0);
        CHECK(made.error().second_hole.has_value());
        if (made.error().second_hole.has_value()) {
            CHECK(made.error().second_hole.value() == 2);
        }
    }
}

TEST_CASE("outer-boundary validation precedes pair validation") {
    std::vector<Polygon> holes;
    // Holes 0 and 1 overlap each other, but hole 1 also escapes the outer
    // boundary. The outer check runs over every hole first, so the reported
    // error is the escaping hole, not the pair.
    holes.push_back(box(14, 14, 18, 18));
    holes.push_back(box(16, 16, 24, 24));

    auto made = Region::make(outer20(), std::move(holes));
    CHECK(!made);
    if (!made) {
        CHECK(made.error().code == RegionErrorCode::hole_not_strictly_inside_outer);
        CHECK(made.error().first_hole == 1);
        CHECK(made.error().second_hole.has_value() == false);
    }
}

// ---------------------------------------------------------------------------
// containment
// ---------------------------------------------------------------------------

TEST_CASE("a footprint strictly inside the positive area is contained") {
    std::vector<Polygon> holes;
    holes.push_back(box(4, 4, 8, 8));
    const Region region = region_of(outer20(), std::move(holes));

    CHECK(region.contains(box(1, 1, 3, 3)));
    CHECK(region.contains(box(10, 10, 18, 18)));
}

TEST_CASE("contact with the outer boundary is legal") {
    const Region region = region_of(outer20(), {});

    // A complete shared edge, a partial shared edge, and a single corner point.
    CHECK(region.contains(box(0, 0, 20, 4)));
    CHECK(region.contains(box(4, 0, 8, 4)));
    CHECK(region.contains(poly({ raw_pt(0, 0), raw_pt(4, 2), raw_pt(2, 4) })));
    // The whole outer polygon is contained by its own region.
    CHECK(region.contains(outer20()));
}

TEST_CASE("a footprint reaching beyond the outer boundary is rejected") {
    const Region region = region_of(outer20(), {});

    CHECK(region.contains(box(18, 18, 24, 24)) == false);
    CHECK(region.contains(box(-2, 8, 2, 12)) == false);
    CHECK(region.contains(box(30, 30, 34, 34)) == false);
}

TEST_CASE("a footprint inside, crossing, or containing a hole is rejected") {
    std::vector<Polygon> holes;
    holes.push_back(box(4, 4, 8, 8));
    const Region region = region_of(outer20(), std::move(holes));

    // Strictly inside the hole.
    CHECK(region.contains(box(5, 5, 7, 7)) == false);
    // Exactly the hole.
    CHECK(region.contains(box(4, 4, 8, 8)) == false);
    // Crossing the hole boundary.
    CHECK(region.contains(box(6, 6, 12, 12)) == false);
    // Containing the hole.
    CHECK(region.contains(box(2, 2, 10, 10)) == false);
}

TEST_CASE("contact with a hole boundary is legal") {
    std::vector<Polygon> holes;
    holes.push_back(box(4, 4, 8, 8));
    const Region region = region_of(outer20(), std::move(holes));

    // A complete shared hole edge.
    CHECK(region.contains(box(8, 4, 12, 8)));
    // A partial shared hole edge.
    CHECK(region.contains(box(8, 5, 12, 7)));
    // A single shared hole corner.
    CHECK(region.contains(box(8, 8, 12, 12)));
    // A concave footprint wrapping two complete hole edges without entering it.
    CHECK(region.contains(poly({
        raw_pt(2, 2),
        raw_pt(10, 2),
        raw_pt(10, 4),
        raw_pt(4, 4),
        raw_pt(4, 10),
        raw_pt(2, 10),
    })));
}

TEST_CASE("region containment considers every hole") {
    std::vector<Polygon> holes;
    holes.push_back(box(4, 4, 8, 8));
    holes.push_back(box(12, 12, 16, 16));
    holes.push_back(box(4, 12, 8, 16));
    const Region region = region_of(outer20(), std::move(holes));

    // Legal against all three, touching two of them at a corner.
    CHECK(region.contains(box(8, 8, 12, 12)));
    // Each hole rejects in turn, including the last one authored.
    CHECK(region.contains(box(5, 5, 7, 7)) == false);
    CHECK(region.contains(box(13, 13, 15, 15)) == false);
    CHECK(region.contains(box(5, 13, 7, 15)) == false);
}

TEST_CASE("concave outer boundaries, holes, and footprints are all exact") {
    std::vector<Polygon> holes;
    // A concave hole inside the C's left bar.
    holes.push_back(poly({
        raw_pt(2, 10),
        raw_pt(6, 10),
        raw_pt(6, 12),
        raw_pt(4, 12),
        raw_pt(4, 14),
        raw_pt(6, 14),
        raw_pt(6, 16),
        raw_pt(2, 16),
    }));
    const Region region = region_of(concave_outer(), std::move(holes));

    // Inside the lower bar of the C.
    CHECK(region.contains(box(10, 2, 14, 6)));
    // Inside the C's mouth: outside the region entirely.
    CHECK(region.contains(box(10, 10, 14, 14)) == false);
    // Bridging the mouth through the two container vertices at x == 24: every
    // vertex is on the boundary and no pair properly crosses, yet the footprint
    // covers excluded space.
    CHECK(region.contains(box(8, 8, 24, 16)) == false);
    // The concave hole's own notch is positive area, so a footprint filling it
    // exactly is legal: it shares three complete hole edges and enters no hole
    // interior. So is one resting along the hole's outer side.
    CHECK(region.contains(box(4, 12, 6, 14)));
    CHECK(region.contains(box(6, 10, 8, 16)));
    // And one that dips into the concave hole is not.
    CHECK(region.contains(box(3, 11, 5, 13)) == false);
}
