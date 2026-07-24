#pragma once

#include <array>
#include <cstdint>

namespace tiles {

// A core-internal exact signed integer with 256 bits of range, stored as four
// little-endian two's-complement 64-bit limbs. It is enough to hold any
// orientation determinant (up to ~129 magnitude bits) or shoelace area sum over
// a realistic vertex count without loss. Its limb representation is private to
// the geometry kernel; it is not a domain or Godot type.
//
// Only the operations the predicates need are provided: construction from 64-
// and 128-bit values, exact multiplication of coordinate-sized operands, exact
// addition/subtraction and accumulation, sign, equality, and ordering.
class Int256 final {
public:
    constexpr Int256() :
        limbs_ { 0, 0, 0, 0 } {}

    static Int256 from_i64(std::int64_t p_value);
    static Int256 from_i128(__int128 p_value);

    // Exact 256-bit product of two operands that each fit in a signed 128-bit
    // integer (coordinate values and coordinate differences both qualify).
    static Int256 multiply(__int128 p_lhs, __int128 p_rhs);

    Int256 operator+(const Int256 &p_other) const;
    Int256 operator-(const Int256 &p_other) const;
    Int256 negated() const;

    bool is_zero() const;
    bool is_negative() const;

    // -1, 0, or +1.
    int sign() const;

    friend bool operator==(const Int256 &p_lhs, const Int256 &p_rhs) {
        return p_lhs.limbs_ == p_rhs.limbs_;
    }

    friend bool operator!=(const Int256 &p_lhs, const Int256 &p_rhs) {
        return !(p_lhs == p_rhs);
    }

    // -1 if lhs < rhs, 0 if equal, +1 if lhs > rhs.
    static int compare(const Int256 &p_lhs, const Int256 &p_rhs);

    friend bool operator<(const Int256 &p_lhs, const Int256 &p_rhs) {
        return compare(p_lhs, p_rhs) < 0;
    }

private:
    explicit constexpr Int256(std::array<std::uint64_t, 4> p_limbs) :
        limbs_(p_limbs) {}

    std::array<std::uint64_t, 4> limbs_;
};

} // namespace tiles
