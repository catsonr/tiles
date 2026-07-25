#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/ArrangementRegion.h"
#include "core/Hex12.h"
#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/Region.h"
#include "core/geometry/Alignment.h"
#include "core/geometry/ExactInteger.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "core/geometry/Predicates.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using namespace tiles;
using tiles_test::raw_pt;
using tiles_test::reference_orientation;

namespace {

// --- exact fixtures ---
//
// Every prototile, oriented value, placement, and arrangement below comes from
// an ordinary checked factory. Nothing here fabricates a footprint, weakens a
// constructor, or reaches past a public invariant to stage a failure.

constexpr std::uint64_t SQUARE_ID = 1;
constexpr std::uint64_t TALL_ID = 2;
constexpr std::uint64_t SMALL_ID = 3;
constexpr std::uint64_t ELL_ID = 4;
constexpr std::uint64_t BIG_ID = 5;
constexpr std::uint64_t TRIANGLE_ID = 35;
constexpr std::uint64_t HEXAGON_ID = 36;

// The lattice tests work in raw units with a four-unit cell: predicates and
// polygons depend only on relative geometry, so small exact values are both
// convenient and complete.
constexpr std::int64_t CELL = 4;

Point raw(std::int64_t p_x, std::int64_t p_y) {
    return raw_pt(p_x, p_y);
}

Prototile rectangle(std::uint64_t p_id, std::int64_t p_width, std::int64_t p_height) {
    auto polygon = Polygon::make({
        raw(0, 0),
        raw(p_width, 0),
        raw(p_width, p_height),
        raw(0, p_height),
    });
    return std::move(Prototile::make(PrototileId(p_id), std::move(polygon).value()))
        .value();
}

// A concave hole-free tile: the L tromino over the four-unit cell.
Prototile ell() {
    auto polygon = Polygon::make({
        raw(0, 0),
        raw(2 * CELL, 0),
        raw(2 * CELL, CELL),
        raw(CELL, CELL),
        raw(CELL, 2 * CELL),
        raw(0, 2 * CELL),
    });
    return std::move(Prototile::make(PrototileId(ELL_ID), std::move(polygon).value()))
        .value();
}

Placement placement_of(const Prototile &p_prototile, Point p_translation) {
    auto placement = Placement::make(reference_orientation(p_prototile), p_translation);
    return std::move(placement).value();
}

// One four-unit cell of the lattice, named by integer grid position.
Placement cell(std::int64_t p_column, std::int64_t p_row) {
    return placement_of(
        rectangle(SQUARE_ID, CELL, CELL), raw(p_column * CELL, p_row * CELL));
}

using Cell = std::pair<std::int64_t, std::int64_t>;

Arrangement cells(const std::vector<Cell> &p_cells) {
    Arrangement arrangement;
    for (const Cell &position : p_cells) {
        arrangement.try_insert(cell(position.first, position.second));
    }
    return arrangement;
}

// Every cell of a rectangular block except those named as omitted.
std::vector<Cell> block_except(
    std::int64_t p_columns, std::int64_t p_rows, const std::vector<Cell> &p_omitted) {
    std::vector<Cell> result;
    for (std::int64_t row = 0; row < p_rows; ++row) {
        for (std::int64_t column = 0; column < p_columns; ++column) {
            bool omitted = false;
            for (const Cell &skip : p_omitted) {
                if (skip.first == column && skip.second == row) {
                    omitted = true;
                }
            }
            if (!omitted) {
                result.push_back(Cell { column, row });
            }
        }
    }
    return result;
}

// The complete canonical boundary, compared vertex by vertex. A polygon's
// canonical sequence starts at its lexicographically smallest vertex and winds
// counterclockwise, so an expected sequence is written the same way.
bool boundary_is(const Polygon &p_polygon, const std::vector<Point> &p_expected) {
    return p_polygon.vertices() == p_expected;
}

Int256 doubled(std::int64_t p_value) {
    return Int256::from_i64(p_value);
}

Int256 summed_footprint_area(const Arrangement &p_arrangement) {
    Int256 total;
    for (const Entry &entry : p_arrangement.entries()) {
        total = total + signed_double_area(entry.placement.footprint().vertices());
    }
    return total;
}

// The hexagon-and-triangle patch grown through ordinary exact joins: a hexagon
// at exact origin, then the first triangle variant and candidate edge each of
// its six edges admits. No hex-12 coordinate is authored here.
Arrangement hex12_star() {
    Arrangement arrangement;

    auto hexagons = compile_hex12_orientations(
        PrototileId(HEXAGON_ID), Hex12RegularPolygon::hexagon,
        { Orientation::reference() });
    if (!hexagons) {
        return arrangement;
    }
    auto anchor = Placement::make(std::move(hexagons).value().front(), raw(0, 0));
    if (!anchor) {
        return arrangement;
    }
    auto inserted = arrangement.try_insert(std::move(anchor).value());
    if (!inserted) {
        return arrangement;
    }
    const PlacementId hexagon_id = inserted.value();

    std::vector<Orientation> twelfths;
    for (std::uint32_t step = 0; step < 12; ++step) {
        twelfths.push_back(Orientation::make(step, 12).value());
    }
    auto triangles = compile_hex12_orientations(
        PrototileId(TRIANGLE_ID), Hex12RegularPolygon::triangle, std::move(twelfths));
    if (!triangles) {
        return arrangement;
    }

    const std::size_t hexagon_edges =
        arrangement.entries().front().placement.footprint().vertices().size();
    for (std::size_t anchor_edge = 0; anchor_edge < hexagon_edges; ++anchor_edge) {
        bool joined = false;
        for (const OrientedPrototile &candidate : triangles.value()) {
            const std::size_t candidate_edges =
                candidate.canonical_polygon().vertices().size();
            for (std::size_t edge = 0; edge < candidate_edges && !joined; ++edge) {
                joined = bool(arrangement.try_join_full_edges(
                    hexagon_id, EdgeIndex(anchor_edge), candidate, EdgeIndex(edge)));
            }
            if (joined) {
                break;
            }
        }
    }

    return arrangement;
}

} // namespace

