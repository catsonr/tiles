#include "TestHarness.h"

#include "core/Orientation.h"

#include <cstdint>

using namespace tiles;

namespace {

Orientation make_or_die(std::uint32_t p_step, std::uint32_t p_order) {
    auto result = Orientation::make(p_step, p_order);
    return result.value();
}

} // namespace

TEST_CASE("orientation rejects order zero") {
    auto zero_order = Orientation::make(0, 0);
    CHECK(zero_order.has_value() == false);
    CHECK(zero_order.error() == OrientationError::zero_order);

    // A nonzero step over a zero order is still rejected.
    auto also_zero = Orientation::make(3, 0);
    CHECK(also_zero.has_value() == false);
    CHECK(also_zero.error() == OrientationError::zero_order);
}

TEST_CASE("every whole turn canonicalizes to zero over one") {
    const Orientation reference = Orientation::reference();
    CHECK(reference.step() == 0u);
    CHECK(reference.order() == 1u);

    CHECK(make_or_die(0, 1) == reference);
    CHECK(make_or_die(4, 4) == reference);
    CHECK(make_or_die(8, 8) == reference);
    CHECK(make_or_die(4, 1) == reference);   // 4 whole turns
    CHECK(make_or_die(12, 4) == reference);  // reduces to 0
    CHECK(make_or_die(1000000, 1000) == reference);
}

TEST_CASE("equivalent fractions reduce to one canonical representative") {
    CHECK(make_or_die(2, 8) == Orientation::quarter());
    CHECK(make_or_die(1, 4) == Orientation::quarter());
    CHECK(make_or_die(250, 1000) == Orientation::quarter());

    CHECK(make_or_die(2, 4) == Orientation::half());
    CHECK(make_or_die(4, 8) == Orientation::half());
    CHECK(make_or_die(500, 1000) == Orientation::half());

    CHECK(make_or_die(6, 8) == Orientation::three_quarter());
    CHECK(make_or_die(3, 4) == Orientation::three_quarter());
    CHECK(make_or_die(750, 1000) == Orientation::three_quarter());

    // A reduced representative stores lowest terms.
    const Orientation quarter = make_or_die(2, 8);
    CHECK(quarter.step() == 1u);
    CHECK(quarter.order() == 4u);
}

TEST_CASE("distinct quarter turns are unequal") {
    const Orientation r = Orientation::reference();
    const Orientation q = Orientation::quarter();
    const Orientation h = Orientation::half();
    const Orientation t = Orientation::three_quarter();

    CHECK(r != q);
    CHECK(q != h);
    CHECK(h != t);
    CHECK(r != t);
    CHECK(r != h);
    CHECK(q != t);
}

TEST_CASE("orientation ordering is angular within one turn") {
    const Orientation r = Orientation::reference();
    const Orientation q = Orientation::quarter();
    const Orientation h = Orientation::half();
    const Orientation t = Orientation::three_quarter();

    CHECK(r < q);
    CHECK(q < h);
    CHECK(h < t);
    CHECK(r < t);

    // Strictness: nothing is less than itself, and order is antisymmetric.
    CHECK(!(q < q));
    CHECK(!(h < q));

    // A finer angle between two quarter turns orders correctly.
    const Orientation eighth = make_or_die(1, 8);
    CHECK(r < eighth);
    CHECK(eighth < q);
}

TEST_CASE("angular ordering uses exact cross products near the component limits") {
    const std::uint32_t big = 0xFFFFFFFFu;

    // 1/big and 2/big share the largest representable order; the cross products
    // 1*big and 2*big both fit in 64 bits, so ordering stays exact.
    const Orientation tiny = make_or_die(1, big);
    const Orientation twice = make_or_die(2, big);
    CHECK(tiny < twice);
    CHECK(!(twice < tiny));
    CHECK(tiny != twice);

    // A whole turn at the limit still collapses to the reference.
    CHECK(make_or_die(big, big) == Orientation::reference());

    // (big-1)/big is just under a full turn: greater than every quarter turn,
    // and the reference angle 0 is the least, so nothing is below it.
    const Orientation almost = make_or_die(big - 1, big);
    CHECK(Orientation::three_quarter() < almost);
    CHECK(!(almost < Orientation::reference()));
}
