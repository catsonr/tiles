#pragma once

#include "core/Arrangement.h"
#include "core/Region.h"
#include "core/Result.h"
#include "core/geometry/ExactInteger.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace tiles {

// Where one directed boundary segment came from: a position in arrangement
// storage order paired with an edge index of that placement's footprint
// polygon. Edge k of an n-vertex footprint runs from vertex k to vertex
// (k + 1) mod n.
//
// It is deliberately not a PlacementId, a pointer, or a rendered coordinate.
// This operation reads a snapshot of one arrangement and its evidence must stay
// meaningful as a plain value after that arrangement is gone.
struct SourceEdgeRef final {
    std::size_t placement;
    std::size_t edge;

    friend bool operator==(SourceEdgeRef p_lhs, SourceEdgeRef p_rhs) {
        return p_lhs.placement == p_rhs.placement && p_lhs.edge == p_rhs.edge;
    }

    friend bool operator!=(SourceEdgeRef p_lhs, SourceEdgeRef p_rhs) {
        return !(p_lhs == p_rhs);
    }
};

// One undirected atomic boundary segment named by its two endpoints in
// lexicographic order, so the same segment has one name whichever direction any
// contributor traversed it.
struct CanonicalSegment final {
    Point lesser;
    Point greater;

    friend bool operator==(CanonicalSegment p_lhs, CanonicalSegment p_rhs) {
        return p_lhs.lesser == p_rhs.lesser && p_lhs.greater == p_rhs.greater;
    }

    friend bool operator!=(CanonicalSegment p_lhs, CanonicalSegment p_rhs) {
        return !(p_lhs == p_rhs);
    }
};

// The two source edges of a boundary crossing, in the pair order the classifier
// reached them.
struct BoundaryCrossing final {
    SourceEdgeRef first;
    SourceEdgeRef second;
};

// One atomic segment together with every source edge that contributed an
// incidence to it, in incidence order.
struct SegmentMultiplicity final {
    CanonicalSegment segment;
    std::vector<SourceEdgeRef> contributing_edges;
};

// Which boundary cycle failed polygon construction, and how.
struct PolygonConstructionFailure final {
    std::size_t cycle;
    PolygonError error;
};

// The two exact totals that were required to be equal.
struct AreaMismatch final {
    Int256 footprint_doubled_area;
    Int256 region_doubled_area;
};

// The offending boundary vertex together with its exact retained-boundary
// degrees. A coverage boundary is manifold exactly when every retained vertex
// has indegree one and outdegree one.
struct NonmanifoldVertex final {
    Point point;
    std::size_t indegree;
    std::size_t outdegree;
};

// A geometric consequence of the public arrangement invariant which did not
// hold. Reaching one of these means an implementation defect, a violated
// upstream invariant, or an incorrect theorem assumption — never an ordinary
// authored arrangement.
enum class ArrangementRegionInvariantFailureCode {
    unsupported_boundary_crossing,
    invalid_segment_multiplicity,
    open_boundary_walk,
    repeated_boundary_vertex,
    zero_area_boundary,
    no_outer_boundary,
    polygon_construction_failed,
    region_construction_failed,
    area_mismatch,
};

// Exactly the provenance its code selects is populated; every other alternative
// is empty. Each payload is whole or absent, so no failure can wear another
// one's evidence.
struct ArrangementRegionInvariantFailure final {
    ArrangementRegionInvariantFailureCode code;
    // unsupported_boundary_crossing
    std::optional<BoundaryCrossing> crossing;
    // invalid_segment_multiplicity
    std::optional<SegmentMultiplicity> multiplicity;
    // open_boundary_walk, repeated_boundary_vertex
    std::optional<Point> point;
    // zero_area_boundary
    std::optional<std::size_t> cycle;
    // polygon_construction_failed
    std::optional<PolygonConstructionFailure> polygon_failure;
    // region_construction_failed
    std::optional<RegionError> region_error;
    // area_mismatch
    std::optional<AreaMismatch> area_mismatch;
};

enum class ArrangementRegionErrorCode {
    empty_arrangement,
    nonmanifold_boundary_vertex,
    disconnected_coverage,
    internal_invariant_failure,
};

// Why one arrangement's coverage is not one region. The first three codes are
// expected properties of otherwise valid authored arrangements and carry no
// blame; the fourth reports a broken proof.
struct ArrangementRegionError final {
    ArrangementRegionErrorCode code;
    // nonmanifold_boundary_vertex
    std::optional<NonmanifoldVertex> nonmanifold_vertex;
    // disconnected_coverage: the canonical first point of every positive
    // boundary component, in complete lexicographic order. At least two.
    std::vector<Point> component_points;
    // internal_invariant_failure
    std::optional<ArrangementRegionInvariantFailure> invariant_failure;
};

// Synthesize the canonical exact region covered by one arrangement.
//
// Every footprint is a checked simple polygon and every pair of footprint
// interiors is disjoint, so the coverage boundary is exactly the set of atomic
// directed footprint segments which no second footprint traverses in the
// opposite direction. This collects those segments without floating point,
// division, interpolation, rounding, snapping, or tolerance: a legal contact
// already expresses every breakpoint it needs as a source vertex, so subdivision
// only ever reuses coordinates the arrangement already contains.
//
// The result is one connected region with zero or more holes, or typed evidence
// that this coverage cannot be one: it is empty, its boundary is nonmanifold
// (a point connection or a pinch), or it has more than one positive component.
//
// This is a pure derivation, not a comparison: it does not accept an
// independently authored region claim. It publishes nothing partial, and the
// returned value and every error payload own their geometry outright.
Result<Region, ArrangementRegionError> region_from_arrangement(
    const Arrangement &p_arrangement);

} // namespace tiles
