#include "core/geometry/Polygon.h"

#include "core/geometry/Predicates.h"

#include <algorithm>
#include <cassert>
#include <optional>

namespace tiles {

namespace {

// Whether two edges of the vertex ring share a vertex. Edge i connects
// vertices i and i+1 (mod n). Adjacent edges legitimately share their common
// endpoint, so only nonadjacent pairs are tested for self-intersection.
bool edges_share_vertex(std::size_t p_i, std::size_t p_j, std::size_t p_n) {
    const std::size_t i_next = (p_i + 1) % p_n;
    const std::size_t j_next = (p_j + 1) % p_n;
    return p_i == p_j || p_i == j_next || i_next == p_j || i_next == j_next;
}

// Steps 1-7 of validation, in the order that gives multiply-invalid input a
// stable diagnosis. Returns the first violated rule, or nothing if simple.
std::optional<PolygonError> find_violation(const Polygon::Vertices &p_v) {
    const std::size_t n = p_v.size();

    // 1. at least three submitted vertices.
    if (n < 3) {
        return PolygonError::too_few_vertices;
    }

    // 2. reject a final vertex equal to the first (redundant closing vertex).
    if (p_v.front() == p_v.back()) {
        return PolygonError::repeated_closing_vertex;
    }

    // 3. reject equal adjacent vertices, including the implicit closing edge.
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        if (p_v[i] == p_v[j]) {
            return PolygonError::zero_length_edge;
        }
    }

    // 4. reject any repeated nonadjacent vertex. Adjacent duplicates are already
    //    excluded, so any remaining equal pair is nonadjacent.
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (p_v[i] == p_v[j]) {
                return PolygonError::repeated_vertex;
            }
        }
    }

    // 5. classify every consecutive triple.
    for (std::size_t i = 0; i < n; ++i) {
        const Point &prev = p_v[(i + n - 1) % n];
        const Point &cur = p_v[i];
        const Point &next = p_v[(i + 1) % n];
        if (orientation(prev, cur, next) == Turn::collinear) {
            // The middle vertex strictly inside its neighbors' segment is
            // redundant; otherwise the two incident edges fold back and overlap.
            if (on_segment(cur, prev, next)) {
                return PolygonError::redundant_collinear_vertex;
            }
            return PolygonError::adjacent_edge_overlap;
        }
    }

    // 6. compare every nonadjacent edge pair.
    for (std::size_t i = 0; i < n; ++i) {
        const Point &a = p_v[i];
        const Point &b = p_v[(i + 1) % n];
        for (std::size_t j = i + 1; j < n; ++j) {
            if (edges_share_vertex(i, j, n)) {
                continue;
            }
            const Point &c = p_v[j];
            const Point &d = p_v[(j + 1) % n];
            if (classify_segments(a, b, c, d) != SegmentRelation::disjoint) {
                return PolygonError::self_intersection;
            }
        }
    }

    // 7. exact signed doubled area; zero means degenerate.
    if (signed_double_area(p_v).is_zero()) {
        return PolygonError::zero_area;
    }

    return std::nullopt;
}

// Reverse clockwise input to counterclockwise winding, then rotate so the
// lexicographically smallest vertex is index zero. The canonical first vertex
// is unique because there are no repeated vertices.
void canonicalize(Polygon::Vertices &p_v) {
    if (signed_double_area(p_v).sign() < 0) {
        std::reverse(p_v.begin(), p_v.end());
    }

    std::size_t smallest = 0;
    for (std::size_t i = 1; i < p_v.size(); ++i) {
        if (lexicographically_less(p_v[i], p_v[smallest])) {
            smallest = i;
        }
    }
    std::rotate(p_v.begin(), p_v.begin() + smallest, p_v.end());
}

// p lies inside or on the counterclockwise triangle (t0, t1, t2).
bool point_in_triangle(
    const Point &p_p, const Point &p_t0, const Point &p_t1, const Point &p_t2) {
    return orientation(p_t0, p_t1, p_p) != Turn::clockwise
        && orientation(p_t1, p_t2, p_p) != Turn::clockwise
        && orientation(p_t2, p_t0, p_p) != Turn::clockwise;
}

