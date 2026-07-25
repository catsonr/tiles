#include "TestHarness.h"

#include "core/geometry/Containment.h"
#include "core/geometry/Coordinate.h"
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

// A point on whole game-unit coordinates, for fixtures that want to prove
// behavior at the scale placements actually use.
Point unit(std::int64_t p_gx, std::int64_t p_gy) {
    return raw_pt(p_gx * Coordinate::SCALE, p_gy * Coordinate::SCALE);
}

// A 10 x 10 convex square on raw lattice units.
Polygon square10() {
    return poly({ raw_pt(0, 0), raw_pt(10, 0), raw_pt(10, 10), raw_pt(0, 10) });
}

// A strongly concave C opening toward +x. Its mouth is the open rectangle
// x in (2, 6), y in (2, 4) together with the open segment x == 6, y in (2, 4):
// excluded space reachable only through container vertices.
//
//     6 +-----+
//       |     |
//     4 |  +--+
//       |  |
//     2 |  +--+
//       |     |
//     0 +-----+
//       0  2  6
Polygon concave_c() {
    return poly({
        raw_pt(0, 0),
        raw_pt(6, 0),
        raw_pt(6, 2),
        raw_pt(2, 2),
        raw_pt(2, 4),
        raw_pt(6, 4),
        raw_pt(6, 6),
        raw_pt(0, 6),
    });
}

Polygon translated(const Polygon &p_polygon, std::int64_t p_dx, std::int64_t p_dy) {
    std::vector<Point> moved;
    for (const Point &vertex : p_polygon.vertices()) {
        moved.push_back(raw_pt(vertex.x.raw() + p_dx, vertex.y.raw() + p_dy));
    }
    return poly(std::move(moved));
}

bool inside(const Polygon &p_polygon, Point p_point) {
    return locate_point(p_polygon, p_point) == PointLocation::inside;
}

bool boundary(const Polygon &p_polygon, Point p_point) {
    return locate_point(p_polygon, p_point) == PointLocation::boundary;
}

bool outside(const Polygon &p_polygon, Point p_point) {
    return locate_point(p_polygon, p_point) == PointLocation::outside;
}

} // namespace

// ---------------------------------------------------------------------------
// point location
// ---------------------------------------------------------------------------

TEST_CASE("point location separates inside, boundary, and outside for a convex polygon") {
    const Polygon square = square10();

    CHECK(inside(square, raw_pt(5, 5)));
    CHECK(inside(square, raw_pt(1, 9)));

    CHECK(outside(square, raw_pt(-1, 5)));
    CHECK(outside(square, raw_pt(11, 5)));
    CHECK(outside(square, raw_pt(5, -1)));
    CHECK(outside(square, raw_pt(5, 11)));
    CHECK(outside(square, raw_pt(-1, -1)));

    // Every vertex, and one strictly interior point of every edge — including
    // both horizontal and both vertical edges.
    CHECK(boundary(square, raw_pt(0, 0)));
    CHECK(boundary(square, raw_pt(10, 0)));
    CHECK(boundary(square, raw_pt(10, 10)));
    CHECK(boundary(square, raw_pt(0, 10)));
    CHECK(boundary(square, raw_pt(5, 0)));
    CHECK(boundary(square, raw_pt(10, 5)));
    CHECK(boundary(square, raw_pt(5, 10)));
    CHECK(boundary(square, raw_pt(0, 5)));
}

TEST_CASE("point location classifies a strongly concave polygon exactly") {
    const Polygon c = concave_c();

    // Left bar, lower bar, upper bar.
    CHECK(inside(c, raw_pt(1, 3)));
    CHECK(inside(c, raw_pt(4, 1)));
    CHECK(inside(c, raw_pt(4, 5)));

    // The mouth is excluded space even though it is surrounded on three sides.
    CHECK(outside(c, raw_pt(3, 3)));
    CHECK(outside(c, raw_pt(4, 3)));
    CHECK(outside(c, raw_pt(5, 3)));
    // And so is everything beyond the outer extent.
    CHECK(outside(c, raw_pt(7, 3)));
    CHECK(outside(c, raw_pt(-1, 3)));

    // Both reflex vertices and the edges bounding the mouth.
    CHECK(boundary(c, raw_pt(2, 2)));
    CHECK(boundary(c, raw_pt(2, 4)));
    CHECK(boundary(c, raw_pt(2, 3)));
    CHECK(boundary(c, raw_pt(4, 2)));
    CHECK(boundary(c, raw_pt(4, 4)));
    CHECK(boundary(c, raw_pt(6, 1)));
    CHECK(boundary(c, raw_pt(6, 5)));
}