// --- the empty arrangement ---

TEST_CASE("an empty arrangement has no region") {
    const Arrangement arrangement;
    auto region = region_from_arrangement(arrangement);
    CHECK(region.has_value() == false);
    if (region.has_value()) {
        return;
    }
    CHECK(region.error().code == ArrangementRegionErrorCode::empty_arrangement);
    CHECK(region.error().nonmanifold_vertex.has_value() == false);
    CHECK(region.error().component_points.empty());
    CHECK(region.error().invariant_failure.has_value() == false);
}

// --- one tile ---

TEST_CASE("one convex tile becomes exactly its own footprint") {
    const Arrangement arrangement = cells({ { 0, 0 } });
    auto region = region_from_arrangement(arrangement);
    CHECK(bool(region));
    if (!region) {
        return;
    }
    CHECK(boundary_is(region.value().outer_boundary(),
        { raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) }));
    CHECK(region.value().inner_boundaries().empty());
    CHECK(region.value().doubled_area() == doubled(32));
    CHECK(same_boundary(region.value().outer_boundary(),
        arrangement.entries().front().placement.footprint()));
}

TEST_CASE("one concave tile keeps its reflex vertex") {
    Arrangement arrangement;
    arrangement.try_insert(placement_of(ell(), raw(0, 0)));
    auto region = region_from_arrangement(arrangement);
    CHECK(bool(region));
    if (!region) {
        return;
    }
    CHECK(boundary_is(region.value().outer_boundary(),
        { raw(0, 0), raw(8, 0), raw(8, 4), raw(4, 4), raw(4, 8), raw(0, 8) }));
    CHECK(region.value().inner_boundaries().empty());
    CHECK(region.value().doubled_area() == doubled(96));
}

// --- shared boundary cancels ---

