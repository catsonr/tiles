#include "core/ArrangementRegion.h"

#include "core/geometry/Predicates.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace tiles {

namespace {

using Failure = ArrangementRegionInvariantFailure;
using FailureCode = ArrangementRegionInvariantFailureCode;
using RegionResult = Result<Region, ArrangementRegionError>;

// -1, 0, or +1 under the same lexicographic order lexicographically_less
// defines. Spelled as a comparison so sorting, grouping, and sequence
// comparison all read from one rule.
int compare_points(const Point &p_lhs, const Point &p_rhs) {
    if (p_lhs.x != p_rhs.x) {
        return p_lhs.x < p_rhs.x ? -1 : 1;
    }
    if (p_lhs.y != p_rhs.y) {
        return p_lhs.y < p_rhs.y ? -1 : 1;
    }
    return 0;
}

int compare_segments(const CanonicalSegment &p_lhs, const CanonicalSegment &p_rhs) {
    const int lesser = compare_points(p_lhs.lesser, p_rhs.lesser);
    if (lesser != 0) {
        return lesser;
    }
    return compare_points(p_lhs.greater, p_rhs.greater);
}

CanonicalSegment canonical_segment(const Point &p_start, const Point &p_end) {
    if (lexicographically_less(p_start, p_end)) {
        return CanonicalSegment { p_start, p_end };
    }
    return CanonicalSegment { p_end, p_start };
}

ArrangementRegionError expected_error(ArrangementRegionErrorCode p_code) {
    ArrangementRegionError error;
    error.code = p_code;
    return error;
}

ArrangementRegionError internal_error(Failure p_failure) {
    ArrangementRegionError error;
    error.code = ArrangementRegionErrorCode::internal_invariant_failure;
    error.invariant_failure = std::move(p_failure);
    return error;
}

Failure bare_failure(FailureCode p_code) {
    Failure failure;
    failure.code = p_code;
    return failure;
}

Failure point_failure(FailureCode p_code, const Point &p_point) {
    Failure failure = bare_failure(p_code);
    failure.point = p_point;
    return failure;
}

// One directed edge of one footprint, still carrying where it came from.
struct SourceEdge final {
    Point start;
    Point end;
    SourceEdgeRef ref;
};

// One directed piece of a source edge after exact subdivision.
struct AtomicSegment final {
    Point start;
    Point end;
    SourceEdgeRef ref;
};

// One atomic segment which survived cancellation, still in its source
// direction, so the coverage interior remains on its left.
struct RetainedSegment final {
    Point start;
    Point end;
};

// 1. Every directed footprint edge in arrangement storage order, then footprint
// polygon edge order. Each public footprint is canonical counterclockwise, so
// the filled interior lies to the left of every edge collected here.
std::vector<SourceEdge> collect_source_edges(const Arrangement &p_arrangement) {
    std::vector<SourceEdge> edges;
    const std::vector<Entry> &entries = p_arrangement.entries();
    for (std::size_t placement = 0; placement < entries.size(); ++placement) {
        const Polygon::Vertices &vertices =
            entries[placement].placement.footprint().vertices();
        const std::size_t n = vertices.size();
        for (std::size_t edge = 0; edge < n; ++edge) {
            edges.push_back(SourceEdge {
                vertices[edge],
                vertices[(edge + 1) % n],
                SourceEdgeRef { placement, edge },
            });
        }
    }
    return edges;
}

// 2. Compare every source-edge pair exactly, in lexicographic source-reference
// order, rejecting proper crossings and collecting the exact source vertices at
// which each edge must be broken.
//
// Both contact kinds reduce to one rule: an endpoint lying on the other closed
// segment is a split point of that other segment. No new coordinate is
// constructed, so nothing here can leave the lattice.
std::optional<ArrangementRegionError> collect_split_points(
    const std::vector<SourceEdge> &p_edges,
    std::vector<std::vector<Point>> &p_splits) {
    p_splits.assign(p_edges.size(), {});
    for (std::size_t i = 0; i < p_edges.size(); ++i) {
        p_splits[i].push_back(p_edges[i].start);
        p_splits[i].push_back(p_edges[i].end);
    }

    for (std::size_t i = 0; i < p_edges.size(); ++i) {
        for (std::size_t j = i + 1; j < p_edges.size(); ++j) {
            const SourceEdge &lhs = p_edges[i];
            const SourceEdge &rhs = p_edges[j];
            const SegmentRelation relation =
                classify_segments(lhs.start, lhs.end, rhs.start, rhs.end);

            if (relation == SegmentRelation::proper_crossing) {
                Failure failure =
                    bare_failure(FailureCode::unsupported_boundary_crossing);
                failure.crossing = BoundaryCrossing { lhs.ref, rhs.ref };
                return internal_error(std::move(failure));
            }
            if (relation == SegmentRelation::disjoint) {
                continue;
            }

            if (on_segment(rhs.start, lhs.start, lhs.end)) {
                p_splits[i].push_back(rhs.start);
            }
            if (on_segment(rhs.end, lhs.start, lhs.end)) {
                p_splits[i].push_back(rhs.end);
            }
            if (on_segment(lhs.start, rhs.start, rhs.end)) {
                p_splits[j].push_back(lhs.start);
            }
            if (on_segment(lhs.end, rhs.start, rhs.end)) {
                p_splits[j].push_back(lhs.end);
            }
        }
    }
    return std::nullopt;
}

// 3. Order one edge's split points along the directed edge and emit every
// nonzero consecutive directed piece.
//
// Every split point is collinear with the edge and inside its bounding box, so
// one raw scalar totally orders them: x for a non-vertical edge, y otherwise,
// reversed when the edge decreases on that axis. Equal keys therefore mean
// equal points, and no parameter is ever divided or interpolated.
std::vector<AtomicSegment> emit_atomic_segments(
    const std::vector<SourceEdge> &p_edges,
    std::vector<std::vector<Point>> &p_splits) {
    std::vector<AtomicSegment> segments;
    for (std::size_t i = 0; i < p_edges.size(); ++i) {
        const SourceEdge &edge = p_edges[i];
        const bool use_x = edge.start.x != edge.end.x;
        const auto key = [use_x](const Point &p_point) {
            return use_x ? p_point.x.raw() : p_point.y.raw();
        };
        const bool ascending = key(edge.start) < key(edge.end);

        std::vector<Point> &points = p_splits[i];
        std::sort(points.begin(), points.end(),
            [&key, ascending](const Point &p_lhs, const Point &p_rhs) {
                return ascending ? key(p_lhs) < key(p_rhs) : key(p_rhs) < key(p_lhs);
            });
        points.erase(std::unique(points.begin(), points.end()), points.end());

        for (std::size_t k = 0; k + 1 < points.size(); ++k) {
            if (points[k] != points[k + 1]) {
                segments.push_back(AtomicSegment { points[k], points[k + 1], edge.ref });
            }
        }
    }
    return segments;
}

// 4. Group atomic segments under their undirected key and keep only coverage
// boundary.
//
// One incidence is boundary. Two opposite-directed incidences are the shared
// internal segment between two footprints and cancel. Anything else contradicts
// pairwise interior disjointness.
std::optional<ArrangementRegionError> cancel_internal_segments(
    const std::vector<AtomicSegment> &p_segments,
    std::vector<RetainedSegment> &p_retained) {
    std::vector<std::size_t> order(p_segments.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    // Stable, so incidences of one key stay in production order — which is
    // source-edge order, the deterministic incidence order evidence reports.
    std::stable_sort(order.begin(), order.end(),
        [&p_segments](std::size_t p_lhs, std::size_t p_rhs) {
            const CanonicalSegment lhs =
                canonical_segment(p_segments[p_lhs].start, p_segments[p_lhs].end);
            const CanonicalSegment rhs =
                canonical_segment(p_segments[p_rhs].start, p_segments[p_rhs].end);
            return compare_segments(lhs, rhs) < 0;
        });

    p_retained.clear();
    std::size_t group_start = 0;
    while (group_start < order.size()) {
        const CanonicalSegment key = canonical_segment(
            p_segments[order[group_start]].start, p_segments[order[group_start]].end);
        std::size_t group_end = group_start + 1;
        while (group_end < order.size()) {
            const CanonicalSegment other = canonical_segment(
                p_segments[order[group_end]].start, p_segments[order[group_end]].end);
            if (compare_segments(key, other) != 0) {
                break;
            }
            ++group_end;
        }

        const std::size_t incidences = group_end - group_start;
        if (incidences == 1) {
            const AtomicSegment &only = p_segments[order[group_start]];
            p_retained.push_back(RetainedSegment { only.start, only.end });
        } else {
            const AtomicSegment &first = p_segments[order[group_start]];
            const AtomicSegment &second = p_segments[order[group_start + 1]];
            const bool opposed =
                incidences == 2 && first.start == second.end && first.end == second.start;
            if (!opposed) {
                std::vector<SourceEdgeRef> contributing;
                for (std::size_t k = group_start; k < group_end; ++k) {
                    contributing.push_back(p_segments[order[k]].ref);
                }
                Failure failure = bare_failure(FailureCode::invalid_segment_multiplicity);
                failure.multiplicity = SegmentMultiplicity { key, std::move(contributing) };
                return internal_error(std::move(failure));
            }
        }

        group_start = group_end;
    }
    return std::nullopt;
}

// The complete sorted set of retained endpoints. Used both to visit boundary
// vertices in lexicographic order and to index their degrees.
std::vector<Point> boundary_vertices(const std::vector<RetainedSegment> &p_retained) {
    std::vector<Point> points;
    points.reserve(p_retained.size() * 2);
    for (const RetainedSegment &segment : p_retained) {
        points.push_back(segment.start);
        points.push_back(segment.end);
    }
    std::sort(points.begin(), points.end(),
        [](const Point &p_lhs, const Point &p_rhs) {
            return lexicographically_less(p_lhs, p_rhs);
        });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    return points;
}

std::size_t vertex_index(const std::vector<Point> &p_points, const Point &p_point) {
    const auto found = std::lower_bound(p_points.begin(), p_points.end(), p_point,
        [](const Point &p_lhs, const Point &p_rhs) {
            return lexicographically_less(p_lhs, p_rhs);
        });
    return static_cast<std::size_t>(found - p_points.begin());
}

// 5. Prove the retained boundary is a manifold directed graph. Indegree and
// outdegree one everywhere is exactly what rejects point connections, branches,
// pinches, and ambiguous continuation, and it is what makes the walk in step 6
// deterministic.
std::optional<ArrangementRegionError> prove_manifold_boundary(
    const std::vector<RetainedSegment> &p_retained,
    const std::vector<Point> &p_vertices) {
    std::vector<std::size_t> indegree(p_vertices.size(), 0);
    std::vector<std::size_t> outdegree(p_vertices.size(), 0);
    for (const RetainedSegment &segment : p_retained) {
        ++outdegree[vertex_index(p_vertices, segment.start)];
        ++indegree[vertex_index(p_vertices, segment.end)];
    }

    for (std::size_t i = 0; i < p_vertices.size(); ++i) {
        if (indegree[i] != 1 || outdegree[i] != 1) {
            ArrangementRegionError error =
                expected_error(ArrangementRegionErrorCode::nonmanifold_boundary_vertex);
            error.nonmanifold_vertex =
                NonmanifoldVertex { p_vertices[i], indegree[i], outdegree[i] };
            return error;
        }
    }
    return std::nullopt;
}

// 6. Consume every retained segment into closed directed cycles. Discovery
// order here is the deterministic cycle index later failures report.
std::optional<ArrangementRegionError> stitch_boundary_cycles(
    const std::vector<RetainedSegment> &p_retained,
    const std::vector<Point> &p_vertices,
    std::vector<std::vector<Point>> &p_cycles) {
    // Outgoing lookup by start vertex. Outdegree one is already proven, so each
    // slot is filled exactly once and continuation is never a choice.
    std::vector<std::size_t> outgoing(p_vertices.size(), p_retained.size());
    for (std::size_t i = 0; i < p_retained.size(); ++i) {
        outgoing[vertex_index(p_vertices, p_retained[i].start)] = i;
    }

    std::vector<bool> used(p_retained.size(), false);
    std::vector<bool> on_walk(p_vertices.size(), false);
    p_cycles.clear();

    while (true) {
        std::size_t seed = p_retained.size();
        for (std::size_t i = 0; i < p_retained.size(); ++i) {
            if (used[i]) {
                continue;
            }
            if (seed == p_retained.size()) {
                seed = i;
                continue;
            }
            const int start = compare_points(p_retained[i].start, p_retained[seed].start);
            if (start < 0
                || (start == 0
                    && compare_points(p_retained[i].end, p_retained[seed].end) < 0)) {
                seed = i;
            }
        }
        if (seed == p_retained.size()) {
            break;
        }

        const Point origin = p_retained[seed].start;
        std::vector<Point> cycle;
        // Which vertices this one walk has already stood on. Cleared when the
        // walk ends, so a vertex belonging to an earlier cycle is diagnosed as
        // missing continuation rather than as a repeat.
        std::vector<std::size_t> walked;
        std::size_t current = seed;
        std::optional<ArrangementRegionError> rejection;
        while (true) {
            used[current] = true;
            const std::size_t start = vertex_index(p_vertices, p_retained[current].start);
            on_walk[start] = true;
            walked.push_back(start);
            cycle.push_back(p_retained[current].start);

            const Point next_point = p_retained[current].end;
            if (next_point == origin) {
                break;
            }
            const std::size_t next_vertex = vertex_index(p_vertices, next_point);
            if (on_walk[next_vertex]) {
                rejection = internal_error(
                    point_failure(FailureCode::repeated_boundary_vertex, next_point));
                break;
            }
            const std::size_t next = outgoing[next_vertex];
            if (next == p_retained.size() || used[next]) {
                rejection = internal_error(
                    point_failure(FailureCode::open_boundary_walk, next_point));
                break;
            }
            current = next;
        }
        for (const std::size_t vertex : walked) {
            on_walk[vertex] = false;
        }
        if (rejection) {
            return rejection;
        }
        p_cycles.push_back(std::move(cycle));
    }
    return std::nullopt;
}

// 7. Remove every vertex which merely subdivides a straight run, cyclically, to
// a fixed point. A turn is never removed, and a collinear vertex outside the
// combined segment would be a fold rather than a subdivision, so it stays and is
// left for polygon validation to reject.
void simplify_collinear_subdivision(std::vector<Point> &p_cycle) {
    bool changed = true;
    while (changed && p_cycle.size() > 3) {
        changed = false;
        const std::size_t n = p_cycle.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Point &previous = p_cycle[(i + n - 1) % n];
            const Point &vertex = p_cycle[i];
            const Point &next = p_cycle[(i + 1) % n];
            if (orientation(previous, vertex, next) != Turn::collinear) {
                continue;
            }
            if (vertex == previous || vertex == next
                || !on_segment(vertex, previous, next)) {
                continue;
            }
            p_cycle.erase(p_cycle.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
            break;
        }
    }
}

const Point &canonical_first_point(const std::vector<Point> &p_cycle) {
    const Point *least = &p_cycle.front();
    for (const Point &point : p_cycle) {
        if (lexicographically_less(point, *least)) {
            least = &point;
        }
    }
    return *least;
}

// Complete canonical boundary comparison: corresponding vertices
// lexicographically, and sequence length only after the common prefix. Hole
// order is then a property of the derived geometry alone, never of arrangement
// storage order.
bool boundary_precedes(const Polygon &p_lhs, const Polygon &p_rhs) {
    const Polygon::Vertices &lhs = p_lhs.vertices();
    const Polygon::Vertices &rhs = p_rhs.vertices();
    const std::size_t common = std::min(lhs.size(), rhs.size());
    for (std::size_t i = 0; i < common; ++i) {
        const int comparison = compare_points(lhs[i], rhs[i]);
        if (comparison != 0) {
            return comparison < 0;
        }
    }
    return lhs.size() < rhs.size();
}

// 11. The independent proof. A finite collection of interior-disjoint
// counterclockwise footprints covers exactly its own summed area, so the
// derived region must report the same exact doubled total.
Int256 summed_footprint_doubled_area(const Arrangement &p_arrangement) {
    Int256 total;
    for (const Entry &entry : p_arrangement.entries()) {
        total = total + signed_double_area(entry.placement.footprint().vertices());
    }
    return total;
}

} // namespace

Result<Region, ArrangementRegionError> region_from_arrangement(
    const Arrangement &p_arrangement) {
    if (p_arrangement.entries().empty()) {
        return RegionResult::failure(
            expected_error(ArrangementRegionErrorCode::empty_arrangement));
    }

    const std::vector<SourceEdge> edges = collect_source_edges(p_arrangement);

    std::vector<std::vector<Point>> splits;
    if (auto rejection = collect_split_points(edges, splits)) {
        return RegionResult::failure(std::move(*rejection));
    }

    const std::vector<AtomicSegment> atomic = emit_atomic_segments(edges, splits);

    std::vector<RetainedSegment> retained;
    if (auto rejection = cancel_internal_segments(atomic, retained)) {
        return RegionResult::failure(std::move(*rejection));
    }

    const std::vector<Point> vertices = boundary_vertices(retained);
    if (auto rejection = prove_manifold_boundary(retained, vertices)) {
        return RegionResult::failure(std::move(*rejection));
    }

    std::vector<std::vector<Point>> cycles;
    if (auto rejection = stitch_boundary_cycles(retained, vertices, cycles)) {
        return RegionResult::failure(std::move(*rejection));
    }

    for (std::vector<Point> &cycle : cycles) {
        simplify_collinear_subdivision(cycle);
    }

    // 8. Exact signed doubled area classifies each cycle before any polygon is
    // built: positive is the outer coverage boundary, negative is a hole.
    std::vector<int> signs(cycles.size(), 0);
    for (std::size_t i = 0; i < cycles.size(); ++i) {
        signs[i] = signed_double_area(cycles[i]).sign();
        if (signs[i] == 0) {
            Failure failure = bare_failure(FailureCode::zero_area_boundary);
            failure.cycle = i;
            return RegionResult::failure(internal_error(std::move(failure)));
        }
    }

    std::vector<std::size_t> positive;
    for (std::size_t i = 0; i < cycles.size(); ++i) {
        if (signs[i] > 0) {
            positive.push_back(i);
        }
    }
    if (positive.empty()) {
        return RegionResult::failure(
            internal_error(bare_failure(FailureCode::no_outer_boundary)));
    }
    if (positive.size() > 1) {
        ArrangementRegionError error =
            expected_error(ArrangementRegionErrorCode::disconnected_coverage);
        for (const std::size_t index : positive) {
            error.component_points.push_back(canonical_first_point(cycles[index]));
        }
        std::sort(error.component_points.begin(), error.component_points.end(),
            [](const Point &p_lhs, const Point &p_rhs) {
                return lexicographically_less(p_lhs, p_rhs);
            });
        return RegionResult::failure(std::move(error));
    }

    // 9. Every cycle through the checked polygon factory, in discovery order,
    // with each hole reversed into counterclockwise winding first.
    std::optional<Polygon> outer;
    std::vector<Polygon> holes;
    for (std::size_t i = 0; i < cycles.size(); ++i) {
        std::vector<Point> boundary = cycles[i];
        if (signs[i] < 0) {
            std::reverse(boundary.begin(), boundary.end());
        }
        auto polygon = Polygon::make(std::move(boundary));
        if (!polygon.has_value()) {
            Failure failure = bare_failure(FailureCode::polygon_construction_failed);
            failure.polygon_failure = PolygonConstructionFailure { i, polygon.error() };
            return RegionResult::failure(internal_error(std::move(failure)));
        }
        if (signs[i] > 0) {
            outer = std::move(polygon).value();
        } else {
            holes.push_back(std::move(polygon).value());
        }
    }
    std::sort(holes.begin(), holes.end(), boundary_precedes);

    // 10.
    auto region = Region::make(std::move(outer).value(), std::move(holes));
    if (!region.has_value()) {
        Failure failure = bare_failure(FailureCode::region_construction_failed);
        failure.region_error = region.error();
        return RegionResult::failure(internal_error(std::move(failure)));
    }

    // 11.
    const Int256 footprint_area = summed_footprint_doubled_area(p_arrangement);
    if (footprint_area != region.value().doubled_area()) {
        Failure failure = bare_failure(FailureCode::area_mismatch);
        failure.area_mismatch =
            AreaMismatch { footprint_area, region.value().doubled_area() };
        return RegionResult::failure(internal_error(std::move(failure)));
    }

    return RegionResult::success(std::move(region).value());
}

} // namespace tiles
