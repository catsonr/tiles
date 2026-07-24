#include "TestHarness.h"

#include "core/geometry/Intersection.h"
#include "core/geometry/Polygon.h"
#include "core/geometry/Predicates.h"
#include "core/geometry/Triangle.h"

#include <cstdint>
#include <utility>
#include <vector>

using namespace tiles;
using tiles_test::raw_pt;

namespace {

Point raw(std::int64_t p_x, std::int64_t p_y) {
    return raw_pt(p_x, p_y);
}

// Build a triangle directly, asserting it is counterclockwise as the overlap
// predicate requires. Ordering is normalized here so callers can list vertices
// in either winding.
Triangle tri(Point p_a, Point p_b, Point p_c) {
    if (orientation(p_a, p_b, p_c) == Turn::clockwise) {
        return Triangle { { p_a, p_c, p_b } };
    }
    return Triangle { { p_a, p_b, p_c } };
}

Polygon polygon_or_die(std::vector<Point> p_vertices) {
    auto result = Polygon::make(std::move(p_vertices));
    return std::move(result).value();
}

std::vector<Point> translate(const std::vector<Point> &p_vertices, std::int64_t p_dx, std::int64_t p_dy) {
    std::vector<Point> out;
    out.reserve(p_vertices.size());
    for (const Point &v : p_vertices) {
        out.push_back(raw(v.x.raw() + p_dx, v.y.raw() + p_dy));
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// triangle interior overlap
// ---------------------------------------------------------------------------

TEST_CASE("triangles with positive-area overlap") {
    const Triangle a = tri(raw(0, 0), raw(4, 0), raw(0, 4));
    const Triangle b = tri(raw(1, 1), raw(5, 1), raw(1, 5));
    CHECK(triangle_interiors_overlap(a, b));
    CHECK(triangle_interiors_overlap(b, a));
}

TEST_CASE("triangle strict containment in both operand orders") {
    const Triangle big = tri(raw(0, 0), raw(10, 0), raw(0, 10));
    const Triangle small = tri(raw(2, 2), raw(4, 2), raw(2, 4));
    CHECK(triangle_interiors_overlap(big, small));
    CHECK(triangle_interiors_overlap(small, big));
}

TEST_CASE("identical triangles overlap") {
    const Triangle a = tri(raw(0, 0), raw(4, 0), raw(2, 3));
    CHECK(triangle_interiors_overlap(a, a));
}

TEST_CASE("separated triangles do not overlap") {
    const Triangle a = tri(raw(0, 0), raw(1, 0), raw(0, 1));
    const Triangle b = tri(raw(10, 10), raw(11, 10), raw(10, 11));
    CHECK(triangle_interiors_overlap(a, b) == false);
    CHECK(triangle_interiors_overlap(b, a) == false);
}

TEST_CASE("triangles sharing a complete edge do not overlap") {
    const Triangle above = tri(raw(0, 0), raw(4, 0), raw(2, 3));
    const Triangle below = tri(raw(0, 0), raw(4, 0), raw(2, -3));
    CHECK(triangle_interiors_overlap(above, below) == false);
    CHECK(triangle_interiors_overlap(below, above) == false);
}

TEST_CASE("triangles sharing a partial edge do not overlap") {
    const Triangle above = tri(raw(0, 0), raw(4, 0), raw(2, 3));
    const Triangle below = tri(raw(1, 0), raw(3, 0), raw(2, -3));
    CHECK(triangle_interiors_overlap(above, below) == false);
}

TEST_CASE("triangles sharing one vertex do not overlap") {
    const Triangle a = tri(raw(0, 0), raw(4, 0), raw(2, 3));
    const Triangle b = tri(raw(0, 0), raw(-3, -1), raw(-1, -3));
    CHECK(triangle_interiors_overlap(a, b) == false);
}

TEST_CASE("triangle vertex touching another edge does not overlap") {
    const Triangle a = tri(raw(0, 0), raw(4, 0), raw(2, 3));
    // Apex sits exactly on a's base at an interior point; body lies below.
    const Triangle b = tri(raw(2, 0), raw(0, -3), raw(4, -3));
    CHECK(triangle_interiors_overlap(a, b) == false);
    CHECK(triangle_interiors_overlap(b, a) == false);
}

TEST_CASE("triangles with collinear edges and same-side interiors overlap") {
    const Triangle a = tri(raw(0, 0), raw(4, 0), raw(0, 4));
    const Triangle b = tri(raw(0, 0), raw(4, 0), raw(4, 4));
    CHECK(triangle_interiors_overlap(a, b));
    CHECK(triangle_interiors_overlap(b, a));
}

TEST_CASE("triangle overlap is invariant under a common translation") {
    const Triangle a = tri(raw(0, 0), raw(4, 0), raw(1, 3));
    const Triangle b = tri(raw(2, 1), raw(6, 1), raw(3, 4));
    const bool base = triangle_interiors_overlap(a, b);
    const std::int64_t dx = 1'000'000;
    const std::int64_t dy = -777'777;
    const Triangle a2 = tri(raw(0 + dx, 0 + dy), raw(4 + dx, 0 + dy), raw(1 + dx, 3 + dy));
    const Triangle b2 = tri(raw(2 + dx, 1 + dy), raw(6 + dx, 1 + dy), raw(3 + dx, 4 + dy));
    CHECK(base == triangle_interiors_overlap(a2, b2));
    CHECK(base);
}

TEST_CASE("triangle overlap at raw extremes exceeding 128-bit intermediates") {
    // A near-full-lattice triangle: its edge differences approach 2^64 and its
    // projected dot products approach 2^128, so the separating-axis arithmetic
    // must use more than signed 128-bit intermediates.
    const std::int64_t A = INT64_MAX;
    const Triangle up = tri(raw(-A, -A), raw(A, -A), raw(0, A));
    const Triangle down = tri(raw(0, -A), raw(A, A), raw(-A, A));
    // The two opposed huge triangles cover the origin from both sides.
    CHECK(triangle_interiors_overlap(up, down));
    CHECK(triangle_interiors_overlap(down, up));

    // Two huge triangles meeting only at the origin are boundary contact only.
    const Triangle lower = tri(raw(-A, -A), raw(A, -A), raw(0, 0));
    const Triangle upper = tri(raw(0, 0), raw(A, A), raw(-A, A));
    CHECK(triangle_interiors_overlap(lower, upper) == false);
    CHECK(triangle_interiors_overlap(upper, lower) == false);
}

// ---------------------------------------------------------------------------
// polygon interior overlap
// ---------------------------------------------------------------------------

TEST_CASE("separated convex polygons do not overlap") {
    const Polygon a = polygon_or_die({ raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) });
    const Polygon b = polygon_or_die({ raw(10, 0), raw(14, 0), raw(14, 4), raw(10, 4) });
    CHECK(interiors_overlap(a, b) == false);
    CHECK(interiors_overlap(b, a) == false);
}

TEST_CASE("separated concave polygons do not overlap") {
    const std::vector<Point> l_shape = {
        raw(0, 0), raw(6, 0), raw(6, 3), raw(3, 3), raw(3, 6), raw(0, 6)
    };
    const Polygon a = polygon_or_die(l_shape);
    const Polygon b = polygon_or_die(translate(l_shape, 100, 0));
    CHECK(interiors_overlap(a, b) == false);
}

TEST_CASE("polygons touching at an isolated point do not overlap") {
    const Polygon a = polygon_or_die({ raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) });
    const Polygon b = polygon_or_die({ raw(4, 4), raw(8, 4), raw(8, 8), raw(4, 8) });
    CHECK(interiors_overlap(a, b) == false);
    CHECK(interiors_overlap(b, a) == false);
}

TEST_CASE("polygons sharing a full edge do not overlap") {
    const Polygon a = polygon_or_die({ raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) });
    const Polygon b = polygon_or_die({ raw(4, 0), raw(8, 0), raw(8, 4), raw(4, 4) });
    CHECK(interiors_overlap(a, b) == false);
}

