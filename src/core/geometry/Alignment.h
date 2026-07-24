#pragma once

#include "core/OrientedPrototile.h"
#include "core/Placement.h"
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

// A strong index into a polygon's canonical vertex sequence: vertex v[i].
// Rotating and re-normalizing a polygon may change which source corner occupies
// an index, so callers select an index from the actual oriented polygon they
// are joining. Deliberately not interchangeable with a bare size_t.
class VertexIndex final {
public:
    using Value = std::size_t;

    explicit constexpr VertexIndex(Value p_value) :
        value_(p_value) {}

    constexpr Value value() const {
        return value_;
    }

    friend constexpr bool operator==(VertexIndex p_lhs, VertexIndex p_rhs) {
        return p_lhs.value_ == p_rhs.value_;
    }

    friend constexpr bool operator!=(VertexIndex p_lhs, VertexIndex p_rhs) {
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

enum class VertexAlignmentError {
    anchor_vertex_out_of_range,
    candidate_vertex_out_of_range,
    translation_overflow,
    footprint_overflow,
    footprint_construction_failed,
};

// Derive one exact candidate placement that mates a complete anchor-footprint
// edge with a complete oriented candidate edge.
//
// With anchor edge a0 -> a1 and oriented candidate canonical edge b0 -> b1, the
// edges are compatible exactly when their displacement vectors are opposite
// (a1 - a0 == b0 - b1); the candidate translation is then t = a0 - b1, verified
// so that b1 + t == a0 and b0 + t == a1. The candidate is finally built from the
// same oriented prototile and t. Alignment proves only exact selected-edge
// agreement, not whole-footprint disjointness.
Result<Placement, AlignmentError> align_full_edge(
    const Polygon &p_anchor_footprint,
    EdgeIndex p_anchor_edge,
    const OrientedPrototile &p_candidate,
    EdgeIndex p_candidate_edge);

// Derive one exact candidate placement that mates a selected anchor-footprint
// vertex with a selected oriented candidate vertex.
//
// With anchor vertex a and oriented candidate vertex b, the candidate
// translation is t = a - b, verified so that b + t == a. The candidate is built
// from the same oriented prototile and t. Alignment proves only exact
// selected-vertex agreement: after insertion the two footprints may share no
// other contact, other vertices, partial edges, complete edges, or several
// disconnected contacts, all legal exactly when no interior overlap exists.
Result<Placement, VertexAlignmentError> align_vertex(
    const Polygon &p_anchor_footprint,
    VertexIndex p_anchor_vertex,
    const OrientedPrototile &p_candidate,
    VertexIndex p_candidate_vertex);

} // namespace tiles
