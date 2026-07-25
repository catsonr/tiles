#pragma once

#include "content/PrototileCatalog.h"
#include "core/OrientedPrototile.h"
#include "core/Prototile.h"
#include "core/Region.h"
#include "core/Result.h"
#include "core/geometry/Polygon.h"
#include "engine/Palette.h"
#include "engine/State.h"

#include <optional>

namespace tiles::engine {

// The construction stage at which the temporary tetromino bootstrap failed.
enum class TetrominoStateStage {
    catalog_lookup,
    palette_entry,
    palette,
    region_outer_polygon,
    region,
};

// A typed failure from make_tetromino_state. It preserves the failing stage, the
// affected prototile id when the failure belongs to one piece, and the complete
// underlying core error. At most one underlying-error field is populated, and it
// matches stage; palette and region failures carry no prototile id. A missing
// canonical id populates no underlying error, because absence from the supplied
// catalog is the complete fact. The debug region's outer boundary is an ordinary
// polygon, so its construction failure is reported through polygon_error at the
// region_outer_polygon stage.
struct TetrominoStateError final {
    TetrominoStateStage stage;
    std::optional<PrototileId> prototile_id;
    std::optional<PolygonError> polygon_error;
    std::optional<LatticeOrientationError> orientation_error;
    std::optional<PaletteError> palette_error;
    std::optional<RegionError> region_error;
};

// Deterministic temporary application content: the seven one-sided tetrominoes
// resolved from the supplied canonical catalog by their stable ids 1..7, in
// catalog order, each with unlimited supply and the four quarter-turn
// orientations, paired with one temporary no-hole rectangular debug region and
// an empty arrangement.
//
// The bootstrap owns no geometry, name, or presentation table of its own: the
// catalog is the sole source of playable prototile geometry, and this factory
// only selects from it. A missing expected id is therefore a typed failure
// rather than a fallback, although the shipped catalog makes it unreachable in
// ordinary execution.
//
// The debug region's size and identity are a construction fixture, not a
// player-facing level contract: it is simply large enough to contain the debug
// arrangement and ordinary interaction around it. This is not procedural
// generation and not a core geometry feature. It is fallible only because every
// core construction path is checked.
Result<State, TetrominoStateError> make_tetromino_state(
    const content::PrototileCatalog &p_catalog);

} // namespace tiles::engine
