#include "core/geometry/Predicates.h"

#include <algorithm>

namespace tiles {

namespace {

// Coordinate differences may need 65 bits, so they are formed directly in
// signed 128-bit; a 64-bit subtraction could overflow.
__int128 difference(Coordinate p_lhs, Coordinate p_rhs) {
    return static_cast<__int128>(p_lhs.raw()) - static_cast<__int128>(p_rhs.raw());
}

} // namespace

Turn orientation(const Point &p_a, const Point &p_b, const Point &p_c) {
    const __int128 abx = difference(p_b.x, p_a.x);
    const __int128 aby = difference(p_b.y, p_a.y);
    const __int128 acx = difference(p_c.x, p_a.x);
    const __int128 acy = difference(p_c.y, p_a.y);

    const Int256 cross = Int256::multiply(abx, acy) - Int256::multiply(aby, acx);
    const int s = cross.sign();
    if (s > 0) {
        return Turn::counterclockwise;
    }
    if (s < 0) {
        return Turn::clockwise;
    }
    return Turn::collinear;
}

bool on_segment(const Point &p_p, const Point &p_a, const Point &p_b) {
    if (orientation(p_a, p_b, p_p) != Turn::collinear) {
        return false;
    }
    const auto px = p_p.x.raw();
    const auto py = p_p.y.raw();
    const auto min_x = std::min(p_a.x.raw(), p_b.x.raw());
    const auto max_x = std::max(p_a.x.raw(), p_b.x.raw());
    const auto min_y = std::min(p_a.y.raw(), p_b.y.raw());
    const auto max_y = std::max(p_a.y.raw(), p_b.y.raw());
    return px >= min_x && px <= max_x && py >= min_y && py <= max_y;
}

SegmentRelation classify_segments(
    const Point &p_a, const Point &p_b, const Point &p_c, const Point &p_d) {
    const Turn o1 = orientation(p_a, p_b, p_c);
    const Turn o2 = orientation(p_a, p_b, p_d);
    const Turn o3 = orientation(p_c, p_d, p_a);
    const Turn o4 = orientation(p_c, p_d, p_b);

    // If c and d both lie on line ab, all four points are collinear.
    if (o1 == Turn::collinear && o2 == Turn::collinear) {
        // Project onto the axis along which the shared line is monotone.
        const bool use_x = p_a.x != p_b.x;
        const auto key = [use_x](const Point &pt) {
            return use_x ? pt.x.raw() : pt.y.raw();
        };
        auto lo1 = key(p_a);
        auto hi1 = key(p_b);
        if (lo1 > hi1) {
            std::swap(lo1, hi1);
        }
        auto lo2 = key(p_c);
        auto hi2 = key(p_d);
        if (lo2 > hi2) {
            std::swap(lo2, hi2);
        }
        const auto lo = std::max(lo1, lo2);
        const auto hi = std::min(hi1, hi2);
        if (lo < hi) {
            return SegmentRelation::collinear_overlap;
        }
        if (lo == hi) {
            return SegmentRelation::endpoint_touch;
        }
        return SegmentRelation::disjoint;
    }

    const bool strict_cross = o1 != o2 && o3 != o4
        && o1 != Turn::collinear && o2 != Turn::collinear
        && o3 != Turn::collinear && o4 != Turn::collinear;
    if (strict_cross) {
        return SegmentRelation::proper_crossing;
    }

    const bool touches =
        (o1 == Turn::collinear && on_segment(p_c, p_a, p_b))
        || (o2 == Turn::collinear && on_segment(p_d, p_a, p_b))
        || (o3 == Turn::collinear && on_segment(p_a, p_c, p_d))
        || (o4 == Turn::collinear && on_segment(p_b, p_c, p_d));
    if (touches) {
        return SegmentRelation::endpoint_touch;
    }

    return SegmentRelation::disjoint;
}

Int256 signed_double_area(const std::vector<Point> &p_vertices) {
    Int256 accumulator;
    const std::size_t n = p_vertices.size();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1 == n) ? 0 : i + 1;
        const __int128 xi = p_vertices[i].x.raw();
        const __int128 yi = p_vertices[i].y.raw();
        const __int128 xj = p_vertices[j].x.raw();
        const __int128 yj = p_vertices[j].y.raw();
        accumulator = accumulator + Int256::multiply(xi, yj) - Int256::multiply(xj, yi);
    }
    return accumulator;
}

} // namespace tiles