TEST_CASE("two complete-edge neighbours become one rectangle") {
    const Arrangement arrangement = cells({ { 0, 0 }, { 1, 0 } });
    auto region = region_from_arrangement(arrangement);
    CHECK(bool(region));
    if (!region) {
        return;
    }
    CHECK(boundary_is(region.value().outer_boundary(),
        { raw(0, 0), raw(8, 0), raw(8, 4), raw(0, 4) }));
    CHECK(region.value().inner_boundaries().empty());
    CHECK(region.value().doubled_area() == doubled(64));
}

TEST_CASE("several short edges cancel against one long edge") {
    // A 4 x 8 tile on the left; its whole right edge is met by the left edges of
    // two stacked cells, so nothing cancels as a complete source edge.
    Arrangement arrangement;
    arrangement.try_insert(placement_of(rectangle(TALL_ID, CELL, 2 * CELL), raw(0, 0)));
    arrangement.try_insert(cell(1, 0));
    arrangement.try_insert(cell(1, 1));

    auto region = region_from_arrangement(arrangement);
    CHECK(bool(region));
    if (!region) {
        return;
    }
    CHECK(boundary_is(region.value().outer_boundary(),
        { raw(0, 0), raw(8, 0), raw(8, 8), raw(0, 8) }));
    CHECK(region.value().inner_boundaries().empty());
    CHECK(region.value().doubled_area() == doubled(128));
    CHECK(region.value().doubled_area() == summed_footprint_area(arrangement));
}

TEST_CASE("a T-junction survives as one boundary vertex") {
    // Two cells side by side, with a 2 x 2 tile on top of the left half of the
    // left cell. (2, 4) stays a boundary vertex; (4, 4) is a straight run and is
    // simplified away.
    Arrangement arrangement;
    arrangement.try_insert(cell(0, 0));
    arrangement.try_insert(cell(1, 0));
    arrangement.try_insert(placement_of(rectangle(SMALL_ID, 2, 2), raw(0, 4)));

    auto region = region_from_arrangement(arrangement);
    CHECK(bool(region));
    if (!region) {
        return;
    }
    CHECK(boundary_is(region.value().outer_boundary(),
        { raw(0, 0), raw(8, 0), raw(8, 4), raw(2, 4), raw(2, 6), raw(0, 6) }));
    CHECK(region.value().inner_boundaries().empty());
    CHECK(region.value().doubled_area() == doubled(72));
    CHECK(region.value().doubled_area() == summed_footprint_area(arrangement));
}

// --- patches ---

TEST_CASE("a lattice patch keeps its concave outer boundary") {
    const Arrangement arrangement = cells({ { 0, 0 }, { 1, 0 }, { 0, 1 } });
    auto region = region_from_arrangement(arrangement);
    CHECK(bool(region));
    if (!region) {
        return;
    }
    CHECK(boundary_is(region.value().outer_boundary(),
        { raw(0, 0), raw(8, 0), raw(8, 4), raw(4, 4), raw(4, 8), raw(0, 8) }));
    CHECK(region.value().inner_boundaries().empty());
    CHECK(region.value().doubled_area() == doubled(96));
}

TEST_CASE("a ring of cells derives one hole") {
    const Arrangement arrangement = cells(block_except(3, 3, { { 1, 1 } }));
    auto region = region_from_arrangement(arrangement);
    CHECK(bool(region));
    if (!region) {
        return;
    }
    CHECK(boundary_is(region.value().outer_boundary(),
        { raw(0, 0), raw(12, 0), raw(12, 12), raw(0, 12) }));
    CHECK(region.value().inner_boundaries().size() == 1);
    if (region.value().inner_boundaries().size() != 1) {
        return;
    }
    CHECK(boundary_is(region.value().inner_boundaries().front(),
        { raw(4, 4), raw(8, 4), raw(8, 8), raw(4, 8) }));
    CHECK(region.value().doubled_area() == doubled(8 * 32));
    CHECK(region.value().doubled_area() == summed_footprint_area(arrangement));
}

