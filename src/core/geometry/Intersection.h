#pragma once

#include "core/geometry/Polygon.h"
#include "core/geometry/Triangle.h"

namespace tiles {

// Whether the open interiors of two triangles intersect with positive area.
// Both triangles must be nondegenerate and counterclockwise (the Polygon
// contract). Exact: the separating-axis test projects with wide-integer dot
// products and uses non-strict interval separation, so boundary-only contact
// (a shared vertex, a shared edge, an edge-on-vertex touch) is not overlap.
bool triangle_interiors_overlap(const Triangle &p_lhs, const Triangle &p_rhs);

// Whether the open interiors of two polygons intersect with positive area.
// Total and symmetric for all public polygons. Every legal boundary contact
// (no shared points, an isolated point, a partial edge, a complete edge) is
// classified as non-overlap; positive-area intersection, strict containment,
// and identity are overlap.
bool interiors_overlap(const Polygon &p_lhs, const Polygon &p_rhs);

} // namespace tiles
