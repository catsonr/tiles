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

// The one way removal fails: no stored entry carries the named identity. There
// is deliberately no second alternative — removal proves nothing geometric, so
// nothing else about the arrangement can reject it.
enum class RemovalError {
    placement_not_found,
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

    // Prove that a placement is insertable right now, without mutating
    // anything. On success the supplied placement is returned unchanged by
    // value: no PlacementId is predicted, reserved, or consumed. Failures are
    // the same typed values in the same precedence try_insert applies — the
    // first conflicting placement in storage order, then identifier exhaustion.
    //
    // This is the one insertion proof. try_insert and both join previews route
    // through it, so no caller can reach a second, drifting copy of it.
    Result<Placement, ArrangementError> preview_insert(Placement p_placement) const;

    // Insert a placement if its footprint interior is disjoint from every
    // existing footprint. On success allocates the next id and appends. On
    // failure the arrangement is left completely unchanged.
    Result<PlacementId, ArrangementError> try_insert(Placement p_placement);

    // Derive the exact placement that an immediate try_join_full_edges with the
    // same arguments would attempt to insert, and prove it insertable, without
    // mutating anything.
    //
    // The returned placement carries the supplied oriented prototile unchanged,
    // the exact translation derived by alignment, and its own authoritative
    // footprint. It has no PlacementId: nothing is reserved, consumed, or
    // predicted. Failures are the same typed values in the same precedence as
    // the mutating verb — anchor, features, compatibility, footprint, overlap,
    // identifier — so a successful preview against an unmodified arrangement
    // implies an immediately following join succeeds with equal geometry.
    Result<Placement, JoinError> preview_join_full_edges(
        PlacementId p_anchor,
        EdgeIndex p_anchor_edge,
        const OrientedPrototile &p_candidate,
        EdgeIndex p_candidate_edge) const;

    // The vertex-mating counterpart of preview_join_full_edges, with the same
    // purity and precedence guarantees.
    Result<Placement, JoinError> preview_join_vertices(
        PlacementId p_anchor,
        VertexIndex p_anchor_vertex,
        const OrientedPrototile &p_candidate,
        VertexIndex p_candidate_vertex) const;

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

    // Remove the one stored entry carrying p_placement, naming it by its stable
    // identity alone. On success the same id is returned and every surviving
    // entry keeps its identity, its placement, and its relative storage order.
    // On failure nothing is touched at all.
    //
    // Removal cannot break the arrangement invariant: a subset of a pairwise
    // interior-disjoint collection is still pairwise interior-disjoint. It may
    // leave the coverage disconnected or drop a placement some later command
    // names as an anchor; both are legal consequences, not errors here.
    //
    // The id allocator is untouched in either case. Identities are never reused
    // merely because an entry was deleted — given 0, 1, 2, removing 1 and
    // inserting again yields 3 — and an exhausted arrangement stays exhausted,
    // because deletion frees space, not another representable identity.
    Result<PlacementId, RemovalError> try_remove(PlacementId p_placement);

    // Test-only construction seam: an empty arrangement whose id allocator
    // starts at p_next_id. It cannot produce an invalid arrangement (an empty
    // arrangement is always valid, and ids stay monotonic from any start), so
    // it exists solely to exercise identifier exhaustion without allocating an
    // astronomical number of placements.
    static Arrangement testing_with_next_id(std::uint64_t p_next_id);

private:
    // The whole-footprint proof followed by identifier availability, in exactly
    // the order insertion applies them. Returns the rejecting error, or nothing
    // when the placement is currently insertable. It is reached only through
    // preview_insert, so preview and mutation cannot drift apart in outcome,
    // precedence, or conflict identity.
    std::optional<ArrangementError> reject_insertion(const Placement &p_placement) const;

    std::vector<Entry> entries_;
    std::uint64_t next_id_ = 0;
    bool exhausted_ = false;
};

} // namespace tiles
