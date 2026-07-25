#include "core/Region.h"

#include "core/geometry/Containment.h"
#include "core/geometry/Intersection.h"
#include "core/geometry/Predicates.h"

#include <cassert>
#include <utility>

namespace tiles {

namespace {

// A hole is strictly inside the outer polygon exactly when it is contained by
// the closed outer polygon and shares no boundary point with it. Containment
// alone would admit a hole equal to the outer polygon or resting against it, and
// a boundary-disjointness test alone would admit one lying entirely outside.
bool strictly_inside(const Polygon &p_outer, const Polygon &p_hole) {
    return contains_closed(p_outer, p_hole) && !boundaries_intersect(p_outer, p_hole);
}

// Two holes are disjoint exactly when their boundaries never meet and their
// interiors never overlap. The first condition rejects proper crossings, point
// contact, and partial- or complete-edge contact; the second rejects identity,
// positive-area overlap, and one hole nested strictly inside another.
bool holes_disjoint(const Polygon &p_lhs, const Polygon &p_rhs) {
    return !boundaries_intersect(p_lhs, p_rhs) && !interiors_overlap(p_lhs, p_rhs);
}

} // namespace

Region::Region(Polygon p_outer, std::vector<Polygon> p_holes, Int256 p_doubled_area) :
    outer_boundary_(std::move(p_outer)),
    inner_boundaries_(std::move(p_holes)),
    doubled_area_(p_doubled_area) {}

Result<Region, RegionError> Region::make(Polygon p_outer, std::vector<Polygon> p_holes) {
    using Made = Result<Region, RegionError>;

    // Fixed validation precedence. Every hole is checked against the outer
    // boundary in authored index order first, so the reported first_hole never
    // depends on which pair happens to be visited.
    for (std::size_t i = 0; i < p_holes.size(); ++i) {
        if (!strictly_inside(p_outer, p_holes[i])) {
            return Made::failure(RegionError {
                RegionErrorCode::hole_not_strictly_inside_outer, i, std::nullopt });
        }
    }

    // Only then pairs, in lexicographic (i, j) order with i < j.
    for (std::size_t i = 0; i < p_holes.size(); ++i) {
        for (std::size_t j = i + 1; j < p_holes.size(); ++j) {
            if (!holes_disjoint(p_holes[i], p_holes[j])) {
                return Made::failure(
                    RegionError { RegionErrorCode::holes_not_disjoint, i, j });
            }
        }
    }

    // Public polygons are canonical counterclockwise, so every operand of this
    // difference is positive.
    Int256 doubled_area = signed_double_area(p_outer.vertices());
    for (const Polygon &hole : p_holes) {
        doubled_area = doubled_area - signed_double_area(hole.vertices());
    }

    // Every validated hole lies strictly inside the outer polygon with pairwise
    // disjoint interiors, so their total area is strictly less than the outer
    // area and this result is positive. A nonpositive value here would be an
    // internal arithmetic defect, not something a valid public polygon can
    // reach, so it asserts rather than becoming a public error alternative.
    assert(doubled_area.sign() > 0);

    return Made::success(
        Region(std::move(p_outer), std::move(p_holes), doubled_area));
}

bool Region::contains(const Polygon &p_polygon) const {
    if (!contains_closed(outer_boundary_, p_polygon)) {
        return false;
    }

    // Hole exclusion is interior overlap, never boundary intersection: a
    // footprint may touch a hole at a point, along part of an edge, or along a
    // complete edge, and only positive-area intrusion into the removed interior
    // is illegal.
    for (const Polygon &hole : inner_boundaries_) {
        if (interiors_overlap(p_polygon, hole)) {
            return false;
        }
    }
    return true;
}

} // namespace tiles
