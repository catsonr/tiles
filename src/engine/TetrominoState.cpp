#include "engine/TetrominoState.h"

#include "core/Arrangement.h"
#include "core/Orientation.h"
#include "core/Region.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "engine/Level.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace tiles::engine {

namespace {

// One game unit is Coordinate::SCALE raw units, so a tetromino cell is a
// SCALE x SCALE square. Points are built exactly on whole game-unit coordinates.
Point unit_point(std::int64_t p_gx, std::int64_t p_gy) {
    return Point {
        Coordinate::from_raw(p_gx * Coordinate::SCALE),
        Coordinate::from_raw(p_gy * Coordinate::SCALE),
    };
}

std::vector<Point> unit_ring(
    const std::vector<std::pair<std::int64_t, std::int64_t>> &p_cells) {
    std::vector<Point> ring;
    ring.reserve(p_cells.size());
    for (const auto &cell : p_cells) {
        ring.push_back(unit_point(cell.first, cell.second));
    }
    return ring;
}

struct PieceSpec final {
    PrototileId::Value id;
    std::vector<std::pair<std::int64_t, std::int64_t>> cells;
};

TetrominoStateError polygon_failure(PrototileId p_id, PolygonError p_error) {
    TetrominoStateError error {};
    error.stage = TetrominoStateStage::polygon;
    error.prototile_id = p_id;
    error.polygon_error = p_error;
    return error;
}

TetrominoStateError prototile_failure(PrototileId p_id, PrototileError p_error) {
    TetrominoStateError error {};
    error.stage = TetrominoStateStage::prototile;
    error.prototile_id = p_id;
    error.prototile_error = p_error;
    return error;
}

TetrominoStateError palette_entry_failure(
    PrototileId p_id, LatticeOrientationError p_error) {
    TetrominoStateError error {};
    error.stage = TetrominoStateStage::palette_entry;
    error.prototile_id = p_id;
    error.orientation_error = p_error;
    return error;
}

TetrominoStateError palette_failure(PaletteError p_error) {
    TetrominoStateError error {};
    error.stage = TetrominoStateStage::palette;
    error.palette_error = p_error;
    return error;
}

TetrominoStateError region_polygon_failure(PolygonError p_error) {
    TetrominoStateError error {};
    error.stage = TetrominoStateStage::region_outer_polygon;
    error.polygon_error = p_error;
    return error;
}

TetrominoStateError region_failure(RegionError p_error) {
    TetrominoStateError error {};
    error.stage = TetrominoStateStage::region;
    error.region_error = p_error;
    return error;
}

// The temporary debug region: one axis-aligned rectangle on exact whole
// game-unit coordinates, with no holes. The debug arrangement occupies
// x in [0, 28] and y in [-22, 0], and a mated tetromino reaches at most four
// units past an anchor, so these bounds leave a clear margin around everything
// the construction fixture and its interaction can produce.
constexpr std::int64_t DEBUG_REGION_MIN_X = -8;
constexpr std::int64_t DEBUG_REGION_MIN_Y = -30;
constexpr std::int64_t DEBUG_REGION_MAX_X = 36;
constexpr std::int64_t DEBUG_REGION_MAX_Y = 8;

} // namespace

Result<State, TetrominoStateError> make_tetromino_state() {
    // Authored palette order o, i, t, s, z, j, l with stable ids 1..7. Each ring
    // has only direction-changing corners and agrees with the act-2 tetromino
    // fixtures; s and z, and j and l, are distinct chiral prototiles.
    const std::vector<PieceSpec> specs = {
        { 1, { { 0, 0 }, { 2, 0 }, { 2, 2 }, { 0, 2 } } },
        { 2, { { 0, 0 }, { 4, 0 }, { 4, 1 }, { 0, 1 } } },
        { 3, { { 0, 0 }, { 3, 0 }, { 3, 1 }, { 2, 1 }, { 2, 2 }, { 1, 2 }, { 1, 1 }, { 0, 1 } } },
        { 4, { { 0, 0 }, { 2, 0 }, { 2, 1 }, { 3, 1 }, { 3, 2 }, { 1, 2 }, { 1, 1 }, { 0, 1 } } },
        { 5, { { 1, 0 }, { 3, 0 }, { 3, 1 }, { 2, 1 }, { 2, 2 }, { 0, 2 }, { 0, 1 }, { 1, 1 } } },
        { 6, { { 0, 0 }, { 2, 0 }, { 2, 3 }, { 1, 3 }, { 1, 1 }, { 0, 1 } } },
        { 7, { { 0, 0 }, { 2, 0 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } } },
    };

    const std::vector<Orientation> requested = {
        Orientation::reference(),
        Orientation::quarter(),
        Orientation::half(),
        Orientation::three_quarter(),
    };

    std::vector<PaletteEntry> entries;
    entries.reserve(specs.size());

    for (const PieceSpec &spec : specs) {
        const PrototileId id(spec.id);

        auto polygon = Polygon::make(unit_ring(spec.cells));
        if (!polygon) {
            return Result<State, TetrominoStateError>::failure(
                polygon_failure(id, polygon.error()));
        }

        auto prototile = Prototile::make(id, std::move(polygon).value());
        if (!prototile) {
            return Result<State, TetrominoStateError>::failure(
                prototile_failure(id, prototile.error()));
        }

        auto entry = PaletteEntry::make(
            std::move(prototile).value(), Supply::unlimited(), requested);
        if (!entry) {
            return Result<State, TetrominoStateError>::failure(
                palette_entry_failure(id, entry.error()));
        }

        entries.push_back(std::move(entry).value());
    }

    auto palette = Palette::make(std::move(entries));
    if (!palette) {
        return Result<State, TetrominoStateError>::failure(
            palette_failure(palette.error()));
    }

    // Every construction is inspected; no geometry is asserted into existence.
    auto outer = Polygon::make({
        unit_point(DEBUG_REGION_MIN_X, DEBUG_REGION_MIN_Y),
        unit_point(DEBUG_REGION_MAX_X, DEBUG_REGION_MIN_Y),
        unit_point(DEBUG_REGION_MAX_X, DEBUG_REGION_MAX_Y),
        unit_point(DEBUG_REGION_MIN_X, DEBUG_REGION_MAX_Y),
    });
    if (!outer) {
        return Result<State, TetrominoStateError>::failure(
            region_polygon_failure(outer.error()));
    }

    auto region = Region::make(std::move(outer).value(), {});
    if (!region) {
        return Result<State, TetrominoStateError>::failure(
            region_failure(region.error()));
    }

    return Result<State, TetrominoStateError>::success(
        State(Level(std::move(palette).value(), std::move(region).value())));
}

} // namespace tiles::engine