TEST_CASE("two omitted interior cells derive two ordered holes") {
    const Arrangement arrangement = cells(block_except(5, 3, { { 1, 1 }, { 3, 1 } }));
    auto region = region_from_arrangement(arrangement);
    CHECK(bool(region));
    if (!region) {
        return;
    }
    CHECK(boundary_is(region.value().outer_boundary(),
        { raw(0, 0), raw(20, 0), raw(20, 12), raw(0, 12) }));
    CHECK(region.value().inner_boundaries().size() == 2);
    if (region.value().inner_boundaries().size() != 2) {
        return;
    }
    CHECK(boundary_is(region.value().inner_boundaries()[0],
        { raw(4, 4), raw(8, 4), raw(8, 8), raw(4, 8) }));
    CHECK(boundary_is(region.value().inner_boundaries()[1],
        { raw(12, 4), raw(16, 4), raw(16, 8), raw(12, 8) }));
    CHECK(region.value().doubled_area() == doubled(13 * 32));
    CHECK(region.value().doubled_area() == summed_footprint_area(arrangement));
}

TEST_CASE("a mixed-prototile hex-12 patch derives one twelve-sided region") {
    const Arrangement arrangement = hex12_star();
    CHECK(arrangement.entries().size() == 7);
    if (arrangement.entries().size() != 7) {
        return;
    }
    // Genuinely mixed: one hexagon identity and six triangle identities.
    std::size_t triangles = 0;
    for (const Entry &entry : arrangement.entries()) {
        if (entry.placement.prototile().id() == PrototileId(TRIANGLE_ID)) {
            ++triangles;
        }
    }
    CHECK(triangles == 6);

    auto region = region_from_arrangement(arrangement);
    CHECK(bool(region));
    if (!region) {
        return;
    }
    CHECK(region.value().outer_boundary().vertices().size() == 12);
    CHECK(region.value().inner_boundaries().empty());
    CHECK(region.value().doubled_area() == summed_footprint_area(arrangement));
    // The derived boundary is a strict superset of the hexagon: the star's area
    // is the hexagon plus six triangles, never the hexagon alone.
    CHECK(region.value().doubled_area()
        != signed_double_area(
            arrangement.entries().front().placement.footprint().vertices()));
}

// --- storage order carries no meaning ---

TEST_CASE("arrangement storage order does not change the derived region") {
    const std::vector<Cell> forward = block_except(5, 3, { { 1, 1 }, { 3, 1 } });
    std::vector<Cell> reversed(forward.rbegin(), forward.rend());

    auto first = region_from_arrangement(cells(forward));
    auto second = region_from_arrangement(cells(reversed));
    CHECK(bool(first));
    CHECK(bool(second));
    if (!first || !second) {
        return;
    }
    CHECK(same_boundary(first.value().outer_boundary(), second.value().outer_boundary()));
    CHECK(first.value().inner_boundaries().size()
        == second.value().inner_boundaries().size());
    if (first.value().inner_boundaries().size()
        != second.value().inner_boundaries().size()) {
        return;
    }
    for (std::size_t i = 0; i < first.value().inner_boundaries().size(); ++i) {
        CHECK(same_boundary(
            first.value().inner_boundaries()[i], second.value().inner_boundaries()[i]));
    }
    CHECK(first.value().doubled_area() == second.value().doubled_area());
}

// --- nonmanifold coverage ---

TEST_CASE("point-connected tiles are rejected as nonmanifold") {
    const Arrangement arrangement = cells({ { 0, 0 }, { 1, 1 } });
    auto region = region_from_arrangement(arrangement);
    CHECK(region.has_value() == false);
    if (region.has_value()) {
        return;
    }
    CHECK(region.error().code == ArrangementRegionErrorCode::nonmanifold_boundary_vertex);
    CHECK(region.error().nonmanifold_vertex.has_value());
    if (!region.error().nonmanifold_vertex.has_value()) {
        return;
    }
    CHECK(region.error().nonmanifold_vertex.value().point == raw(4, 4));
    CHECK(region.error().nonmanifold_vertex.value().indegree == 2);
    CHECK(region.error().nonmanifold_vertex.value().outdegree == 2);
    CHECK(region.error().component_points.empty());
    CHECK(region.error().invariant_failure.has_value() == false);
}

