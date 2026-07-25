#include "core/geometry/Containment.h"

#include "core/geometry/ExactInteger.h"
#include "core/geometry/Predicates.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tiles {

namespace {

// A point of the exact rational plane written as one common-denominator pair.
//
// Splitting a boundary edge at the vertices lying on it produces atoms whose
// midpoints need not sit on the q16.48 lattice: two adjacent raw integers have a
// half-raw midpoint. Rounding such a midpoint back into a Point would move it
// across the very boundary the atom exists to classify, so it is carried exactly
// instead. Ordinary lattice points have denominator one; an atom midpoint has
// numerators a.raw + b.raw and denominator two. Every numerator is formed in
// __int128, never in signed 64-bit.
struct ScaledPoint final {
    __int128 x;
    __int128 y;
    std::int64_t denominator;
};

ScaledPoint exact_point(const Point &p_point) {
    return ScaledPoint {
        static_cast<__int128>(p_point.x.raw()),
        static_cast<__int128>(p_point.y.raw()),
        1,
    };
}

// The exact midpoint of two lattice points, never rounded onto the lattice.
ScaledPoint exact_midpoint(const Point &p_a, const Point &p_b) {
    return ScaledPoint {
        static_cast<__int128>(p_a.x.raw()) + static_cast<__int128>(p_b.x.raw()),
        static_cast<__int128>(p_a.y.raw()) + static_cast<__int128>(p_b.y.raw()),
        2,
    };
}

// One lattice coordinate lifted into a scaled point's denominator. A raw value
// needs 64 bits and the denominator is at most two, so the product is exact in
// __int128.
__int128 lifted(std::int64_t p_raw, std::int64_t p_denominator) {
    return static_cast<__int128>(p_raw) * static_cast<__int128>(p_denominator);
}

// Sign of the cross product (b - a) x (p - a) with p carried as a scaled point:
//
//     (b - a) x (p_numerator - denominator * a)
//
// The denominator is positive, so scaling the second operand by it cannot change
// the sign. Differences reach 65 bits and the lifted difference 66, so the
// products are accumulated in Int256 exactly as the lattice predicates do.
int orientation_sign(const Point &p_a, const Point &p_b, const ScaledPoint &p_point) {
    const __int128 ex = static_cast<__int128>(p_b.x.raw()) - static_cast<__int128>(p_a.x.raw());
    const __int128 ey = static_cast<__int128>(p_b.y.raw()) - static_cast<__int128>(p_a.y.raw());
    const __int128 px = p_point.x - lifted(p_a.x.raw(), p_point.denominator);
    const __int128 py = p_point.y - lifted(p_a.y.raw(), p_point.denominator);
    return (Int256::multiply(ex, py) - Int256::multiply(ey, px)).sign();
}

// Whether a scaled point lies on the closed segment [a, b]: collinear, and
// within the segment's closed extent on both axes after applying the common
// denominator.
bool on_segment_scaled(const ScaledPoint &p_point, const Point &p_a, const Point &p_b) {
    if (orientation_sign(p_a, p_b, p_point) != 0) {
        return false;
    }

    const __int128 ax = lifted(p_a.x.raw(), p_point.denominator);
    const __int128 bx = lifted(p_b.x.raw(), p_point.denominator);
    const __int128 ay = lifted(p_a.y.raw(), p_point.denominator);
    const __int128 by = lifted(p_b.y.raw(), p_point.denominator);

    return p_point.x >= std::min(ax, bx) && p_point.x <= std::max(ax, bx)
        && p_point.y >= std::min(ay, by) && p_point.y <= std::max(ay, by);
}

// The one exact point-location implementation. Public q16.48 location routes
// through it with denominator one, so lattice points and atom midpoints are
// classified by identical arithmetic.
PointLocation locate_scaled(const Polygon &p_polygon, const ScaledPoint &p_point) {
    const Polygon::Vertices &vertices = p_polygon.vertices();
    const std::size_t n = vertices.size();

    // Boundary membership is proven before anything counts crossings, so the
    // winding loop below never has to reason about a point lying on an edge.
    for (std::size_t i = 0; i < n; ++i) {
        const Point &a = vertices[i];
        const Point &b = vertices[(i + 1 == n) ? 0 : i + 1];
        if (on_segment_scaled(p_point, a, b)) {
            return PointLocation::boundary;
        }
    }

    // Exact winding number about a point known to be off the boundary. Each edge
    // is counted only when it crosses the point's horizontal line, with the
    // half-open convention that an edge owns its lower endpoint and not its
    // upper one; a vertex the line passes through is therefore counted exactly
    // once across the two edges meeting there, and a horizontal edge is never
    // counted. The side of each crossing edge is decided by the exact
    // orientation sign, not by constructing an intersection abscissa.
    int winding = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Point &a = vertices[i];
        const Point &b = vertices[(i + 1 == n) ? 0 : i + 1];
        const __int128 ay = lifted(a.y.raw(), p_point.denominator);
        const __int128 by = lifted(b.y.raw(), p_point.denominator);

        if (ay <= p_point.y) {
            if (by > p_point.y && orientation_sign(a, b, p_point) > 0) {
                ++winding;
            }
        } else {
            if (by <= p_point.y && orientation_sign(a, b, p_point) < 0) {
                --winding;
            }
        }
    }

