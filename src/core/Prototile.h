#pragma once

#include "core/Result.h"
#include "core/geometry/Polygon.h"

#include <cstdint>

namespace tiles {

// A strong identity for a prototile. Every bit pattern is valid; equality and
// ordering compare the stored integer. It is deliberately not interchangeable
// with a bare integer.
class PrototileId final {
public:
    using Value = std::uint64_t;

    explicit constexpr PrototileId(Value p_value) :
        value_(p_value) {}

    constexpr Value value() const {
        return value_;
    }

    friend constexpr bool operator==(PrototileId p_lhs, PrototileId p_rhs) {
        return p_lhs.value_ == p_rhs.value_;
    }

    friend constexpr bool operator!=(PrototileId p_lhs, PrototileId p_rhs) {
        return !(p_lhs == p_rhs);
    }

    friend constexpr bool operator<(PrototileId p_lhs, PrototileId p_rhs) {
        return p_lhs.value_ < p_rhs.value_;
    }

private:
    Value value_;
};

enum class PrototileError {
    normalization_overflow,
};

// A named, particular polygon admitted as a playable tile type. Its polygon is
// translation-normalized so its canonical first vertex sits at the local
// origin; lattice translations of one boundary yield identical local geometry.
// Rotation is deliberately not normalized: it is a placement dimension.
//
// Congruence under rotation is future work, so no operator== is defined here:
// identity equality and geometric congruence are different relations.
class Prototile final {
public:
    static Result<Prototile, PrototileError> make(PrototileId p_id, const Polygon &p_polygon);

    PrototileId id() const {
        return id_;
    }

    const Polygon &polygon() const {
        return polygon_;
    }

private:
    PrototileId id_;
    Polygon polygon_;

    explicit Prototile(PrototileId p_id, Polygon p_polygon);
};

} // namespace tiles