TEST_CASE("polygons sharing a partial edge or t-junction do not overlap") {
    const Polygon a = polygon_or_die({ raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) });
    // b's left edge lies on a's right edge but spans only part of it.
    const Polygon b = polygon_or_die({ raw(4, 1), raw(8, 1), raw(8, 3), raw(4, 3) });
    CHECK(interiors_overlap(a, b) == false);
    CHECK(interiors_overlap(b, a) == false);
}

TEST_CASE("identical polygons overlap") {
    const Polygon a = polygon_or_die({ raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) });
    CHECK(interiors_overlap(a, a));
}

TEST_CASE("strict containment overlaps") {
    const Polygon big = polygon_or_die({ raw(0, 0), raw(10, 0), raw(10, 10), raw(0, 10) });
    const Polygon small = polygon_or_die({ raw(3, 3), raw(6, 3), raw(6, 6), raw(3, 6) });
    CHECK(interiors_overlap(big, small));
    CHECK(interiors_overlap(small, big));
}

TEST_CASE("proper boundary crossing overlaps") {
    const Polygon a = polygon_or_die({ raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) });
    const Polygon b = polygon_or_die({ raw(2, 2), raw(6, 2), raw(6, 6), raw(2, 6) });
    CHECK(interiors_overlap(a, b));
    CHECK(interiors_overlap(b, a));
}

