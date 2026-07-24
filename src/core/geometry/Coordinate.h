#pragma once

#include "core/Result.h"

#include <cstdint>

namespace tiles {

enum class QuantizationError {
    non_finite,
    out_of_range,
};

enum class ArithmeticError {
    overflow,
};

// A single fixed-point lattice coordinate: a signed 64-bit integer read with 48
// fractional bits (value == raw / 2^48). Every bit pattern denotes exactly one
// lattice value. There are deliberately no implicit conversions to or from
// integer or floating-point types.
class Coordinate final {
public:
    using Storage = std::int64_t;

    static constexpr unsigned FRACTIONAL_BITS = 48;
    static constexpr Storage SCALE = Storage { 1 } << FRACTIONAL_BITS;

    static constexpr Coordinate from_raw(Storage p_raw) {
        return Coordinate { p_raw };
    }

    constexpr Storage raw() const {
        return raw_;
    }

    friend constexpr bool operator==(Coordinate p_lhs, Coordinate p_rhs) {
        return p_lhs.raw_ == p_rhs.raw_;
    }

    friend constexpr bool operator!=(Coordinate p_lhs, Coordinate p_rhs) {
        return !(p_lhs == p_rhs);
    }

    friend constexpr bool operator<(Coordinate p_lhs, Coordinate p_rhs) {
        return p_lhs.raw_ < p_rhs.raw_;
    }

    friend constexpr bool operator>(Coordinate p_lhs, Coordinate p_rhs) {
        return p_rhs < p_lhs;
    }

    friend constexpr bool operator<=(Coordinate p_lhs, Coordinate p_rhs) {
        return !(p_rhs < p_lhs);
    }

    friend constexpr bool operator>=(Coordinate p_lhs, Coordinate p_rhs) {
        return !(p_lhs < p_rhs);
    }

private:
    Storage raw_;

    explicit constexpr Coordinate(Storage p_raw) :
        raw_(p_raw) {}
};

// Quantize a finite double onto the lattice: interpret its exact binary value,
// multiply by 2^48, and round to the nearest integer with exact ties away from
// zero. Computed from the sign/exponent/significand so the result never depends
// on the active floating-point rounding mode. Rejects non-finite inputs and
// results outside signed 64-bit storage.
Result<Coordinate, QuantizationError> quantize_double(double p_value);

// Checked addition and subtraction. Overflow returns ArithmeticError::overflow;
// it never wraps, saturates, throws, or invokes signed-overflow UB.
Result<Coordinate, ArithmeticError> checked_add(Coordinate p_lhs, Coordinate p_rhs);
Result<Coordinate, ArithmeticError> checked_subtract(Coordinate p_lhs, Coordinate p_rhs);

// Checked negation. Negating INT64_MIN has no representable result and returns
// ArithmeticError::overflow; it never wraps, saturates, throws, or invokes UB.
Result<Coordinate, ArithmeticError> checked_negate(Coordinate p_value);

} // namespace tiles
