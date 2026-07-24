#include "core/Arrangement.h"

#include "core/geometry/Intersection.h"

#include <utility>

namespace tiles {

namespace {

JoinErrorCode join_code_from_alignment(AlignmentError p_error) {
    switch (p_error) {
        case AlignmentError::anchor_edge_out_of_range:
            return JoinErrorCode::anchor_edge_out_of_range;
        case AlignmentError::candidate_edge_out_of_range:
            return JoinErrorCode::candidate_edge_out_of_range;
        case AlignmentError::incompatible_edges:
            return JoinErrorCode::incompatible_edges;
        case AlignmentError::translation_overflow:
            return JoinErrorCode::translation_overflow;
        case AlignmentError::footprint_overflow:
            return JoinErrorCode::footprint_overflow;
        case AlignmentError::footprint_construction_failed:
            return JoinErrorCode::footprint_construction_failed;
    }
    // Unreachable: every AlignmentError is mapped above.
    return JoinErrorCode::footprint_construction_failed;
}

JoinErrorCode join_code_from_vertex_alignment(VertexAlignmentError p_error) {
    switch (p_error) {
        case VertexAlignmentError::anchor_vertex_out_of_range:
            return JoinErrorCode::anchor_vertex_out_of_range;
        case VertexAlignmentError::candidate_vertex_out_of_range:
            return JoinErrorCode::candidate_vertex_out_of_range;
        case VertexAlignmentError::translation_overflow:
            return JoinErrorCode::translation_overflow;
        case VertexAlignmentError::footprint_overflow:
            return JoinErrorCode::footprint_overflow;
        case VertexAlignmentError::footprint_construction_failed:
            return JoinErrorCode::footprint_construction_failed;
    }
    // Unreachable: every VertexAlignmentError is mapped above.
    return JoinErrorCode::footprint_construction_failed;
}

JoinError join_error_from_arrangement(const ArrangementError &p_error) {
    switch (p_error.code) {
        case ArrangementErrorCode::interior_overlap:
            return JoinError { JoinErrorCode::interior_overlap, p_error.conflicting_placement };
        case ArrangementErrorCode::identifier_exhausted:
            return JoinError { JoinErrorCode::identifier_exhausted, std::nullopt };
    }
    // Unreachable: every ArrangementErrorCode is mapped above.
    return JoinError { JoinErrorCode::identifier_exhausted, std::nullopt };
}

} // namespace

std::optional<PlacementId> Arrangement::next_id() const {
    if (exhausted_) {
        return std::nullopt;
    }
    return PlacementId(next_id_);
}

Result<PlacementId, ArrangementError> Arrangement::try_insert(Placement p_placement) {
    // Compare the candidate footprint against every existing footprint. A single
    // positive-area interior overlap rejects the insertion and names the
    // conflicting placement.
    for (const Entry &entry : entries_) {
        if (interiors_overlap(p_placement.footprint(), entry.placement.footprint())) {
            return Result<PlacementId, ArrangementError>::failure(
                ArrangementError { ArrangementErrorCode::interior_overlap, entry.id });
        }
    }

    if (exhausted_) {
        return Result<PlacementId, ArrangementError>::failure(
            ArrangementError { ArrangementErrorCode::identifier_exhausted, std::nullopt });
    }

    // The candidate is interior-disjoint from every existing entry, so appending
    // it preserves pairwise interior disjointness. Consume the id only now.
    const PlacementId id(next_id_);
    entries_.push_back(Entry { id, std::move(p_placement) });
    if (next_id_ == UINT64_MAX) {
        exhausted_ = true;
    } else {
        ++next_id_;
    }
    return Result<PlacementId, ArrangementError>::success(id);
}

namespace {

// The independent whole-footprint proof lives in try_insert; it is also the only
// place that mutates state, so routing every derived candidate through it keeps
// a failed join fully transactional.
const Entry *find_anchor(const std::vector<Entry> &p_entries, PlacementId p_anchor) {
    for (const Entry &entry : p_entries) {
        if (entry.id == p_anchor) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

Result<PlacementId, JoinError> Arrangement::try_join_full_edges(
    PlacementId p_anchor,
    EdgeIndex p_anchor_edge,
    const OrientedPrototile &p_candidate,
    EdgeIndex p_candidate_edge) {
    // Resolve the anchor placement. Its footprint is read only during alignment,
    // which completes before any insertion mutates the entry storage.
    const Entry *anchor = find_anchor(entries_, p_anchor);
    if (anchor == nullptr) {
        return Result<PlacementId, JoinError>::failure(
            JoinError { JoinErrorCode::anchor_not_found, std::nullopt });
    }

    auto aligned = align_full_edge(
        anchor->placement.footprint(), p_anchor_edge, p_candidate, p_candidate_edge);
    if (!aligned.has_value()) {
        return Result<PlacementId, JoinError>::failure(
            JoinError { join_code_from_alignment(aligned.error()), std::nullopt });
    }

    auto inserted = try_insert(std::move(aligned).value());
    if (!inserted.has_value()) {
        return Result<PlacementId, JoinError>::failure(
            join_error_from_arrangement(inserted.error()));
    }
    return Result<PlacementId, JoinError>::success(inserted.value());
}

Result<PlacementId, JoinError> Arrangement::try_join_vertices(
    PlacementId p_anchor,
    VertexIndex p_anchor_vertex,
    const OrientedPrototile &p_candidate,
    VertexIndex p_candidate_vertex) {
    const Entry *anchor = find_anchor(entries_, p_anchor);
    if (anchor == nullptr) {
        return Result<PlacementId, JoinError>::failure(
            JoinError { JoinErrorCode::anchor_not_found, std::nullopt });
    }

    auto aligned = align_vertex(
        anchor->placement.footprint(), p_anchor_vertex, p_candidate, p_candidate_vertex);
    if (!aligned.has_value()) {
        return Result<PlacementId, JoinError>::failure(
            JoinError { join_code_from_vertex_alignment(aligned.error()), std::nullopt });
    }

    auto inserted = try_insert(std::move(aligned).value());
    if (!inserted.has_value()) {
        return Result<PlacementId, JoinError>::failure(
            join_error_from_arrangement(inserted.error()));
    }
    return Result<PlacementId, JoinError>::success(inserted.value());
}

Arrangement Arrangement::testing_with_next_id(std::uint64_t p_next_id) {
    Arrangement arrangement;
    arrangement.next_id_ = p_next_id;
    return arrangement;
}

} // namespace tiles