TEST_CASE("an edge-connected patch pinched at one point is rejected") {
    // A C of seven cells whose two ends meet diagonally at (4, 4). Every cell is
    // edge-connected to the next, so only the pinch makes this coverage invalid.
    const Arrangement arrangement =
        cells(block_except(3, 3, { { 0, 0 }, { 1, 1 } }));
    auto region = region_from_arrangement(arrangement);
    CHECK(region.has_value() == false);
    if (region.has_value()) {
        return;
    }
    CHECK(region.error().code == ArrangementRegionErrorCode::nonmanifold_boundary_vertex);
    if (!region.error().nonmanifold_vertex.has_value()) {
        return;
    }
    CHECK(region.error().nonmanifold_vertex.value().point == raw(4, 4));
    CHECK(region.error().nonmanifold_vertex.value().indegree == 2);
    CHECK(region.error().nonmanifold_vertex.value().outdegree == 2);
}

TEST_CASE("the lexicographically least offending vertex is reported") {
    // Two pinch points, authored so storage order disagrees with the answer.
    const Arrangement arrangement = cells({ { 2, 2 }, { 0, 0 }, { 1, 1 } });
    auto region = region_from_arrangement(arrangement);
    CHECK(region.has_value() == false);
    if (region.has_value() || !region.error().nonmanifold_vertex.has_value()) {
        return;
    }
    CHECK(region.error().code == ArrangementRegionErrorCode::nonmanifold_boundary_vertex);
    CHECK(region.error().nonmanifold_vertex.value().point == raw(4, 4));
}

// --- disconnected coverage ---

TEST_CASE("two separated tiles are rejected as disconnected coverage") {
    const Arrangement arrangement = cells({ { 0, 0 }, { 5, 0 } });
    auto region = region_from_arrangement(arrangement);
    CHECK(region.has_value() == false);
    if (region.has_value()) {
        return;
    }
    CHECK(region.error().code == ArrangementRegionErrorCode::disconnected_coverage);
    CHECK(region.error().component_points.size() == 2);
    if (region.error().component_points.size() != 2) {
        return;
    }
    CHECK(region.error().component_points[0] == raw(0, 0));
    CHECK(region.error().component_points[1] == raw(20, 0));
    CHECK(region.error().nonmanifold_vertex.has_value() == false);
    CHECK(region.error().invariant_failure.has_value() == false);
}

TEST_CASE("component points are reported in lexicographic order, not storage order") {
    const Arrangement arrangement = cells({ { 5, 0 }, { 0, 0 } });
    auto region = region_from_arrangement(arrangement);
    CHECK(region.has_value() == false);
    if (region.has_value() || region.error().component_points.size() != 2) {
        return;
    }
    CHECK(region.error().component_points[0] == raw(0, 0));
    CHECK(region.error().component_points[1] == raw(20, 0));
}

TEST_CASE("removing a bridge placement disconnects the coverage") {
    Arrangement arrangement;
    arrangement.try_insert(cell(0, 0));
    const auto bridge = arrangement.try_insert(cell(1, 0)).value();
    arrangement.try_insert(cell(2, 0));

    auto before = region_from_arrangement(arrangement);
    CHECK(bool(before));
    if (before) {
        CHECK(boundary_is(before.value().outer_boundary(),
            { raw(0, 0), raw(12, 0), raw(12, 4), raw(0, 4) }));
    }

    CHECK(bool(arrangement.try_remove(bridge)));
    auto after = region_from_arrangement(arrangement);
    CHECK(after.has_value() == false);
    if (after.has_value()) {
        return;
    }
    CHECK(after.error().code == ArrangementRegionErrorCode::disconnected_coverage);
    CHECK(after.error().component_points.size() == 2);
    if (after.error().component_points.size() != 2) {
        return;
    }
    CHECK(after.error().component_points[0] == raw(0, 0));
    CHECK(after.error().component_points[1] == raw(8, 0));
}

