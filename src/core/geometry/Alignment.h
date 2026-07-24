#pragma once

#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/Result.h"
#include "core/geometry/Polygon.h"

#include <cstddef>

namespace tiles {

// A strong index into a polygon's canonical vertex order. Edge i runs from
// vertex v[i] to vertex v[(i + 1) mod n]. Deliberately not interchangeable with
// a bare size_t.
class EdgeIndex final {
public:
    using Value = std::size_t;

    explicit constexpr EdgeIndex(Value p_value) :
        value_(p_value) {}

    constexpr Value value() const {
        return value_;
    }

    friend constexpr bool operator==(EdgeIndex p_lhs, EdgeIndex p_rhs) {
        return p_lhs.value_ == p_rhs.value_;
    }

    friend constexpr bool operator!=(EdgeIndex p_lhs, EdgeIndex p_rhs) {
        return !(p_lhs == p_rhs);
    }

private:
    Value value_;
};

enum class AlignmentError {
    anchor_edge_out_of_range,
    candidate_edge_out_of_range,
    incompatible_edges,
    translation_overflow,
    footprint_overflow,
    footprint_construction_failed,
};

// Derive one exact candidate placement that mates a complete anchor-footprint
// edge with a complete candidate prototile edge.
//
// With anchor edge a0 -> a1 and candidate canonical edge b0 -> b1, the edges
// are compatible exactly when their displacement vectors are opposite
// (a1 - a0 == b0 - b1); the candidate translation is then t = a0 - b1, verified
// so that b1 + t == a0 and b0 + t == a1. The candidate is finally built from
// its canonical prototile and t. Alignment proves only exact selected-edge
// agreement, not whole-footprint disjointness.
Result<Placement, AlignmentError> align_full_edge(
    const Polygon &p_anchor_footprint,
    EdgeIndex p_anchor_edge,
    const Prototile &p_candidate,
    EdgeIndex p_candidate_edge);

} // namespace tiles