TEST_CASE("positive-area collinear overlap") {
    // Two rectangles whose top and bottom edges are collinear and which share a
    // positive-area strip.
    const Polygon a = polygon_or_die({ raw(0, 0), raw(6, 0), raw(6, 2), raw(0, 2) });
    const Polygon b = polygon_or_die({ raw(3, 0), raw(9, 0), raw(9, 2), raw(3, 2) });
    CHECK(interiors_overlap(a, b));
}

TEST_CASE("concave interlocking polygons overlap") {
    // Two C-shapes rotated 180 degrees relative to each other, interlocked so
    // their prongs cross into each other's cavities.
    const std::vector<Point> c_shape = {
        raw(0, 0), raw(6, 0), raw(6, 2), raw(2, 2), raw(2, 4), raw(6, 4), raw(6, 6), raw(0, 6)
    };
    const Polygon a = polygon_or_die(c_shape);
    // Mirror through the center (3,3): produces a C opening left, shifted to
    // interlock with a.
    std::vector<Point> mirrored;
    for (const Point &v : c_shape) {
        mirrored.push_back(raw(6 - v.x.raw(), 6 - v.y.raw()));
    }
    const Polygon b = polygon_or_die(translate(mirrored, 3, 0));
    CHECK(interiors_overlap(a, b));
    CHECK(interiors_overlap(b, a));
}

TEST_CASE("contact at several disconnected boundary features does not overlap") {
    // A notched block sitting against a matching block: they meet along two
    // separate edge segments with a gap between, but never share interior.
    const Polygon a = polygon_or_die({
        raw(0, 0), raw(4, 0), raw(4, 2), raw(2, 2), raw(2, 4), raw(4, 4), raw(4, 6), raw(0, 6)
    });
    const Polygon b = polygon_or_die({ raw(4, 0), raw(8, 0), raw(8, 6), raw(4, 6),
                                       raw(4, 4), raw(2, 4), raw(2, 2), raw(4, 2) });
    CHECK(interiors_overlap(a, b) == false);
    CHECK(interiors_overlap(b, a) == false);
}

TEST_CASE("polygon overlap is invariant under a common translation") {
    const std::vector<Point> a_v = { raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) };
    const std::vector<Point> b_v = { raw(2, 2), raw(6, 2), raw(6, 6), raw(2, 6) };
    const Polygon a = polygon_or_die(a_v);
    const Polygon b = polygon_or_die(b_v);
    const bool base = interiors_overlap(a, b);
    const Polygon a2 = polygon_or_die(translate(a_v, 9'000'000, -4'000'000));
    const Polygon b2 = polygon_or_die(translate(b_v, 9'000'000, -4'000'000));
    CHECK(base == interiors_overlap(a2, b2));
    CHECK(base);
}

TEST_CASE("overlap found only by a non-first triangle pair") {
    // Two concave polygons whose overlap lies away from the first triangle of
    // each certified triangulation, so the double loop must reach a later pair.
    // A comb with a left tower (x in [0,3]), a right tower (x in [6,9]), and a
    // base; ear clipping emits the right tower as its first triangle.
    const std::vector<Point> a_v = {
        raw(0, 0), raw(9, 0), raw(9, 9), raw(6, 9), raw(6, 3), raw(3, 3), raw(3, 9), raw(0, 9)
    };
    const Polygon a = polygon_or_die(a_v);
    // b overlaps only a's left tower, which the leading triangle never covers.
    const Polygon b = polygon_or_die({ raw(1, 4), raw(4, 4), raw(4, 8), raw(1, 8) });

    // The overlap is genuine...
    CHECK(interiors_overlap(a, b));
    // ...yet a's leading triangle overlaps no triangle of b, so the double loop
    // can only find the witness by scanning past the first triangle.
    bool leading_witnesses = false;
    for (const Triangle &t : b.triangulation()) {
        if (triangle_interiors_overlap(a.triangulation().front(), t)) {
            leading_witnesses = true;
        }
    }
    CHECK(leading_witnesses == false);
}