TEST_CASE("point location is exact on both sides of a reflex vertex") {
    const Polygon c = concave_c();

    // The four quadrants around the reflex vertex (2, 2): inside below, inside
    // left, outside above-right, and the vertex itself on the boundary.
    CHECK(inside(c, raw_pt(1, 1)));
    CHECK(inside(c, raw_pt(3, 1)));
    CHECK(inside(c, raw_pt(1, 3)));
    CHECK(outside(c, raw_pt(3, 3)));
    CHECK(boundary(c, raw_pt(2, 2)));

    // And the same around the reflex vertex (2, 4).
    CHECK(inside(c, raw_pt(1, 5)));
    CHECK(inside(c, raw_pt(3, 5)));
    CHECK(outside(c, raw_pt(3, 3)));
    CHECK(boundary(c, raw_pt(2, 4)));
}

TEST_CASE("point location survives a ray passing exactly through polygon vertices") {
    const Polygon c = concave_c();

    // The horizontal lines y == 2 and y == 4 pass through four polygon vertices
    // between them, so a naive crossing count that double-counts a vertex, or
    // counts a horizontal edge, misclassifies these.
    CHECK(inside(c, raw_pt(1, 2)));
    CHECK(inside(c, raw_pt(1, 4)));
    CHECK(outside(c, raw_pt(7, 2)));
    CHECK(outside(c, raw_pt(7, 4)));
    CHECK(outside(c, raw_pt(-1, 2)));

    // An apex touched by the ray without crossing: both edges at (2, 4) descend,
    // so the line y == 4 through the apex must contribute nothing.
    const Polygon triangle = poly({ raw_pt(0, 0), raw_pt(4, 0), raw_pt(2, 4) });
    CHECK(outside(triangle, raw_pt(-1, 4)));
    CHECK(outside(triangle, raw_pt(5, 4)));
    CHECK(boundary(triangle, raw_pt(2, 4)));
    // A line crossing two ascending edges is an ordinary double crossing.
    CHECK(outside(triangle, raw_pt(-1, 2)));
    CHECK(inside(triangle, raw_pt(2, 1)));
}

TEST_CASE("point location handles diagonal edges without constructing intersections") {
    const Polygon triangle = poly({ raw_pt(0, 0), raw_pt(4, 0), raw_pt(0, 4) });
    CHECK(boundary(triangle, raw_pt(2, 2)));
    CHECK(boundary(triangle, raw_pt(1, 3)));
    CHECK(inside(triangle, raw_pt(1, 1)));
    CHECK(outside(triangle, raw_pt(3, 3)));
    CHECK(outside(triangle, raw_pt(2, 3)));
}

TEST_CASE("point location is exact on fractional q16.48 coordinates") {
    // A 2 x 2 game-unit square, probed at coordinates that are not whole game
    // units and not even multiples of a half unit.
    const Polygon square = poly({ unit(0, 0), unit(2, 0), unit(2, 2), unit(0, 2) });

    const Point interior = raw_pt(Coordinate::SCALE / 2 + 1, Coordinate::SCALE / 3 - 7);
    CHECK(inside(square, interior));

    const Point on_edge = raw_pt(Coordinate::SCALE / 2 + 1, 0);
    CHECK(boundary(square, on_edge));

    const Point just_beyond = raw_pt(2 * Coordinate::SCALE + 1, Coordinate::SCALE);
    CHECK(outside(square, just_beyond));

    // One raw unit inside the right edge is still strictly inside: there is no
    // tolerance anywhere in the predicate.
    CHECK(inside(square, raw_pt(2 * Coordinate::SCALE - 1, Coordinate::SCALE)));
}

TEST_CASE("point location is translation invariant") {
    const std::int64_t dx = 7 * Coordinate::SCALE + 13;
    const std::int64_t dy = -3 * Coordinate::SCALE - 5;
    const Polygon c = translated(concave_c(), dx, dy);

    CHECK(inside(c, raw_pt(1 + dx, 3 + dy)));
    CHECK(outside(c, raw_pt(4 + dx, 3 + dy)));
    CHECK(boundary(c, raw_pt(2 + dx, 2 + dy)));
    CHECK(boundary(c, raw_pt(6 + dx, 1 + dy)));
    CHECK(outside(c, raw_pt(-1 + dx, 3 + dy)));
}

