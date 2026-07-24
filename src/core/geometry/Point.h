#pragma once

#include "core/Result.h"
#include "core/geometry/Coordinate.h"

namespace tiles {

struct Point final {
    Coordinate x;
    Coordinate y;

    friend constexpr bool operator==(Point p_lhs, Point p_rhs) {
        return p_lhs.x == p_rhs.x && p_lhs.y == p_rhs.y;
    }

    friend constexpr bool operator!=(Point p_lhs, Point p_rhs) {
        return !(p_lhs == p_rhs);
    }
};

// Explicit lexicographic order by (x, y). Used to select a polygon's canonical
// first vertex; deliberately not spelled operator< so it is never an implicit
// geometric comparison.
constexpr bool lexicographically_less(Point p_lhs, Point p_rhs) {
    if (p_lhs.x != p_rhs.x) {
        return p_lhs.x < p_rhs.x;
    }
    return p_lhs.y < p_rhs.y;
}

// Componentwise checked arithmetic. If either component overflows, the whole
// operation reports ArithmeticError::overflow.
Result<Point, ArithmeticError> checked_add(Point p_lhs, Point p_rhs);
Result<Point, ArithmeticError> checked_subtract(Point p_lhs, Point p_rhs);

} // namespace tiles
