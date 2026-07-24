#include "TestHarness.h"

#include "core/geometry/Polygon.h"
#include "core/geometry/Predicates.h"

#include <cstdint>
#include <vector>

using namespace tiles;
using tiles_test::raw_pt;

namespace {

bool is_polygon_vertex(const Point &p_p, const Polygon::Vertices &p_vertices) {
    for (const Point &v : p_vertices) {
        if (v == p_p) {
            return true;
        }
    }
    return false;
}

Int256 triangle_double_area(const Triangle &p_triangle) {
    const std::vector<Point> vs = {
        p_triangle.vertices[0], p_triangle.vertices[1], p_triangle.vertices[2]
    };
    return signed_double_area(vs);
}

bool triangulations_equal(const Polygon::Triangulation &p_a, const Polygon::Triangulation &p_b) {
    if (p_a.size() != p_b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_a.size(); ++i) {
        for (std::size_t k = 0; k < 3; ++k) {
            if (p_a[i].vertices[k] != p_b[i].vertices[k]) {
                return false;
            }
        }
    }
    return true;
}

void assert_certificate(const Polygon &p_poly) {
    const auto &vertices = p_poly.vertices();
    const auto &triangles = p_poly.triangulation();

    // Exactly n - 2 triangles.
    CHECK(triangles.size() == vertices.size() - 2);

    Int256 area_sum;
    for (const Triangle &tri : triangles) {
        // Vertices are drawn only from the polygon.
        CHECK(is_polygon_vertex(tri.vertices[0], vertices));
        CHECK(is_polygon_vertex(tri.vertices[1], vertices));
        CHECK(is_polygon_vertex(tri.vertices[2], vertices));
        // Strictly counterclockwise and nondegenerate.
        CHECK(orientation(tri.vertices[0], tri.vertices[1], tri.vertices[2])
            == Turn::counterclockwise);
        area_sum = area_sum + triangle_double_area(tri);
    }

    // The triangle areas sum exactly to the polygon's doubled area.
    CHECK((area_sum - signed_double_area(vertices)).is_zero());
}

} // namespace

TEST_CASE("triangulation certificate for a convex polygon") {
    auto poly = Polygon::make(
        { raw_pt(0, 0), raw_pt(4, 0), raw_pt(4, 4), raw_pt(0, 4) });
    CHECK(poly.has_value());
    assert_certificate(poly.value());
}

TEST_CASE("triangulation certificate for a strongly concave polygon") {
    // A comb whose set of valid ears changes as vertices are removed.
    auto poly = Polygon::make({
        raw_pt(0, 0),
        raw_pt(6, 0),
        raw_pt(6, 4),
        raw_pt(5, 1),
        raw_pt(3, 4),
        raw_pt(1, 1),
        raw_pt(0, 4),
    });
    CHECK(poly.has_value());
    assert_certificate(poly.value());
}

TEST_CASE("triangulation is deterministic across winding and cyclic shifts") {
    const std::vector<Point> base = {
        raw_pt(0, 0), raw_pt(6, 0), raw_pt(6, 4),
        raw_pt(5, 1), raw_pt(3, 4), raw_pt(1, 1), raw_pt(0, 4),
    };

    auto reference = Polygon::make(base);
    CHECK(reference.has_value());

    // A cyclic shift.
    std::vector<Point> shifted(base.begin() + 3, base.end());
    shifted.insert(shifted.end(), base.begin(), base.begin() + 3);
    auto shifted_poly = Polygon::make(shifted);
    CHECK(shifted_poly.has_value());

    // Reversed winding.
    std::vector<Point> reversed(base.rbegin(), base.rend());
    auto reversed_poly = Polygon::make(reversed);
    CHECK(reversed_poly.has_value());

    CHECK(triangulations_equal(
        reference.value().triangulation(), shifted_poly.value().triangulation()));
    CHECK(triangulations_equal(
        reference.value().triangulation(), reversed_poly.value().triangulation()));
}
