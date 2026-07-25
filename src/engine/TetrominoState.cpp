#include "engine/TetrominoState.h"

#include "content/PrototileCatalog.h"
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

// One game unit is Coordinate::SCALE raw units. The debug region's corners are
// built exactly on whole game-unit coordinates.
Point unit_point(std::int64_t p_gx, std::int64_t p_gy) {
    return Point {
        Coordinate::from_raw(p_gx * Coordinate::SCALE),
        Coordinate::from_raw(p_gy * Coordinate::SCALE),
    };
}

TetrominoStateError catalog_lookup_failure(PrototileId p_id) {
    TetrominoStateError error {};
    error.stage = TetrominoStateStage::catalog_lookup;
    error.prototile_id = p_id;
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

// The canonical ids this temporary fixture selects, in the palette order the
// construction surface has always used: o, i, t, s, z, j, l.
constexpr PrototileId::Value BOOTSTRAP_IDS[] = { 1, 2, 3, 4, 5, 6, 7 };

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

Result<State, TetrominoStateError> make_tetromino_state(
    const content::PrototileCatalog &p_catalog) {
    const std::vector<Orientation> requested = {
        Orientation::reference(),
        Orientation::quarter(),
        Orientation::half(),
        Orientation::three_quarter(),
    };

    std::vector<PaletteEntry> entries;
    entries.reserve(sizeof(BOOTSTRAP_IDS) / sizeof(BOOTSTRAP_IDS[0]));

    for (const PrototileId::Value value : BOOTSTRAP_IDS) {
        const PrototileId id(value);

        // Exact lookup with no fallback: an absent id fails the bootstrap rather
        // than substituting other geometry.
        const content::CanonicalPrototile *canonical = p_catalog.find(id);
        if (canonical == nullptr) {
            return Result<State, TetrominoStateError>::failure(catalog_lookup_failure(id));
        }

        auto entry = PaletteEntry::make(
            canonical->prototile(), Supply::unlimited(), requested);
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
