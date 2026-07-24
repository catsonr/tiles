#pragma once

#include "core/Result.h"
#include "core/geometry/Point.h"

namespace tiles {

// One of the four exact quarter turns about the local origin. Kept as a plain
// geometry enum so the transform kernel needs no knowledge of the Orientation
// domain value; the lattice-orientation compiler maps an admitted Orientation
// onto one of these.
enum class QuarterTurn {
    zero,          // ( x,  y)
    quarter,       // (-y,  x)
    half,          // (-x, -y)
    three_quarter, // ( y, -x)
};

enum class RotationError {
    coordinate_overflow,
};

// Apply an exact quarter turn to a point about the local origin. Every negation
// is checked: negating INT64_MIN has no representable result and returns
// coordinate_overflow rather than wrapping. All four turns have positive
// orientation; none reflects.
Result<Point, RotationError> rotate_point(Point p_point, QuarterTurn p_turn);

} // namespace tiles
