#pragma once

#include "core/Result.h"
#include "core/geometry/Point.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace tiles::content {

// One occupied closed unit cell of the shipped-content authoring grid. Cell
// (x, y) is the closed square [x, x + 1] x [y, y + 1] in whole game units.
//
// This is a compact source description for shipped polyomino definitions only.
// It is not a runtime value: no Prototile, Polygon, Placement, or Region ever
// retains cells, and nothing rasterizes geometry back into them.
struct Cell final {
    std::int64_t x;
    std::int64_t y;

    friend constexpr bool operator==(Cell p_lhs, Cell p_rhs) {
        return p_lhs.x == p_rhs.x && p_lhs.y == p_rhs.y;
    }

    friend constexpr bool operator!=(Cell p_lhs, Cell p_rhs) {
        return !(p_lhs == p_rhs);
    }

    // Lexicographic order by (x, y), used only to make the traced boundary and
    // every rejection deterministic.
    friend constexpr bool operator<(Cell p_lhs, Cell p_rhs) {
        if (p_lhs.x != p_rhs.x) {
            return p_lhs.x < p_rhs.x;
        }
        return p_lhs.y < p_rhs.y;
    }
};

enum class CellBoundaryErrorCode {
    empty,
    duplicate_cell,
    coordinate_out_of_range,
    disconnected,
    branching_vertex,
    multiple_boundary_cycles,
};

// A typed failure from tracing one occupied-cell set. `cell` is populated
// exactly when the defect belongs to one named cell: a duplicate entry, or a
// cell whose corners leave the representable lattice.
struct CellBoundaryError final {
    CellBoundaryErrorCode code;
    std::optional<Cell> cell;
};

// Trace the exterior boundary of one finite occupied-cell set into a single
// simple ring of exact whole-game-unit points, in counterclockwise order and
// with the closing vertex omitted.
//
// The set is proven nonempty, duplicate-free, representable, and edge-connected;
// then each cell contributes its four directed unit boundary edges, every edge
// whose opposite belongs to a neighbouring occupied cell is cancelled, and the
// retained exterior edges are required to form exactly one directed cycle
// visiting each retained edge once. Any branch, or any second cycle such as the
// inner boundary of a hole-bearing set, is rejected rather than silently
// dropped.
//
// Only direction-changing corners are emitted: a straight run of collinear unit
// edges is one polygon edge, which is what the polygon validator admits. The
// returned ring is a vertex sequence, not a Polygon: the caller passes it
// through the ordinary Polygon::make so shipped content is validated and
// canonicalized by exactly the same authority as every other polygon.
Result<std::vector<Point>, CellBoundaryError> trace_cell_boundary(
    const std::vector<Cell> &p_cells);

// Build one exact lattice coordinate from a whole game-unit value, rejecting
// any value whose raw representation would leave signed 64-bit storage. Shipped
// content never converts a floating value into a coordinate.
Result<Coordinate, CellBoundaryError> whole_game_units(std::int64_t p_units);

} // namespace tiles::content