TEST_CASE("point location stays exact at raw coordinate extrema") {
    // Vertices near the representable limit: every difference needs 64 bits and
    // every product needs well over 128, so a narrower intermediate would wrap.
    constexpr std::int64_t K = std::int64_t { 1 } << 62;
    const Polygon huge = poly({ raw_pt(-K, -K), raw_pt(K, -K), raw_pt(K, K), raw_pt(-K, K) });

    CHECK(inside(huge, raw_pt(0, 0)));
    CHECK(inside(huge, raw_pt(K - 1, K - 1)));
    CHECK(boundary(huge, raw_pt(K, 0)));
    CHECK(boundary(huge, raw_pt(K, K)));
    CHECK(boundary(huge, raw_pt(-K, 5)));
    CHECK(outside(huge, raw_pt(K, K + 1)));

    // The same at extreme magnitudes with a diagonal edge, where the orientation
    // determinant is largest.
    const Polygon wedge = poly({ raw_pt(-K, -K), raw_pt(K, -K), raw_pt(-K, K) });
    CHECK(boundary(wedge, raw_pt(0, 0)));
    CHECK(inside(wedge, raw_pt(-1, 0)));
    CHECK(outside(wedge, raw_pt(1, 0)));
}

// ---------------------------------------------------------------------------
// closed containment
// ---------------------------------------------------------------------------

TEST_CASE("strict containment and equality are both contained") {
    const Polygon container = square10();
    const Polygon strict = poly({ raw_pt(2, 2), raw_pt(8, 2), raw_pt(8, 8), raw_pt(2, 8) });

    CHECK(contains_closed(container, strict));
    CHECK(contains_closed(container, square10()));
    // Equality is contained in both directions; strict containment is not.
    CHECK(contains_closed(strict, square10()) == false);
}

TEST_CASE("complete-edge, partial-edge, and point boundary contact are contained") {
    const Polygon container = square10();

    // A candidate sharing a complete container edge.
    const Polygon complete_edge =
        poly({ raw_pt(0, 0), raw_pt(10, 0), raw_pt(10, 3), raw_pt(0, 3) });
    CHECK(contains_closed(container, complete_edge));

    // A candidate resting on part of one edge, sharing no vertex with it.
    const Polygon partial_edge =
        poly({ raw_pt(3, 0), raw_pt(7, 0), raw_pt(7, 4), raw_pt(3, 4) });
    CHECK(contains_closed(container, partial_edge));

    // A candidate meeting the boundary only at the corner (0, 0).
    const Polygon corner_touch = poly({ raw_pt(0, 0), raw_pt(4, 2), raw_pt(2, 4) });
    CHECK(contains_closed(container, corner_touch));

    // And one touching a reflex vertex of a concave container at a single point.
    const Polygon c = concave_c();
    const Polygon reflex_touch =
        poly({ raw_pt(1, 1), raw_pt(2, 2), raw_pt(1, 3), raw_pt(0, 2) });
    CHECK(contains_closed(c, reflex_touch));
}

TEST_CASE("a single outside vertex and a proper crossing both reject containment") {
    const Polygon container = square10();

    const Polygon one_outside =
        poly({ raw_pt(6, 6), raw_pt(12, 6), raw_pt(12, 12), raw_pt(6, 12) });
    CHECK(contains_closed(container, one_outside) == false);

    // A bar passing straight through two opposite edges: proper crossings only.
    const Polygon crossing =
        poly({ raw_pt(-2, 4), raw_pt(12, 4), raw_pt(12, 6), raw_pt(-2, 6) });
    CHECK(contains_closed(container, crossing) == false);
}

TEST_CASE("a candidate strictly containing the proposed container is rejected") {
    const Polygon container =
        poly({ raw_pt(2, 2), raw_pt(8, 2), raw_pt(8, 8), raw_pt(2, 8) });
    CHECK(contains_closed(container, square10()) == false);
    // Concave in the same relationship.
    CHECK(contains_closed(concave_c(), square10()) == false);
}

