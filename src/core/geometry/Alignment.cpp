#include "core/geometry/Alignment.h"

namespace tiles {

namespace {

// Exact endpoint difference; may need 65 signed bits, so it is not a
// Coordinate value and is formed directly in signed 128-bit.
__int128 difference(Coordinate p_lhs, Coordinate p_rhs) {
    return static_cast<__int128>(p_lhs.raw()) - static_cast<__int128>(p_rhs.raw());
}

Point edge_start(const Polygon::Vertices &p_vertices, EdgeIndex p_edge) {
    return p_vertices[p_edge.value()];
}

Point edge_end(const Polygon::Vertices &p_vertices, EdgeIndex p_edge) {
    const std::size_t next = (p_edge.value() + 1) % p_vertices.size();
    return p_vertices[next];
}

AlignmentError from_placement_error(PlacementError p_error) {
    switch (p_error) {
        case PlacementError::footprint_overflow:
            return AlignmentError::footprint_overflow;
        case PlacementError::footprint_construction_failed:
            return AlignmentError::footprint_construction_failed;
    }
    // Unreachable: every PlacementError is mapped above.
    return AlignmentError::footprint_construction_failed;
}

VertexAlignmentError vertex_from_placement_error(PlacementError p_error) {
    switch (p_error) {
        case PlacementError::footprint_overflow:
            return VertexAlignmentError::footprint_overflow;
        case PlacementError::footprint_construction_failed:
            return VertexAlignmentError::footprint_construction_failed;
    }
    // Unreachable: every PlacementError is mapped above.
    return VertexAlignmentError::footprint_construction_failed;
}

} // namespace

Result<Placement, AlignmentError> align_full_edge(
    const Polygon &p_anchor_footprint,
    EdgeIndex p_anchor_edge,
    const OrientedPrototile &p_candidate,
    EdgeIndex p_candidate_edge) {
    const Polygon::Vertices &anchor = p_anchor_footprint.vertices();
    const Polygon::Vertices &candidate = p_candidate.canonical_polygon().vertices();

    if (p_anchor_edge.value() >= anchor.size()) {
        return Result<Placement, AlignmentError>::failure(
            AlignmentError::anchor_edge_out_of_range);
    }
    if (p_candidate_edge.value() >= candidate.size()) {
        return Result<Placement, AlignmentError>::failure(
            AlignmentError::candidate_edge_out_of_range);
    }

    const Point a0 = edge_start(anchor, p_anchor_edge);
    const Point a1 = edge_end(anchor, p_anchor_edge);
    const Point b0 = edge_start(candidate, p_candidate_edge);
    const Point b1 = edge_end(candidate, p_candidate_edge);

    // Compatible exactly when the displacement vectors are opposites:
    // a1 - a0 == b0 - b1. Compared in the exact wide representation because the
    // components may require 65 signed bits.
    const bool compatible =
        difference(a1.x, a0.x) == difference(b0.x, b1.x)
        && difference(a1.y, a0.y) == difference(b0.y, b1.y);
    if (!compatible) {
        return Result<Placement, AlignmentError>::failure(
            AlignmentError::incompatible_edges);
    }

    // Candidate translation t = a0 - b1. Both components must fit Coordinate.
    auto translation = checked_subtract(a0, b1);
    if (!translation.has_value()) {
        return Result<Placement, AlignmentError>::failure(
            AlignmentError::translation_overflow);
    }
    const Point t = translation.value();

    // Verify the selected edge maps exactly onto the anchor edge with checked
    // arithmetic: b1 + t == a0 and b0 + t == a1.
    auto b1_mapped = checked_add(b1, t);
    auto b0_mapped = checked_add(b0, t);
    if (!b1_mapped.has_value() || !b0_mapped.has_value()) {
        return Result<Placement, AlignmentError>::failure(
            AlignmentError::translation_overflow);
    }
    if (b1_mapped.value() != a0 || b0_mapped.value() != a1) {
        return Result<Placement, AlignmentError>::failure(
            AlignmentError::incompatible_edges);
    }

    auto placement = Placement::make(p_candidate, t);
    if (!placement.has_value()) {
        return Result<Placement, AlignmentError>::failure(
            from_placement_error(placement.error()));
    }
    return Result<Placement, AlignmentError>::success(std::move(placement).value());
}

Result<Placement, VertexAlignmentError> align_vertex(
    const Polygon &p_anchor_footprint,
    VertexIndex p_anchor_vertex,
    const OrientedPrototile &p_candidate,
    VertexIndex p_candidate_vertex) {
    const Polygon::Vertices &anchor = p_anchor_footprint.vertices();
    const Polygon::Vertices &candidate = p_candidate.canonical_polygon().vertices();

    if (p_anchor_vertex.value() >= anchor.size()) {
        return Result<Placement, VertexAlignmentError>::failure(
            VertexAlignmentError::anchor_vertex_out_of_range);
    }
    if (p_candidate_vertex.value() >= candidate.size()) {
        return Result<Placement, VertexAlignmentError>::failure(
            VertexAlignmentError::candidate_vertex_out_of_range);
    }

    const Point a = anchor[p_anchor_vertex.value()];
    const Point b = candidate[p_candidate_vertex.value()];

    // Candidate translation t = a - b. Both components must fit Coordinate.
    auto translation = checked_subtract(a, b);
    if (!translation.has_value()) {
        return Result<Placement, VertexAlignmentError>::failure(
            VertexAlignmentError::translation_overflow);
    }
    const Point t = translation.value();

    // Verify the selected vertex maps exactly onto the anchor vertex with
    // checked arithmetic: b + t == a. Because t = a - b componentwise and a is a
    // representable coordinate, the sum is exactly a and never overflows, so the
    // equality mismatch branch is an unreachable invariant backstop.
    auto b_mapped = checked_add(b, t);
    if (!b_mapped.has_value() || b_mapped.value() != a) {
        return Result<Placement, VertexAlignmentError>::failure(
            VertexAlignmentError::translation_overflow);
    }

    auto placement = Placement::make(p_candidate, t);
    if (!placement.has_value()) {
        return Result<Placement, VertexAlignmentError>::failure(
            vertex_from_placement_error(placement.error()));
    }
    return Result<Placement, VertexAlignmentError>::success(
        std::move(placement).value());
}

} // namespace tiles
