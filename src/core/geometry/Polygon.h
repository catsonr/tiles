#pragma once

#include "core/Result.h"
#include "core/geometry/Point.h"
#include "core/geometry/Triangle.h"

#include <vector>

namespace tiles {

enum class PolygonError {
    too_few_vertices,
    repeated_closing_vertex,
    zero_length_edge,
    repeated_vertex,
    redundant_collinear_vertex,
    adjacent_edge_overlap,
    self_intersection,
    zero_area,
    triangulation_failed,
};

// A closed, filled, hole-free simple polygon together with a certified
// triangulation of its interior. There is no public unchecked constructor:
// only make() can produce one, and only from a vertex sequence proven simple.
//
// Stored vertices use counterclockwise winding and begin at the
// lexicographically smallest vertex. The closing edge back to vertex zero is
// implicit and never repeated. An n-vertex polygon owns exactly n - 2
// nondegenerate triangles; the triangulation is derived evidence, not identity.
class Polygon final {
public:
    using Vertices = std::vector<Point>;
    using Triangulation = std::vector<Triangle>;

    // Validate, canonicalize, and triangulate an owned sequence of
    // already-quantized points. Returns the complete polygon or one error.
    static Result<Polygon, PolygonError> make(Vertices p_vertices);

    const Vertices &vertices() const {
        return vertices_;
    }

    const Triangulation &triangulation() const {
        return triangulation_;
    }

private:
    Vertices vertices_;
    Triangulation triangulation_;

    explicit Polygon(Vertices p_vertices, Triangulation p_triangulation);
};

// Exact boundary identity: two polygons share a boundary exactly when their
// canonical vertex sequences are equal. Triangulation is derived evidence and is
// deliberately not part of this comparison. It is spelled as a named operation,
// not operator==, so it is never mistaken for prototile identity or congruence.
bool same_boundary(const Polygon &p_lhs, const Polygon &p_rhs);

// Translate p_polygon so its canonical first (lexicographically smallest) vertex
// sits at the local origin, rebuilding through make(). Because translation
// preserves simplicity, winding, the lexicographic minimum, and triangulation
// topology, the rebuild always succeeds; only a component leaving the lattice is
// an error. Shared by Prototile::make and the lattice-orientation compiler so
// the single checked normalization path is never duplicated.
Result<Polygon, ArithmeticError> translation_normalize(const Polygon &p_polygon);

} // namespace tiles
