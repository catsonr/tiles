#include "core/geometry/Intersection.h"

#include "core/geometry/ExactInteger.h"

#include <algorithm>
#include <array>

namespace tiles {

namespace {

// Coordinate differences may need 65 bits, so they are formed directly in
// signed 128-bit; a 64-bit subtraction could overflow.
__int128 difference(Coordinate p_lhs, Coordinate p_rhs) {
    return static_cast<__int128>(p_lhs.raw()) - static_cast<__int128>(p_rhs.raw());
}

// Exact projection of a point onto axis (p_nx, p_ny): nx*x + ny*y. The axis
// components are 65-bit edge differences and the coordinates are 64-bit, so a
// single product can exceed signed 128 bits and their sum can exceed it again;
// the whole dot product is accumulated in Int256.
Int256 project(const Point &p_point, __int128 p_nx, __int128 p_ny) {
    return Int256::multiply(p_nx, static_cast<__int128>(p_point.x.raw()))
        + Int256::multiply(p_ny, static_cast<__int128>(p_point.y.raw()));
}

struct Interval {
    Int256 lo;
    Int256 hi;
};

// Project a triangle's three vertices onto the axis and return the closed
// interval they span.
Interval project_triangle(const Triangle &p_triangle, __int128 p_nx, __int128 p_ny) {
    Int256 lo = project(p_triangle.vertices[0], p_nx, p_ny);
    Int256 hi = lo;
    for (std::size_t i = 1; i < 3; ++i) {
        const Int256 value = project(p_triangle.vertices[i], p_nx, p_ny);
        if (Int256::compare(value, lo) < 0) {
            lo = value;
        }
        if (Int256::compare(value, hi) > 0) {
            hi = value;
        }
    }
    return Interval { lo, hi };
}

// True when the two closed projection intervals are weakly separated. Equality
// at a shared endpoint counts as separation: the closed triangles may touch
// there while their open interiors stay disjoint.
bool intervals_separated(const Interval &p_a, const Interval &p_b) {
    return Int256::compare(p_a.hi, p_b.lo) <= 0
        || Int256::compare(p_b.hi, p_a.lo) <= 0;
}

// Does the axis normal to edge (p_from -> p_to) weakly separate the triangles?
bool axis_separates(
    const Triangle &p_a, const Triangle &p_b, const Point &p_from, const Point &p_to) {
    // Edge vector e = to - from; the axis is its left normal (-e.y, e.x). Its
    // direction is irrelevant to interval separation, so either normal works.
    const __int128 ex = difference(p_to.x, p_from.x);
    const __int128 ey = difference(p_to.y, p_from.y);
    const __int128 nx = -ey;
    const __int128 ny = ex;
    return intervals_separated(
        project_triangle(p_a, nx, ny), project_triangle(p_b, nx, ny));
}

} // namespace

bool triangle_interiors_overlap(const Triangle &p_lhs, const Triangle &p_rhs) {
    // Separating-axis theorem for two convex polygons: the interiors are
    // disjoint iff some edge normal of either triangle weakly separates them.
    for (std::size_t i = 0; i < 3; ++i) {
        const Point &from = p_lhs.vertices[i];
        const Point &to = p_lhs.vertices[(i + 1) % 3];
        if (axis_separates(p_lhs, p_rhs, from, to)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const Point &from = p_rhs.vertices[i];
        const Point &to = p_rhs.vertices[(i + 1) % 3];
        if (axis_separates(p_lhs, p_rhs, from, to)) {
            return false;
        }
    }
    return true;
}

namespace {

struct BoundingBox {
    std::int64_t min_x;
    std::int64_t max_x;
    std::int64_t min_y;
    std::int64_t max_y;
};

BoundingBox bounds(const Polygon &p_polygon) {
    const Polygon::Vertices &vertices = p_polygon.vertices();
    BoundingBox box {
        vertices.front().x.raw(),
        vertices.front().x.raw(),
        vertices.front().y.raw(),
        vertices.front().y.raw(),
    };
    for (const Point &vertex : vertices) {
        box.min_x = std::min(box.min_x, vertex.x.raw());
        box.max_x = std::max(box.max_x, vertex.x.raw());
        box.min_y = std::min(box.min_y, vertex.y.raw());
        box.max_y = std::max(box.max_y, vertex.y.raw());
    }
    return box;
}

// Weakly separated boxes cannot contain a positive-area intersection: equality
// on an axis leaves at most a shared edge or point. Raw coordinates fit int64,
// so the comparison is exact without wide integers.
bool boxes_separated(const BoundingBox &p_a, const BoundingBox &p_b) {
    return p_a.max_x <= p_b.min_x || p_b.max_x <= p_a.min_x
        || p_a.max_y <= p_b.min_y || p_b.max_y <= p_a.min_y;
}

} // namespace

bool interiors_overlap(const Polygon &p_lhs, const Polygon &p_rhs) {
    if (boxes_separated(bounds(p_lhs), bounds(p_rhs))) {
        return false;
    }

    // Each certified triangulation covers its polygon with interior-disjoint
    // triangles, so the open interiors intersect iff some triangle pair does.
    for (const Triangle &left : p_lhs.triangulation()) {
        for (const Triangle &right : p_rhs.triangulation()) {
            if (triangle_interiors_overlap(left, right)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace tiles
