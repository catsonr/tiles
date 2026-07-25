#include "content/CellBoundary.h"

#include "core/geometry/Coordinate.h"

#include <cstddef>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace tiles::content {

namespace {

// A boundary lattice point in whole game units. Cell corners are integers, so
// the trace runs entirely in this small integer space and only the accepted
// corners are converted into exact coordinates.
struct Corner final {
    std::int64_t x;
    std::int64_t y;

    friend bool operator==(Corner p_lhs, Corner p_rhs) {
        return p_lhs.x == p_rhs.x && p_lhs.y == p_rhs.y;
    }

    friend bool operator<(Corner p_lhs, Corner p_rhs) {
        if (p_lhs.x != p_rhs.x) {
            return p_lhs.x < p_rhs.x;
        }
        return p_lhs.y < p_rhs.y;
    }
};

// One game unit is Coordinate::SCALE raw units, so a whole-unit value is
// representable exactly when its product with SCALE stays in signed 64-bit
// storage. SCALE is 2^48, leaving 15 magnitude bits plus sign.
constexpr std::int64_t MAX_WHOLE_UNITS = 32767;
constexpr std::int64_t MIN_WHOLE_UNITS = -32768;

CellBoundaryError plain_failure(CellBoundaryErrorCode p_code) {
    CellBoundaryError error {};
    error.code = p_code;
    return error;
}

CellBoundaryError cell_failure(CellBoundaryErrorCode p_code, Cell p_cell) {
    CellBoundaryError error {};
    error.code = p_code;
    error.cell = p_cell;
    return error;
}

// Whether both of a cell's spans stay inside the representable whole-unit
// range. The far corners are x + 1 and y + 1, so the upper bound is checked
// before the increment rather than after it.
bool cell_is_representable(Cell p_cell) {
    return p_cell.x >= MIN_WHOLE_UNITS && p_cell.x < MAX_WHOLE_UNITS
        && p_cell.y >= MIN_WHOLE_UNITS && p_cell.y < MAX_WHOLE_UNITS;
}

// Every occupied cell reachable from the first one by shared complete edges.
// Diagonal contact deliberately does not connect: two cells meeting only at a
// corner are two components, not one polyomino.
bool is_edge_connected(const std::set<Cell> &p_cells) {
    std::set<Cell> reached;
    std::vector<Cell> frontier;

    const Cell start = *p_cells.begin();
    reached.insert(start);
    frontier.push_back(start);

    while (!frontier.empty()) {
        const Cell cell = frontier.back();
        frontier.pop_back();

        const Cell neighbours[4] = {
            Cell { cell.x - 1, cell.y },
            Cell { cell.x + 1, cell.y },
            Cell { cell.x, cell.y - 1 },
            Cell { cell.x, cell.y + 1 },
        };
        for (const Cell &neighbour : neighbours) {
            if (p_cells.count(neighbour) == 0 || reached.count(neighbour) != 0) {
                continue;
            }
            reached.insert(neighbour);
            frontier.push_back(neighbour);
        }
    }

    return reached.size() == p_cells.size();
}

} // namespace

Result<Coordinate, CellBoundaryError> whole_game_units(std::int64_t p_units) {
    if (p_units < MIN_WHOLE_UNITS || p_units > MAX_WHOLE_UNITS) {
        return Result<Coordinate, CellBoundaryError>::failure(
            plain_failure(CellBoundaryErrorCode::coordinate_out_of_range));
    }
    return Result<Coordinate, CellBoundaryError>::success(
        Coordinate::from_raw(p_units * Coordinate::SCALE));
}

