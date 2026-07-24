#pragma once

#include "core/geometry/ExactInteger.h"
#include "core/geometry/Point.h"

#include <vector>

namespace tiles {

enum class Turn {
    clockwise,
    collinear,
    counterclockwise,
};

// Complete classification of how two closed segments relate.
enum class SegmentRelation {
    disjoint,
    proper_crossing,   // cross at a single point interior to both segments
    endpoint_touch,    // meet at exactly one point that is an endpoint of one
    collinear_overlap, // collinear and sharing a subsegment of positive length
};

// Orientation of the ordered triple (a, b, c), computed exactly.
Turn orientation(const Point &p_a, const Point &p_b, const Point &p_c);

// Whether p lies on the closed segment [a, b]. Exact.
bool on_segment(const Point &p_p, const Point &p_a, const Point &p_b);

// Exact classification of segment [a, b] against segment [c, d]. Does not
// construct any intersection coordinate.
SegmentRelation classify_segments(
    const Point &p_a, const Point &p_b, const Point &p_c, const Point &p_d);

// Exact signed doubled area of the polygon given by its vertices in order
// (shoelace sum). Positive for counterclockwise winding, negative for
// clockwise, zero for degenerate.
Int256 signed_double_area(const std::vector<Point> &p_vertices);

} // namespace tiles
