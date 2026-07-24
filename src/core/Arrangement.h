#pragma once

#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/Result.h"
#include "core/geometry/Alignment.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace tiles {

// A strong identity for a placement inside an arrangement. Equality and
// ordering compare the stored integer. Arrangements allocate ids monotonically
// from zero; an id is consumed only after a successful insertion.
class PlacementId final {
public:
    using Value = std::uint64_t;

    explicit constexpr PlacementId(Value p_value) :
        value_(p_value) {}

    constexpr Value value() const {
        return value_;
    }

    friend constexpr bool operator==(PlacementId p_lhs, PlacementId p_rhs) {
        return p_lhs.value_ == p_rhs.value_;
    }

    friend constexpr bool operator!=(PlacementId p_lhs, PlacementId p_rhs) {
        return !(p_lhs == p_rhs);
    }

    friend constexpr bool operator<(PlacementId p_lhs, PlacementId p_rhs) {
        return p_lhs.value_ < p_rhs.value_;
    }

private:
    Value value_;
};

// One stored placement together with its identity. The storage sequence is not
// mathematical ordering; an arrangement is an unordered collection.
struct Entry final {
    PlacementId id;
    Placement placement;
};

enum class ArrangementErrorCode {
    interior_overlap,
    identifier_exhausted,
};

struct ArrangementError final {
    ArrangementErrorCode code;
    // Populated only for interior_overlap.
    std::optional<PlacementId> conflicting_placement;
};

enum class JoinErrorCode {
    anchor_not_found,
    anchor_edge_out_of_range,
    candidate_edge_out_of_range,
    anchor_vertex_out_of_range,
    candidate_vertex_out_of_range,
    incompatible_edges,
    translation_overflow,
    footprint_overflow,
    footprint_construction_failed,
    interior_overlap,
    identifier_exhausted,
};

struct JoinError final {
    JoinErrorCode code;
    // Populated only for interior_overlap.
    std::optional<PlacementId> conflicting_placement;
};

// A finite collection of placements whose footprint interiors are pairwise
// disjoint. The invariant holds by construction: the only ways to add a
// placement are tryInsert and tryJoin, both of which reject any candidate whose
// interior overlaps an existing footprint. A default-constructed arrangement is
// empty and valid.
class Arrangement final {
public:
    Arrangement() = default;

    // Read-only view of stored entries. Order is storage, not meaning.
    const std::vector<Entry> &entries() const {
        return entries_;
    }

    // The id the next successful insertion will receive, if any remain. Exposed
    // for observing the allocator across a failed operation.
    std::optional<PlacementId> next_id() const;

    // Insert a placement if its footprint interior is disjoint from every
    // existing footprint. On success allocates the next id and appends. On
    // failure the arrangement is left completely unchanged.
    Result<PlacementId, ArrangementError> try_insert(Placement p_placement);

    // Derive a candidate placement by mating a complete anchor footprint edge
    // with a complete oriented candidate edge, then insert it. Transactional: a
    // failure at any stage leaves entries, ids, ordering, and the allocator
    // unchanged.
    Result<PlacementId, JoinError> try_join_full_edges(
        PlacementId p_anchor,
        EdgeIndex p_anchor_edge,
        const OrientedPrototile &p_candidate,
        EdgeIndex p_candidate_edge);

    // Derive a candidate placement by mating a selected anchor footprint vertex
    // with a selected oriented candidate vertex, then insert it. Alignment
    // proves only the selected vertex equality; the whole-footprint proof still
    // lives in insertion. Transactional in the same way as the full-edge join.
    Result<PlacementId, JoinError> try_join_vertices(
        PlacementId p_anchor,
        VertexIndex p_anchor_vertex,
        const OrientedPrototile &p_candidate,
        VertexIndex p_candidate_vertex);

    // Test-only construction seam: an empty arrangement whose id allocator
    // starts at p_next_id. It cannot produce an invalid arrangement (an empty
    // arrangement is always valid, and ids stay monotonic from any start), so
    // it exists solely to exercise identifier exhaustion without allocating an
    // astronomical number of placements.
    static Arrangement testing_with_next_id(std::uint64_t p_next_id);

private:
    std::vector<Entry> entries_;
    std::uint64_t next_id_ = 0;
    bool exhausted_ = false;
};

} // namespace tiles
