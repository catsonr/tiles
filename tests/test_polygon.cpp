#include "TestHarness.h"

#include "core/geometry/Polygon.h"

#include <cstdint>
#include <vector>

using namespace tiles;
using tiles_test::raw_pt;

namespace {

bool same_vertices(const Polygon::Vertices &p_actual, const std::vector<Point> &p_expected) {
    if (p_actual.size() != p_expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_actual.size(); ++i) {
        if (p_actual[i] != p_expected[i]) {
            return false;
        }
    }
    return true;
}

PolygonError error_of(std::vector<Point> p_vertices) {
    auto result = Polygon::make(std::move(p_vertices));
    return result.error();
}

} // namespace

TEST_CASE("valid convex and concave polygons are accepted") {
    auto triangle = Polygon::make({ raw_pt(0, 0), raw_pt(4, 0), raw_pt(2, 3) });
    CHECK(triangle.has_value());

    auto square = Polygon::make({ raw_pt(0, 0), raw_pt(2, 0), raw_pt(2, 2), raw_pt(0, 2) });
    CHECK(square.has_value());

    // Concave arrow / chevron.
    auto concave = Polygon::make(
        { raw_pt(0, 0), raw_pt(4, 0), raw_pt(4, 4), raw_pt(2, 1), raw_pt(0, 4) });
    CHECK(concave.has_value());
}

TEST_CASE("winding and cyclic shift canonicalize to one stored sequence") {
    const std::vector<Point> canonical = { raw_pt(0, 0), raw_pt(4, 0), raw_pt(2, 3) };

    // Every counterclockwise cyclic shift.
    const std::vector<std::vector<Point>> ccw_shifts = {
        { raw_pt(0, 0), raw_pt(4, 0), raw_pt(2, 3) },
        { raw_pt(4, 0), raw_pt(2, 3), raw_pt(0, 0) },
        { raw_pt(2, 3), raw_pt(0, 0), raw_pt(4, 0) },
    };
    // Every clockwise cyclic shift (reversed winding).
    const std::vector<std::vector<Point>> cw_shifts = {
        { raw_pt(2, 3), raw_pt(4, 0), raw_pt(0, 0) },
        { raw_pt(4, 0), raw_pt(0, 0), raw_pt(2, 3) },
        { raw_pt(0, 0), raw_pt(2, 3), raw_pt(4, 0) },
    };

    for (const auto &input : ccw_shifts) {
        auto poly = Polygon::make(input);
        CHECK(poly.has_value());
        CHECK(same_vertices(poly.value().vertices(), canonical));
    }
    for (const auto &input : cw_shifts) {
        auto poly = Polygon::make(input);
        CHECK(poly.has_value());
        CHECK(same_vertices(poly.value().vertices(), canonical));
    }
}

TEST_CASE("polygon errors are reported in the specified order") {
    CHECK(error_of({ raw_pt(0, 0), raw_pt(1, 0) }) == PolygonError::too_few_vertices);

    CHECK(error_of({ raw_pt(0, 0), raw_pt(2, 0), raw_pt(1, 2), raw_pt(0, 0) })
        == PolygonError::repeated_closing_vertex);

    CHECK(error_of({ raw_pt(0, 0), raw_pt(0, 0), raw_pt(2, 0), raw_pt(1, 2) })
        == PolygonError::zero_length_edge);

    // Non-adjacent repeat: index 0 and index 2.
    CHECK(error_of({ raw_pt(0, 0), raw_pt(2, 0), raw_pt(0, 0), raw_pt(1, 2) })
        == PolygonError::repeated_vertex);

    // Midpoint (1,0) lies strictly inside the straight bottom edge.
    CHECK(error_of({ raw_pt(0, 0), raw_pt(1, 0), raw_pt(2, 0), raw_pt(2, 2), raw_pt(0, 2) })
        == PolygonError::redundant_collinear_vertex);

    // Spike whose incident edges fold back along the same line.
    CHECK(error_of({ raw_pt(0, 0), raw_pt(2, 0), raw_pt(1, 0), raw_pt(1, 2) })
        == PolygonError::adjacent_edge_overlap);

    // Bow tie: nonadjacent edges cross. (Its signed area is also zero, but the
    // self-intersection is diagnosed first.)
    CHECK(error_of({ raw_pt(0, 0), raw_pt(2, 2), raw_pt(2, 0), raw_pt(0, 2) })
        == PolygonError::self_intersection);
}

TEST_CASE("nonadjacent vertex-on-edge contact is self-intersection") {
    // The vertex (2,0) of a later edge lands on the interior of edge (0,0)-(4,0).
    CHECK(error_of({ raw_pt(0, 0), raw_pt(4, 0), raw_pt(4, 3), raw_pt(2, 0), raw_pt(0, 3) })
        == PolygonError::self_intersection);
}

TEST_CASE("fully collinear input never yields a polygon") {
    // Zero area is a defensive backstop unreachable through make(): any all-
    // collinear loop is rejected earlier by the consecutive-triple checks.
    auto result = Polygon::make({ raw_pt(0, 0), raw_pt(1, 0), raw_pt(2, 0), raw_pt(3, 0) });
    CHECK(result.has_value() == false);
    CHECK((result.error() == PolygonError::redundant_collinear_vertex
        || result.error() == PolygonError::adjacent_edge_overlap));
}