// Deterministic ear clipping over a canonical counterclockwise boundary. Scans
// active vertices in canonical order and clips the first valid ear until one
// triangle remains. Yields exactly n - 2 counterclockwise triangles.
std::optional<Polygon::Triangulation> triangulate(const Polygon::Vertices &p_v) {
    const std::size_t n = p_v.size();
    Polygon::Triangulation triangles;
    triangles.reserve(n - 2);

    std::vector<std::size_t> active(n);
    for (std::size_t i = 0; i < n; ++i) {
        active[i] = i;
    }

    while (active.size() > 3) {
        const std::size_t m = active.size();
        bool clipped = false;

        for (std::size_t i = 0; i < m; ++i) {
            const std::size_t prev = active[(i + m - 1) % m];
            const std::size_t cur = active[i];
            const std::size_t next = active[(i + 1) % m];

            if (orientation(p_v[prev], p_v[cur], p_v[next]) != Turn::counterclockwise) {
                continue;
            }

            bool contains_other = false;
            for (std::size_t k = 0; k < m; ++k) {
                const std::size_t idx = active[k];
                if (idx == prev || idx == cur || idx == next) {
                    continue;
                }
                if (point_in_triangle(p_v[idx], p_v[prev], p_v[cur], p_v[next])) {
                    contains_other = true;
                    break;
                }
            }
            if (contains_other) {
                continue;
            }

            triangles.push_back(Triangle { { p_v[prev], p_v[cur], p_v[next] } });
            active.erase(active.begin() + i);
            clipped = true;
            break;
        }

        if (!clipped) {
            return std::nullopt;
        }
    }

    // The final three vertices form the last triangle. A validated simple
    // polygon leaves them counterclockwise; anything else is a defect.
    if (orientation(p_v[active[0]], p_v[active[1]], p_v[active[2]])
        != Turn::counterclockwise) {
        return std::nullopt;
    }
    triangles.push_back(Triangle { { p_v[active[0]], p_v[active[1]], p_v[active[2]] } });

    return triangles;
}

} // namespace

Polygon::Polygon(Vertices p_vertices, Triangulation p_triangulation) :
    vertices_(std::move(p_vertices)),
    triangulation_(std::move(p_triangulation)) {}

Result<Polygon, PolygonError> Polygon::make(Vertices p_vertices) {
    if (const auto violation = find_violation(p_vertices)) {
        return Result<Polygon, PolygonError>::failure(*violation);
    }

    canonicalize(p_vertices);

    auto triangulation = triangulate(p_vertices);
    if (!triangulation) {
        return Result<Polygon, PolygonError>::failure(PolygonError::triangulation_failed);
    }

    return Result<Polygon, PolygonError>::success(
        Polygon(std::move(p_vertices), std::move(*triangulation)));
}

bool same_boundary(const Polygon &p_lhs, const Polygon &p_rhs) {
    return p_lhs.vertices() == p_rhs.vertices();
}

Result<Polygon, ArithmeticError> translation_normalize(const Polygon &p_polygon) {
    const Point origin = p_polygon.vertices().front();

    Polygon::Vertices translated;
    translated.reserve(p_polygon.vertices().size());
    for (const Point &vertex : p_polygon.vertices()) {
        auto shifted = checked_subtract(vertex, origin);
        if (!shifted.has_value()) {
            return Result<Polygon, ArithmeticError>::failure(shifted.error());
        }
        translated.push_back(shifted.value());
    }

    // Reconstruct through the validated factory rather than reaching into an
    // incomplete polygon. The subtraction cannot break simplicity, the
    // lexicographic minimum, winding, or triangulation topology, so this rebuild
    // always succeeds for an already-valid polygon.
    auto rebuilt = Polygon::make(std::move(translated));
    assert(rebuilt.has_value());
    return Result<Polygon, ArithmeticError>::success(std::move(rebuilt).value());
}

} // namespace tiles
