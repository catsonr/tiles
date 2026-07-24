#include "core/geometry/Rotation.h"

#include "core/geometry/Coordinate.h"

namespace tiles {

namespace {

Result<Point, RotationError> overflow() {
    return Result<Point, RotationError>::failure(RotationError::coordinate_overflow);
}

} // namespace

Result<Point, RotationError> rotate_point(Point p_point, QuarterTurn p_turn) {
    switch (p_turn) {
        case QuarterTurn::zero: {
            return Result<Point, RotationError>::success(p_point);
        }
        case QuarterTurn::quarter: {
            // (-y, x)
            auto nx = checked_negate(p_point.y);
            if (!nx.has_value()) {
                return overflow();
            }
            return Result<Point, RotationError>::success(Point { nx.value(), p_point.x });
        }
        case QuarterTurn::half: {
            // (-x, -y)
            auto nx = checked_negate(p_point.x);
            auto ny = checked_negate(p_point.y);
            if (!nx.has_value() || !ny.has_value()) {
                return overflow();
            }
            return Result<Point, RotationError>::success(Point { nx.value(), ny.value() });
        }
        case QuarterTurn::three_quarter: {
            // (y, -x)
            auto ny = checked_negate(p_point.x);
            if (!ny.has_value()) {
                return overflow();
            }
            return Result<Point, RotationError>::success(Point { p_point.y, ny.value() });
        }
    }
    // Unreachable: every QuarterTurn is handled above.
    return overflow();
}

} // namespace tiles