TEST_CASE("a concavity bridge with no outside vertex and no proper crossing is rejected") {
    const Polygon c = concave_c();

    // The mouth rectangle. Every one of its vertices lies on the container
    // boundary, three of its edges lie along container edges, and its fourth
    // edge meets the container only at the two container vertices (6, 2) and
    // (6, 4) — an endpoint touch, never a proper crossing. An implementation
    // testing "all vertices inside and no proper crossing" accepts this; the
    // open atom (6, 2)-(6, 4) is outside, so the boundary-atom test rejects it.
    const Polygon mouth = poly({ raw_pt(2, 2), raw_pt(6, 2), raw_pt(6, 4), raw_pt(2, 4) });

    for (const Point &vertex : mouth.vertices()) {
        CHECK(locate_point(c, vertex) != PointLocation::outside);
    }
    CHECK(outside(c, raw_pt(6, 3)));
    CHECK(contains_closed(c, mouth) == false);
}

TEST_CASE("a reflex bridge crossing excluded space through vertices alone is rejected") {
    const Polygon c = concave_c();

    // A triangle whose vertices are all on the container boundary and whose
    // hypotenuse runs straight across the mouth from container vertex (6, 4) to
    // reflex vertex (2, 2). No segment pair properly crosses: every contact is
    // an endpoint touch or a collinear overlap.
    const Polygon bridge = poly({ raw_pt(2, 2), raw_pt(6, 2), raw_pt(6, 4) });

    for (const Point &vertex : bridge.vertices()) {
        CHECK(locate_point(c, vertex) == PointLocation::boundary);
    }
    CHECK(outside(c, raw_pt(4, 3)));
    CHECK(contains_closed(c, bridge) == false);
}

TEST_CASE("both directional boundary-atom tests agree on the mouth of a concave container") {
    // The container-atom direction exists so that container boundary entering
    // the candidate interior is rejected, which for hole-free simple polygons
    // coincides with the candidate-atom rejection above. Asserting both
    // directions on the same fixture pins that agreement.
    const Polygon c = concave_c();
    const Polygon mouth = poly({ raw_pt(2, 2), raw_pt(6, 2), raw_pt(6, 4), raw_pt(2, 4) });

    CHECK(contains_closed(c, mouth) == false);
    CHECK(contains_closed(mouth, c) == false);
    // The container boundary genuinely passes through the mouth's interior
    // neighbourhood: the reflex edge midpoints sit on the mouth's boundary, and
    // the mouth's excluded-side midpoint sits outside the container.
    CHECK(boundary(mouth, raw_pt(4, 2)));
    CHECK(boundary(mouth, raw_pt(4, 4)));
    CHECK(outside(c, raw_pt(6, 3)));
}

TEST_CASE("a concave candidate inside a convex container is contained") {
    const Polygon container = square10();
    CHECK(contains_closed(container, concave_c()));
    // And a convex candidate inside a concave container.
    const Polygon inside_left_bar =
        poly({ raw_pt(0, 1), raw_pt(2, 1), raw_pt(2, 5), raw_pt(0, 5) });
    CHECK(contains_closed(concave_c(), inside_left_bar));
    // A concave candidate inside a concave container: the C inside itself,
    // shrunk into the lower bar.
    const Polygon lower_bar = poly({ raw_pt(0, 0), raw_pt(6, 0), raw_pt(6, 2), raw_pt(0, 2) });
    CHECK(contains_closed(concave_c(), lower_bar));
}

TEST_CASE("containment does not depend on submitted vertex order") {
    // Polygon::make canonicalizes winding and starting vertex, so these three
    // spellings are the same polygon and must answer identically.
    const Polygon rotated =
        poly({ raw_pt(8, 8), raw_pt(2, 8), raw_pt(2, 2), raw_pt(8, 2) });
    const Polygon forward =
        poly({ raw_pt(2, 2), raw_pt(8, 2), raw_pt(8, 8), raw_pt(2, 8) });
    const Polygon reversed =
        poly({ raw_pt(2, 8), raw_pt(8, 8), raw_pt(8, 2), raw_pt(2, 2) });

    CHECK(rotated.vertices() == forward.vertices());
    CHECK(reversed.vertices() == forward.vertices());

    const Polygon container = square10();
    CHECK(contains_closed(container, rotated));
    CHECK(contains_closed(container, forward));
    CHECK(contains_closed(container, reversed));
}