    return winding != 0 ? PointLocation::inside : PointLocation::outside;
}

// Split one closed edge [a, b] at every vertex of p_split_source lying on it,
// then classify the exact midpoint of every resulting open atom against
// p_target. The atoms are open, so the shared split points — which are exactly
// the points where membership can change — are deliberately not classified here.
bool edge_atoms_avoid(
    const Point &p_a,
    const Point &p_b,
    const Polygon &p_split_source,
    const Polygon &p_target,
    PointLocation p_rejected) {
    std::vector<Point> splits;
    splits.push_back(p_a);
    splits.push_back(p_b);
    for (const Point &vertex : p_split_source.vertices()) {
        if (on_segment(vertex, p_a, p_b)) {
            splits.push_back(vertex);
        }
    }

    // A polygon edge is nondegenerate, so at least one raw axis is nonconstant
    // along it; ordering by that axis is exactly segment order (or its reverse,
    // which induces the same consecutive pairs). Every split point is a lattice
    // point, so the comparison is an exact integer comparison.
    const bool order_by_x = p_a.x != p_b.x;
    std::sort(splits.begin(), splits.end(), [order_by_x](const Point &p_lhs, const Point &p_rhs) {
        return order_by_x ? p_lhs.x.raw() < p_rhs.x.raw() : p_lhs.y.raw() < p_rhs.y.raw();
    });

    for (std::size_t i = 0; i + 1 < splits.size(); ++i) {
        const Point &from = splits[i];
        const Point &to = splits[i + 1];
        // Collinear duplicates collapse: on one nondegenerate segment, equality
        // along the ordering axis is equality of the whole point.
        if (from == to) {
            continue;
        }
        if (locate_scaled(p_target, exact_midpoint(from, to)) == p_rejected) {
            return false;
        }
    }
    return true;
}

// Every boundary atom of p_edge_source, split at the vertices of p_split_source,
// classified against p_target.
bool boundary_atoms_avoid(
    const Polygon &p_edge_source,
    const Polygon &p_split_source,
    const Polygon &p_target,
    PointLocation p_rejected) {
    const Polygon::Vertices &vertices = p_edge_source.vertices();
    const std::size_t n = vertices.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Point &a = vertices[i];
        const Point &b = vertices[(i + 1 == n) ? 0 : i + 1];
        if (!edge_atoms_avoid(a, b, p_split_source, p_target, p_rejected)) {
            return false;
        }
    }
    return true;
}

} // namespace

PointLocation locate_point(const Polygon &p_polygon, Point p_point) {
    return locate_scaled(p_polygon, exact_point(p_point));
}

bool contains_closed(const Polygon &p_container, const Polygon &p_candidate) {
    // 1. An outside candidate vertex settles the question immediately.
    for (const Point &vertex : p_candidate.vertices()) {
        if (locate_point(p_container, vertex) == PointLocation::outside) {
            return false;
        }
    }

    // 2. A proper crossing means the candidate boundary passes strictly through
    // the container boundary, so part of it lies strictly outside. Rejecting
    // here is also what lets every remaining contact be an input endpoint on the
    // other segment: no intersection coordinate is ever constructed.
    const Polygon::Vertices &candidate_vertices = p_candidate.vertices();
    const Polygon::Vertices &container_vertices = p_container.vertices();
    for (std::size_t i = 0; i < candidate_vertices.size(); ++i) {
        const Point &a = candidate_vertices[i];
        const Point &b = candidate_vertices[(i + 1) % candidate_vertices.size()];
        for (std::size_t j = 0; j < container_vertices.size(); ++j) {
            const Point &c = container_vertices[j];
            const Point &d = container_vertices[(j + 1) % container_vertices.size()];
            if (classify_segments(a, b, c, d) == SegmentRelation::proper_crossing) {
                return false;
            }
        }
    }

    // 3. No open interval of the candidate boundary escapes the container. This
    // catches the concavity bridge whose endpoints are container vertices, which
    // no vertex test and no proper-crossing test can see.
    if (!boundary_atoms_avoid(
            p_candidate, p_container, p_container, PointLocation::outside)) {
        return false;
    }

    // 4. Symmetrically, no open interval of the container boundary enters the
    // candidate interior. An excluded reflex part of the container can reach
    // into the candidate through tangential contacts alone, and this is the only
    // test that rejects it. It is load-bearing even though ordinary convex
    // fixtures pass without it.
    if (!boundary_atoms_avoid(
            p_container, p_candidate, p_candidate, PointLocation::inside)) {
        return false;
    }

    return true;
}

bool boundaries_intersect(const Polygon &p_lhs, const Polygon &p_rhs) {
    const Polygon::Vertices &lhs = p_lhs.vertices();
    const Polygon::Vertices &rhs = p_rhs.vertices();
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const Point &a = lhs[i];
        const Point &b = lhs[(i + 1) % lhs.size()];
        for (std::size_t j = 0; j < rhs.size(); ++j) {
            const Point &c = rhs[j];
            const Point &d = rhs[(j + 1) % rhs.size()];
            if (classify_segments(a, b, c, d) != SegmentRelation::disjoint) {
                return true;
            }
        }
    }
    return false;
}

} // namespace tiles
