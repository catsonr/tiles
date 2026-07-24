#include "core/geometry/ExactInteger.h"

namespace tiles {

Int256 Int256::from_i64(std::int64_t p_value) {
    const std::uint64_t low = static_cast<std::uint64_t>(p_value);
    const std::uint64_t extend = (p_value < 0) ? ~std::uint64_t { 0 } : std::uint64_t { 0 };
    return Int256(std::array<std::uint64_t, 4> { low, extend, extend, extend });
}

Int256 Int256::from_i128(__int128 p_value) {
    const unsigned __int128 bits = static_cast<unsigned __int128>(p_value);
    const std::uint64_t low = static_cast<std::uint64_t>(bits);
    const std::uint64_t high = static_cast<std::uint64_t>(bits >> 64);
    const std::uint64_t extend = (p_value < 0) ? ~std::uint64_t { 0 } : std::uint64_t { 0 };
    return Int256(std::array<std::uint64_t, 4> { low, high, extend, extend });
}

Int256 Int256::multiply(__int128 p_lhs, __int128 p_rhs) {
    const bool negative = (p_lhs < 0) != (p_rhs < 0);

    // Operands are far from INT128_MIN (they hold coordinate values or 65-bit
    // differences), so negation of the magnitude is safe.
    const unsigned __int128 lhs =
        p_lhs < 0 ? static_cast<unsigned __int128>(-p_lhs) : static_cast<unsigned __int128>(p_lhs);
    const unsigned __int128 rhs =
        p_rhs < 0 ? static_cast<unsigned __int128>(-p_rhs) : static_cast<unsigned __int128>(p_rhs);

    const std::uint64_t a0 = static_cast<std::uint64_t>(lhs);
    const std::uint64_t a1 = static_cast<std::uint64_t>(lhs >> 64);
    const std::uint64_t b0 = static_cast<std::uint64_t>(rhs);
    const std::uint64_t b1 = static_cast<std::uint64_t>(rhs >> 64);

    const unsigned __int128 p00 = static_cast<unsigned __int128>(a0) * b0;
    const unsigned __int128 p01 = static_cast<unsigned __int128>(a0) * b1;
    const unsigned __int128 p10 = static_cast<unsigned __int128>(a1) * b0;
    const unsigned __int128 p11 = static_cast<unsigned __int128>(a1) * b1;

    const std::uint64_t r0 = static_cast<std::uint64_t>(p00);

    unsigned __int128 carry = (p00 >> 64)
        + static_cast<std::uint64_t>(p01)
        + static_cast<std::uint64_t>(p10);
    const std::uint64_t r1 = static_cast<std::uint64_t>(carry);

    carry = (carry >> 64) + (p01 >> 64) + (p10 >> 64) + static_cast<std::uint64_t>(p11);
    const std::uint64_t r2 = static_cast<std::uint64_t>(carry);

    carry = (carry >> 64) + (p11 >> 64);
    const std::uint64_t r3 = static_cast<std::uint64_t>(carry);

    const Int256 magnitude(std::array<std::uint64_t, 4> { r0, r1, r2, r3 });
    return negative ? magnitude.negated() : magnitude;
}

Int256 Int256::operator+(const Int256 &p_other) const {
    std::array<std::uint64_t, 4> result {};
    unsigned __int128 carry = 0;
    for (int i = 0; i < 4; ++i) {
        const unsigned __int128 sum =
            static_cast<unsigned __int128>(limbs_[i]) + p_other.limbs_[i] + carry;
        result[i] = static_cast<std::uint64_t>(sum);
        carry = sum >> 64;
    }
    return Int256(result);
}

Int256 Int256::negated() const {
    std::array<std::uint64_t, 4> result {};
    unsigned __int128 carry = 1;
    for (int i = 0; i < 4; ++i) {
        const unsigned __int128 sum =
            static_cast<unsigned __int128>(~limbs_[i]) + carry;
        result[i] = static_cast<std::uint64_t>(sum);
        carry = sum >> 64;
    }
    return Int256(result);
}

Int256 Int256::operator-(const Int256 &p_other) const {
    return *this + p_other.negated();
}

bool Int256::is_zero() const {
    return limbs_[0] == 0 && limbs_[1] == 0 && limbs_[2] == 0 && limbs_[3] == 0;
}

bool Int256::is_negative() const {
    return (limbs_[3] >> 63) != 0;
}

int Int256::sign() const {
    if (is_zero()) {
        return 0;
    }
    return is_negative() ? -1 : 1;
}

int Int256::compare(const Int256 &p_lhs, const Int256 &p_rhs) {
    // Operands here stay well within 256-bit range, so the difference does not
    // overflow and its sign gives the ordering.
    return (p_lhs - p_rhs).sign();
}

} // namespace tiles
