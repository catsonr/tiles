#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"

#include <cstdint>
#include <cstring>

namespace tiles {

namespace {

using Storage = Coordinate::Storage;

constexpr int MANTISSA_BITS = 52;
constexpr int EXPONENT_BIAS = 1023;

} // namespace

Result<Coordinate, QuantizationError> quantize_double(double p_value) {
    std::uint64_t bits;
    std::memcpy(&bits, &p_value, sizeof(bits));

    const std::uint64_t sign = bits >> 63;
    const std::uint64_t raw_exponent = (bits >> MANTISSA_BITS) & 0x7FFu;
    const std::uint64_t fraction = bits & ((std::uint64_t { 1 } << MANTISSA_BITS) - 1);

    // Infinity and NaN both use the all-ones exponent; reject them together.
    if (raw_exponent == 0x7FFu) {
        return Result<Coordinate, QuantizationError>::failure(
            QuantizationError::non_finite);
    }

    // Decompose into an integer significand m and a binary exponent e such that
    // |value| == m * 2^e exactly. Subnormals (raw_exponent == 0) carry no
    // implicit leading bit.
    std::uint64_t mantissa;
    int exponent;
    if (raw_exponent == 0) {
        mantissa = fraction;
        exponent = 1 - EXPONENT_BIAS - MANTISSA_BITS;
    } else {
        mantissa = fraction | (std::uint64_t { 1 } << MANTISSA_BITS);
        exponent = static_cast<int>(raw_exponent) - EXPONENT_BIAS - MANTISSA_BITS;
    }

    if (mantissa == 0) {
        return Result<Coordinate, QuantizationError>::success(Coordinate::from_raw(0));
    }

    // We want round-ties-away( |value| * 2^48 ) == round( m * 2^shift ), where
    // shift == exponent + FRACTIONAL_BITS. All of this is exact integer work, so
    // it is independent of the hardware floating-point rounding mode.
    const int shift = exponent + static_cast<int>(Coordinate::FRACTIONAL_BITS);

    unsigned __int128 magnitude;
    if (shift >= 0) {
        // A left shift of a 53-bit significand past 2^63 cannot be a valid
        // coordinate, and would also overflow the 128-bit accumulator.
        if (shift >= 64) {
            return Result<Coordinate, QuantizationError>::failure(
                QuantizationError::out_of_range);
        }
        magnitude = static_cast<unsigned __int128>(mantissa) << shift;
    } else {
        const unsigned drop = static_cast<unsigned>(-shift);
        if (drop >= 128) {
            magnitude = 0;
        } else {
            magnitude = static_cast<unsigned __int128>(mantissa) >> drop;
            const unsigned __int128 remainder =
                static_cast<unsigned __int128>(mantissa)
                & ((static_cast<unsigned __int128>(1) << drop) - 1);
            // Round half away from zero: since drop >= 1 here, the exact
            // half-step is 2^(drop-1). remainder >= half rounds the magnitude up.
            const unsigned __int128 half = static_cast<unsigned __int128>(1) << (drop - 1);
            if (remainder >= half) {
                magnitude += 1;
            }
        }
    }

    // Range: [INT64_MIN, INT64_MAX]. The negative endpoint 2^63 maps to
    // INT64_MIN and must never be formed by negating in signed 64-bit.
    const unsigned __int128 positive_max = static_cast<unsigned __int128>(INT64_MAX);
    const unsigned __int128 negative_max = static_cast<unsigned __int128>(1) << 63;

    if (sign == 0) {
        if (magnitude > positive_max) {
            return Result<Coordinate, QuantizationError>::failure(
                QuantizationError::out_of_range);
        }
        return Result<Coordinate, QuantizationError>::success(
            Coordinate::from_raw(static_cast<Storage>(magnitude)));
    }

    if (magnitude > negative_max) {
        return Result<Coordinate, QuantizationError>::failure(
            QuantizationError::out_of_range);
    }
    if (magnitude == negative_max) {
        return Result<Coordinate, QuantizationError>::success(
            Coordinate::from_raw(INT64_MIN));
    }
    return Result<Coordinate, QuantizationError>::success(
        Coordinate::from_raw(-static_cast<Storage>(magnitude)));
}

Result<Coordinate, ArithmeticError> checked_add(Coordinate p_lhs, Coordinate p_rhs) {
    Storage result;
    if (__builtin_add_overflow(p_lhs.raw(), p_rhs.raw(), &result)) {
        return Result<Coordinate, ArithmeticError>::failure(ArithmeticError::overflow);
    }
    return Result<Coordinate, ArithmeticError>::success(Coordinate::from_raw(result));
}

Result<Coordinate, ArithmeticError> checked_subtract(Coordinate p_lhs, Coordinate p_rhs) {
    Storage result;
    if (__builtin_sub_overflow(p_lhs.raw(), p_rhs.raw(), &result)) {
        return Result<Coordinate, ArithmeticError>::failure(ArithmeticError::overflow);
    }
    return Result<Coordinate, ArithmeticError>::success(Coordinate::from_raw(result));
}

Result<Point, ArithmeticError> checked_add(Point p_lhs, Point p_rhs) {
    auto x = checked_add(p_lhs.x, p_rhs.x);
    if (!x.has_value()) {
        return Result<Point, ArithmeticError>::failure(x.error());
    }
    auto y = checked_add(p_lhs.y, p_rhs.y);
    if (!y.has_value()) {
        return Result<Point, ArithmeticError>::failure(y.error());
    }
    return Result<Point, ArithmeticError>::success(Point { x.value(), y.value() });
}

Result<Point, ArithmeticError> checked_subtract(Point p_lhs, Point p_rhs) {
    auto x = checked_subtract(p_lhs.x, p_rhs.x);
    if (!x.has_value()) {
        return Result<Point, ArithmeticError>::failure(x.error());
    }
    auto y = checked_subtract(p_lhs.y, p_rhs.y);
    if (!y.has_value()) {
        return Result<Point, ArithmeticError>::failure(y.error());
    }
    return Result<Point, ArithmeticError>::success(Point { x.value(), y.value() });
}

} // namespace tiles