TEST_CASE("containment is exact on fractional and translated fixtures") {
    const Polygon container = poly({ unit(0, 0), unit(4, 0), unit(4, 4), unit(0, 4) });

    // A candidate whose corners are not on any whole or half game unit.
    const Polygon fractional = poly({
        raw_pt(Coordinate::SCALE / 3, Coordinate::SCALE / 7),
        raw_pt(3 * Coordinate::SCALE + 11, Coordinate::SCALE / 7),
        raw_pt(3 * Coordinate::SCALE + 11, 2 * Coordinate::SCALE - 5),
        raw_pt(Coordinate::SCALE / 3, 2 * Coordinate::SCALE - 5),
    });
    CHECK(contains_closed(container, fractional));

    // One raw unit past the right edge is outside, with no tolerance.
    const Polygon escaping = poly({
        raw_pt(Coordinate::SCALE, Coordinate::SCALE),
        raw_pt(4 * Coordinate::SCALE + 1, Coordinate::SCALE),
        raw_pt(4 * Coordinate::SCALE + 1, 2 * Coordinate::SCALE),
        raw_pt(Coordinate::SCALE, 2 * Coordinate::SCALE),
    });
    CHECK(contains_closed(container, escaping) == false);

    // The concavity-bridge rejection survives translation unchanged.
    const std::int64_t dx = -9 * Coordinate::SCALE - 17;
    const std::int64_t dy = 5 * Coordinate::SCALE + 23;
    const Polygon c = translated(concave_c(), dx, dy);
    const Polygon mouth = translated(
        poly({ raw_pt(2, 2), raw_pt(6, 2), raw_pt(6, 4), raw_pt(2, 4) }), dx, dy);
    const Polygon left_bar = translated(
        poly({ raw_pt(0, 1), raw_pt(2, 1), raw_pt(2, 5), raw_pt(0, 5) }), dx, dy);
    CHECK(contains_closed(c, mouth) == false);
    CHECK(contains_closed(c, left_bar));
}

TEST_CASE("containment stays exact near the coordinate limits") {
    constexpr std::int64_t K = std::int64_t { 1 } << 62;
    const Polygon container =
        poly({ raw_pt(-K, -K), raw_pt(K, -K), raw_pt(K, K), raw_pt(-K, K) });

    const Polygon inner = poly({
        raw_pt(-K + 1, -K + 1),
        raw_pt(K - 1, -K + 1),
        raw_pt(K - 1, K - 1),
        raw_pt(-K + 1, K - 1),
    });
    CHECK(contains_closed(container, inner));

    // Sharing a complete edge at the limit is still contained...
    const Polygon flush = poly({ raw_pt(-K, -K), raw_pt(K, -K), raw_pt(K, 0), raw_pt(-K, 0) });
    CHECK(contains_closed(container, flush));

    // ...and one raw unit past it is not.
    const Polygon over = poly({
        raw_pt(-K, -K),
        raw_pt(K + 1, -K),
        raw_pt(K + 1, 0),
        raw_pt(-K, 0),
    });
    CHECK(contains_closed(container, over) == false);
}

// ---------------------------------------------------------------------------
// boundary intersection
// ---------------------------------------------------------------------------

TEST_CASE("boundaries_intersect is true for every kind of contact and false otherwise") {
    const Polygon container = square10();

    // Separated entirely.
    const Polygon far_away =
        poly({ raw_pt(20, 20), raw_pt(24, 20), raw_pt(24, 24), raw_pt(20, 24) });
    CHECK(boundaries_intersect(container, far_away) == false);

    // Strictly inside: contained, but the boundaries never meet.
    const Polygon strict = poly({ raw_pt(2, 2), raw_pt(8, 2), raw_pt(8, 8), raw_pt(2, 8) });
    CHECK(boundaries_intersect(container, strict) == false);
    CHECK(contains_closed(container, strict));

    // Identity.
    CHECK(boundaries_intersect(container, square10()));

    // A single shared point.
    const Polygon corner = poly({ raw_pt(10, 10), raw_pt(14, 10), raw_pt(14, 14) });
    CHECK(boundaries_intersect(container, corner));

    // A shared complete edge, and a shared partial edge.
    const Polygon abutting =
        poly({ raw_pt(10, 0), raw_pt(14, 0), raw_pt(14, 10), raw_pt(10, 10) });
    CHECK(boundaries_intersect(container, abutting));
    const Polygon partial =
        poly({ raw_pt(10, 2), raw_pt(14, 2), raw_pt(14, 6), raw_pt(10, 6) });
    CHECK(boundaries_intersect(container, partial));

    // A proper crossing.
    const Polygon crossing =
        poly({ raw_pt(-2, 4), raw_pt(12, 4), raw_pt(12, 6), raw_pt(-2, 6) });
    CHECK(boundaries_intersect(container, crossing));

    // Symmetric in its operands.
    CHECK(boundaries_intersect(far_away, container) == false);
    CHECK(boundaries_intersect(crossing, container));
}
