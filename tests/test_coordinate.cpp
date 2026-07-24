#include "TestHarness.h"

#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"

#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace tiles;
using tiles_test::raw_pt;

namespace {

constexpr std::int64_t SCALE = Coordinate::SCALE;

} // namespace

TEST_CASE("coordinate from_raw round-trips at the extremes") {
    CHECK(Coordinate::from_raw(INT64_MIN).raw() == INT64_MIN);
    CHECK(Coordinate::from_raw(0).raw() == 0);
    CHECK(Coordinate::from_raw(INT64_MAX).raw() == INT64_MAX);
}

TEST_CASE("coordinate equality and ordering") {
    const auto a = Coordinate::from_raw(-5);
    const auto b = Coordinate::from_raw(-5);
    const auto c = Coordinate::from_raw(7);
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c);
    CHECK(c > a);
    CHECK(a <= b);
    CHECK(a >= b);
    CHECK(Coordinate::from_raw(INT64_MIN) < Coordinate::from_raw(INT64_MAX));
}

TEST_CASE("checked coordinate addition and subtraction at the limits") {
    const auto max = Coordinate::from_raw(INT64_MAX);
    const auto min = Coordinate::from_raw(INT64_MIN);
    const auto one = Coordinate::from_raw(1);

    CHECK(checked_add(max, one).has_value() == false);
    CHECK(checked_add(max, one).error() == ArithmeticError::overflow);
    CHECK(checked_subtract(min, one).error() == ArithmeticError::overflow);

    auto ok = checked_add(Coordinate::from_raw(3), Coordinate::from_raw(4));
    CHECK(ok.has_value());
    CHECK(ok.value() == Coordinate::from_raw(7));

    // INT64_MAX - INT64_MIN is not representable.
    CHECK(checked_subtract(max, min).has_value() == false);
}

TEST_CASE("point arithmetic fails when one component overflows") {
    const Point a = raw_pt(INT64_MAX, 0);
    const Point b = raw_pt(1, 1);
    CHECK(checked_add(a, b).has_value() == false);

    const Point c = raw_pt(5, INT64_MIN);
    const Point d = raw_pt(5, 1);
    CHECK(checked_subtract(c, d).has_value() == false);

    auto ok = checked_add(raw_pt(2, 3), raw_pt(4, 5));
    CHECK(ok.has_value());
    CHECK(ok.value() == raw_pt(6, 8));
}

TEST_CASE("quantize maps whole and fractional values on both signs") {
    CHECK(quantize_double(0.0).value() == Coordinate::from_raw(0));
    CHECK(quantize_double(-0.0).value() == Coordinate::from_raw(0));
    CHECK(quantize_double(1.0).value() == Coordinate::from_raw(SCALE));
    CHECK(quantize_double(-1.0).value() == Coordinate::from_raw(-SCALE));
    CHECK(quantize_double(0.5).value() == Coordinate::from_raw(SCALE / 2));
    CHECK(quantize_double(-0.25).value() == Coordinate::from_raw(-SCALE / 4));
}

TEST_CASE("quantize rounds half away from zero") {
    // 2^-49 * 2^48 == 0.5 exactly: a tie that rounds up in magnitude.
    const double half_up = std::ldexp(1.0, -49);
    CHECK(quantize_double(half_up).value() == Coordinate::from_raw(1));
    CHECK(quantize_double(-half_up).value() == Coordinate::from_raw(-1));

    // 63 * 2^-55 == 0.4921875 lattice units: below the tie, rounds toward zero.
    const double below = std::ldexp(63.0, -55);
    CHECK(quantize_double(below).value() == Coordinate::from_raw(0));
    CHECK(quantize_double(-below).value() == Coordinate::from_raw(0));

    // 65 * 2^-55 == 0.5078125 lattice units: above the tie, rounds away.
    const double above = std::ldexp(65.0, -55);
    CHECK(quantize_double(above).value() == Coordinate::from_raw(1));
    CHECK(quantize_double(-above).value() == Coordinate::from_raw(-1));

    // 3 * 2^-49 == 1.5: ties away from zero to 2.
    CHECK(quantize_double(std::ldexp(3.0, -49)).value() == Coordinate::from_raw(2));
}

TEST_CASE("quantize accepts representable endpoints and rejects out-of-range") {
    // The negative endpoint -32768 maps exactly to INT64_MIN and must not be
    // formed by negating in signed 64-bit.
    CHECK(quantize_double(-32768.0).value() == Coordinate::from_raw(INT64_MIN));
    CHECK(quantize_double(32767.0).value() == Coordinate::from_raw(std::int64_t { 32767 } * SCALE));

    // +32768 * 2^48 == 2^63 == INT64_MAX + 1: just out of range.
    CHECK(quantize_double(32768.0).error() == QuantizationError::out_of_range);
    // Well past the range on both sides.
    CHECK(quantize_double(-65536.0).error() == QuantizationError::out_of_range);
    CHECK(quantize_double(1e30).error() == QuantizationError::out_of_range);
}

TEST_CASE("quantize rejects every non-finite class") {
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(quantize_double(inf).error() == QuantizationError::non_finite);
    CHECK(quantize_double(-inf).error() == QuantizationError::non_finite);
    CHECK(quantize_double(std::numeric_limits<double>::quiet_NaN()).error()
        == QuantizationError::non_finite);
    if (std::numeric_limits<double>::has_signaling_NaN) {
        CHECK(quantize_double(std::numeric_limits<double>::signaling_NaN()).error()
            == QuantizationError::non_finite);
    }
}

TEST_CASE("quantize is independent of the floating-point rounding mode") {
    const double tie = std::ldexp(1.0, -49); // exact half-step
    const int modes[] = { FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO };
    const int saved = std::fegetround();
    for (int mode : modes) {
        if (std::fesetround(mode) != 0) {
            continue;
        }
        CHECK(quantize_double(tie).value() == Coordinate::from_raw(1));
        CHECK(quantize_double(-tie).value() == Coordinate::from_raw(-1));
        CHECK(quantize_double(std::ldexp(3.0, -49)).value() == Coordinate::from_raw(2));
    }
    std::fesetround(saved);
}
