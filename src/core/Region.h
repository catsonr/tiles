#pragma once

#include "core/Result.h"
#include "core/geometry/ExactInteger.h"
#include "core/geometry/Polygon.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace tiles {

enum class RegionErrorCode {
    hole_not_strictly_inside_outer,
    holes_not_disjoint,
};

// The first offending hole, or the first offending pair. second_hole is
// populated exactly when the code is holes_not_disjoint, and then first_hole is
// the lower index of the pair.
struct RegionError final {
    RegionErrorCode code;
    std::size_t first_hole;
    std::optional<std::size_t> second_hole;
};

// One connected closed polygonal area: an outer polygon contributing positive
// area, and zero or more holes each removing its own open interior. Every
// boundary curve — outer and inner alike — remains part of the region, so a
// footprint may touch or share an edge with any of them.
//
// A Region can only exist once every hole has been proven strictly inside the
// outer boundary and pairwise disjoint from the other holes, which is what keeps
// the positive area connected and its exact doubled area positive. Boundaries
// are owned by value and exposed read-only; there is no unchecked public
// constructor, no mutable view, and no second coordinate representation.
//
// Hole order is preserved exactly as authored. It carries no geometric meaning;
// it exists so rendering and error identity are deterministic.
class Region final {
public:
    static Result<Region, RegionError> make(
        Polygon p_outer, std::vector<Polygon> p_holes);

    const Polygon &outer_boundary() const {
        return outer_boundary_;
    }

    const std::vector<Polygon> &inner_boundaries() const {
        return inner_boundaries_;
    }

    // Exact outer doubled area minus every hole's exact doubled area, computed
    // once during construction. Doubled area is the complete comparison value
    // this project needs: it is never halved, rounded, or converted to a scalar.
    const Int256 &doubled_area() const {
        return doubled_area_;
    }

    // Whether the closed polygon lies within the closed region: inside the
    // closed outer boundary, and meeting no hole's open interior. Point,
    // partial-edge, and complete-edge contact with any boundary stays legal.
    bool contains(const Polygon &p_polygon) const;

private:
    Region(Polygon p_outer, std::vector<Polygon> p_holes, Int256 p_doubled_area);

    Polygon outer_boundary_;
    std::vector<Polygon> inner_boundaries_;
    Int256 doubled_area_;
};

} // namespace tiles