Result<std::vector<Point>, CellBoundaryError> trace_cell_boundary(
    const std::vector<Cell> &p_cells) {
    using Failure = Result<std::vector<Point>, CellBoundaryError>;

    if (p_cells.empty()) {
        return Failure::failure(plain_failure(CellBoundaryErrorCode::empty));
    }

    std::set<Cell> occupied;
    for (const Cell &cell : p_cells) {
        if (!cell_is_representable(cell)) {
            return Failure::failure(
                cell_failure(CellBoundaryErrorCode::coordinate_out_of_range, cell));
        }
        if (!occupied.insert(cell).second) {
            return Failure::failure(
                cell_failure(CellBoundaryErrorCode::duplicate_cell, cell));
        }
    }

    if (!is_edge_connected(occupied)) {
        return Failure::failure(plain_failure(CellBoundaryErrorCode::disconnected));
    }

    // Each cell contributes its four unit boundary edges in counterclockwise
    // order, so a retained exterior edge already walks the filled area on its
    // left. An interior edge is exactly one whose reverse belongs to the
    // neighbouring occupied cell, so the two cancel.
    std::set<std::pair<Corner, Corner>> directed;
    for (const Cell &cell : occupied) {
        const Corner a { cell.x, cell.y };
        const Corner b { cell.x + 1, cell.y };
        const Corner c { cell.x + 1, cell.y + 1 };
        const Corner d { cell.x, cell.y + 1 };
        directed.insert({ a, b });
        directed.insert({ b, c });
        directed.insert({ c, d });
        directed.insert({ d, a });
    }

    std::map<Corner, Corner> next;
    std::set<Corner> has_incoming;
    std::size_t retained = 0;
    for (const auto &edge : directed) {
        if (directed.count({ edge.second, edge.first }) != 0) {
            continue;
        }
        // Exactly one outgoing and one incoming retained edge per boundary
        // corner. A second one is a pinch: two cells meeting only diagonally
        // while the boundary passes through that corner twice.
        if (!next.insert({ edge.first, edge.second }).second) {
            return Failure::failure(
                plain_failure(CellBoundaryErrorCode::branching_vertex));
        }
        if (!has_incoming.insert(edge.second).second) {
            return Failure::failure(
                plain_failure(CellBoundaryErrorCode::branching_vertex));
        }
        ++retained;
    }

    // A nonempty occupied set always retains at least the four edges of one
    // cell, so this cannot be reached for admitted input; it is guarded rather
    // than assumed so the traversal below never starts from nothing.
    if (retained == 0) {
        return Failure::failure(
            plain_failure(CellBoundaryErrorCode::multiple_boundary_cycles));
    }

    // The lexicographically smallest boundary corner belongs to the outer
    // boundary of any occupied set, so the traversal starts on the ring the
    // shipped definition means.
    const Corner start = next.begin()->first;
    std::vector<Corner> cycle;
    cycle.reserve(retained);

    Corner current = start;
    do {
        cycle.push_back(current);
        const auto step = next.find(current);
        if (step == next.end()) {
            return Failure::failure(
                plain_failure(CellBoundaryErrorCode::multiple_boundary_cycles));
        }
        current = step->second;
        if (cycle.size() > retained) {
            return Failure::failure(
                plain_failure(CellBoundaryErrorCode::multiple_boundary_cycles));
        }
    } while (!(current == start));

    // A retained edge left unvisited means the boundary has a second cycle:
    // the occupied set encloses a hole, which shipped prototile content forbids.
    if (cycle.size() != retained) {
        return Failure::failure(
            plain_failure(CellBoundaryErrorCode::multiple_boundary_cycles));
    }

    // Only direction changes are corners. A straight run of collinear unit
    // edges is one polygon edge; emitting its interior points would produce a
    // redundant collinear vertex, which the polygon validator rightly rejects.
    std::vector<Point> ring;
    ring.reserve(cycle.size());
    for (std::size_t i = 0; i < cycle.size(); ++i) {
        const Corner &previous = cycle[(i + cycle.size() - 1) % cycle.size()];
        const Corner &vertex = cycle[i];
        const Corner &following = cycle[(i + 1) % cycle.size()];

        const std::int64_t in_x = vertex.x - previous.x;
        const std::int64_t in_y = vertex.y - previous.y;
        const std::int64_t out_x = following.x - vertex.x;
        const std::int64_t out_y = following.y - vertex.y;
        if (in_x == out_x && in_y == out_y) {
            continue;
        }

        auto x = whole_game_units(vertex.x);
        if (!x) {
            return Failure::failure(x.error());
        }
        auto y = whole_game_units(vertex.y);
        if (!y) {
            return Failure::failure(y.error());
        }
        ring.push_back(Point { x.value(), y.value() });
    }

    return Failure::success(std::move(ring));
}

} // namespace tiles::content
