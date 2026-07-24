#pragma once

#include "core/Result.h"

#include <cstdint>

namespace tiles {

enum class OrientationError {
    zero_order,
};

// A strong immutable value representing a rational fraction of one complete
// counterclockwise turn, stored as a canonical step/order pair in lowest terms.
//
// Construction reduces step modulo order, canonicalizes every whole turn to 0/1,
// and otherwise divides both components by their greatest common divisor. Two
// orientations are therefore equal exactly when they denote the same angle:
//
//     0/1 == 4/4     1/4 == 2/8     1/2 == 4/8     3/4 == 6/8
//
// Ordering is angular order within [0, 1), decided by exact integer
// cross-products; two 32-bit components multiply safely into 64 bits, so no
// floating point is ever involved. The general step/order representation is
// intentional: later tiers admit finite orientation systems with more than four
// turns without changing this type.
class Orientation final {
public:
    using Component = std::uint32_t;

    static Result<Orientation, OrientationError> make(
        Component p_step, Component p_order);

    // Legible constants for the four quarter turns. Each is already in canonical
    // lowest terms, so it obeys the single representation above.
    static constexpr Orientation reference() {
        return Orientation(0, 1);
    }
    static constexpr Orientation quarter() {
        return Orientation(1, 4);
    }
    static constexpr Orientation half() {
        return Orientation(1, 2);
    }
    static constexpr Orientation three_quarter() {
        return Orientation(3, 4);
    }

    constexpr Component step() const {
        return step_;
    }
    constexpr Component order() const {
        return order_;
    }

    friend constexpr bool operator==(Orientation p_lhs, Orientation p_rhs) {
        return p_lhs.step_ == p_rhs.step_ && p_lhs.order_ == p_rhs.order_;
    }

    friend constexpr bool operator!=(Orientation p_lhs, Orientation p_rhs) {
        return !(p_lhs == p_rhs);
    }

    friend constexpr bool operator<(Orientation p_lhs, Orientation p_rhs) {
        // step/order comparison as an exact cross-product. Both sides fit in 64
        // bits because each component is a 32-bit value.
        return static_cast<std::uint64_t>(p_lhs.step_) * p_rhs.order_
            < static_cast<std::uint64_t>(p_rhs.step_) * p_lhs.order_;
    }

private:
    constexpr Orientation(Component p_step, Component p_order) :
        step_(p_step),
        order_(p_order) {}

    Component step_;
    Component order_;
};

} // namespace tiles
