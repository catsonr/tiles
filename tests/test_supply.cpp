#include "TestHarness.h"

#include "engine/Supply.h"

#include <cstdint>
#include <optional>

using tiles::engine::Supply;
using tiles::engine::SupplyError;

TEST_CASE("supply rejects a zero finite amount") {
    auto supply = Supply::finite(0);
    CHECK(!supply);
    if (!supply) {
        CHECK(supply.error() == SupplyError::zero_finite_amount);
    }
}

TEST_CASE("supply accepts a finite amount of one") {
    auto supply = Supply::finite(1);
    CHECK(bool(supply));
    if (supply) {
        CHECK(!supply.value().is_unlimited());
        CHECK(supply.value().finite_amount() == std::optional<Supply::Amount>(1));
    }
}

TEST_CASE("supply accepts the maximum finite amount unchanged") {
    const Supply::Amount max = UINT64_MAX;
    auto supply = Supply::finite(max);
    CHECK(bool(supply));
    if (supply) {
        CHECK(!supply.value().is_unlimited());
        CHECK(supply.value().finite_amount() == std::optional<Supply::Amount>(max));
    }
}

TEST_CASE("unlimited supply is distinct from every finite supply") {
    const Supply unlimited = Supply::unlimited();
    CHECK(unlimited.is_unlimited());
    CHECK(unlimited.finite_amount() == std::nullopt);
    CHECK(unlimited != Supply::finite(1).value());
    CHECK(unlimited != Supply::finite(UINT64_MAX).value());
}

TEST_CASE("supply equality distinguishes kind and amount") {
    CHECK(Supply::unlimited() == Supply::unlimited());
    CHECK(Supply::finite(3).value() == Supply::finite(3).value());
    CHECK(Supply::finite(3).value() != Supply::finite(4).value());
    CHECK(Supply::finite(3).value() != Supply::unlimited());
    CHECK(Supply::unlimited() != Supply::finite(3).value());
}