// --- precedence ---

TEST_CASE("a nonmanifold vertex is reported before disconnected coverage") {
    // A point-connected pair and a separate cell: both defects are present, and
    // the manifold proof runs first.
    const Arrangement arrangement = cells({ { 0, 0 }, { 1, 1 }, { 5, 0 } });
    auto region = region_from_arrangement(arrangement);
    CHECK(region.has_value() == false);
    if (region.has_value()) {
        return;
    }
    CHECK(region.error().code == ArrangementRegionErrorCode::nonmanifold_boundary_vertex);
    CHECK(region.error().component_points.empty());
    if (!region.error().nonmanifold_vertex.has_value()) {
        return;
    }
    CHECK(region.error().nonmanifold_vertex.value().point == raw(4, 4));
}

// --- exact area ---

TEST_CASE("the derived region area equals the summed footprint area") {
    const std::vector<std::vector<Cell>> fixtures {
        { { 0, 0 } },
        { { 0, 0 }, { 1, 0 } },
        block_except(3, 3, { { 1, 1 } }),
        block_except(5, 3, { { 1, 1 }, { 3, 1 } }),
        block_except(4, 4, {}),
    };
    for (const std::vector<Cell> &fixture : fixtures) {
        const Arrangement arrangement = cells(fixture);
        auto region = region_from_arrangement(arrangement);
        CHECK(bool(region));
        if (!region) {
            continue;
        }
        CHECK(region.value().doubled_area() == summed_footprint_area(arrangement));
        CHECK(region.value().doubled_area()
            == doubled(static_cast<std::int64_t>(fixture.size()) * 32));
    }
}

// --- one raw unit of disagreement ---

TEST_CASE("one raw q16.48 unit of misalignment stays boundary instead of cancelling") {
    constexpr std::int64_t SIDE = std::int64_t { 1 } << 20;
    const Prototile big = rectangle(BIG_ID, SIDE, SIDE);

    Arrangement aligned;
    aligned.try_insert(placement_of(big, raw(0, 0)));
    aligned.try_insert(placement_of(big, raw(SIDE, 0)));
    auto flush = region_from_arrangement(aligned);
    CHECK(bool(flush));
    if (flush) {
        CHECK(boundary_is(flush.value().outer_boundary(),
            { raw(0, 0), raw(2 * SIDE, 0), raw(2 * SIDE, SIDE), raw(0, SIDE) }));
        CHECK(flush.value().doubled_area() == doubled(4 * SIDE * SIDE));
    }

    // The same pair, with the right tile lifted by exactly one raw unit. All but
    // one unit of each shared edge still cancels; the two leftover units are
    // coverage boundary and appear as four extra exact vertices.
    Arrangement offset;
    offset.try_insert(placement_of(big, raw(0, 0)));
    offset.try_insert(placement_of(big, raw(SIDE, 1)));
    auto stepped = region_from_arrangement(offset);
    CHECK(bool(stepped));
    if (!stepped) {
        return;
    }
    CHECK(boundary_is(stepped.value().outer_boundary(),
        {
            raw(0, 0),
            raw(SIDE, 0),
            raw(SIDE, 1),
            raw(2 * SIDE, 1),
            raw(2 * SIDE, SIDE + 1),
            raw(SIDE, SIDE + 1),
            raw(SIDE, SIDE),
            raw(0, SIDE),
        }));
    CHECK(stepped.value().inner_boundaries().empty());
    CHECK(stepped.value().doubled_area() == doubled(4 * SIDE * SIDE));
    CHECK(stepped.value().doubled_area() == summed_footprint_area(offset));
}
