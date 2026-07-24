#pragma once

#include "core/Result.h"

#include <cstdint>
#include <optional>

namespace tiles::engine {

enum class SupplyError {
    zero_finite_amount,
};

// A configured capacity for one prototile: either a positive finite amount or
// unlimited. Supply is a capacity, never a mutable remaining-piece counter. Any
// future "remaining pieces" value is derived from this capacity and the
// authoritative arrangement, so nothing here is ever decremented and nothing can
// drift out of agreement with arrangement contents.
class Supply final {
public:
    using Amount = std::uint64_t;

    // A positive finite capacity. Zero is rejected: a prototile with no pieces is
    // simply absent from the palette, not a distinct zero-supply entry.
    static Result<Supply, SupplyError> finite(Amount p_amount) {
        if (p_amount == 0) {
            return Result<Supply, SupplyError>::failure(
                SupplyError::zero_finite_amount);
        }
        return Result<Supply, SupplyError>::success(Supply(p_amount));
    }

    static Supply unlimited() {
        return Supply(std::nullopt);
    }

    bool is_unlimited() const {
        return !amount_.has_value();
    }

    // The exact amount for a finite supply; empty for an unlimited supply.
    std::optional<Amount> finite_amount() const {
        return amount_;
    }

    friend bool operator==(Supply p_lhs, Supply p_rhs) {
        return p_lhs.amount_ == p_rhs.amount_;
    }

    friend bool operator!=(Supply p_lhs, Supply p_rhs) {
        return !(p_lhs == p_rhs);
    }

private:
    // Empty means unlimited; a present value is always positive by construction.
    explicit Supply(std::optional<Amount> p_amount) :
        amount_(p_amount) {}

    std::optional<Amount> amount_;
};

} // namespace tiles::engine
