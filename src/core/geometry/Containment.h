#pragma once

#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"

namespace tiles {

// Where a point lies with respect to one closed filled polygon. Boundary is a
// distinct answer, never folded into inside or outside: closed polygonal sets
// meet along their boundaries, and every containment question in this project
// depends on that contact being legal.
enum class PointLocation {
    outside,
    boundary,
    inside,
};

// Exact point location against a closed filled polygon. Boundary membership is
// tested first, so a point on a vertex, on a complete edge, or strictly inside
// an edge is boundary. The remaining classification is an exact winding-number
// count. Every comparison is integer arithmetic over q16.48 raw values widened
// into __int128 differences and Int256 products: nothing here divides,
// constructs an intersection coordinate, casts to a floating type, or selects a
// tolerance. Convexity, a particular winding supplied by the caller, and
// axis-aligned boundaries are all irrelevant beyond the canonical Polygon
// contract.
PointLocation locate_point(const Polygon &p_polygon, Point p_point);

// Whether every closed point of p_candidate belongs to the closed filled polygon
// p_container. Equality and every kind of boundary-only contact are contained.
//
// This is not "all vertices inside and no proper crossing": a candidate edge can
// leave and re-enter a concave container exactly through container vertices, and
// an excluded concavity of the container can enter the candidate interior
// through a tangential contact, both using only endpoint-touch relations. The
// implementation classifies the open boundary intervals induced by every such
// contact, in both directions.
bool contains_closed(const Polygon &p_container, const Polygon &p_candidate);

// Whether any boundary segment pair of the two polygons is non-disjoint: a
// proper crossing, an endpoint touch, or a positive-length collinear overlap.
// Strict interior containment with disjoint boundaries is not an intersection of
// boundaries and is therefore false.
bool boundaries_intersect(const Polygon &p_lhs, const Polygon &p_rhs);

} // namespace tiles
