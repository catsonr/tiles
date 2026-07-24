#pragma once

#include "core/OrientedPrototile.h"
#include "core/Prototile.h"
#include "core/Result.h"
#include "core/geometry/Polygon.h"
#include "engine/Palette.h"
#include "engine/State.h"

#include <optional>

namespace tiles::engine {

// The construction stage at which handcrafted tetromino assembly failed.
enum class TetrominoStateStage {
    polygon,
    prototile,
    palette_entry,
    palette,
};

// A typed failure from make_tetromino_state. It preserves the failing stage, the
// affected prototile id when the failure belongs to one piece, and the complete
// underlying core error. Exactly one underlying-error field is populated, and it
// matches stage; palette failures carry no prototile id.
struct TetrominoStateError final {
    TetrominoStateStage stage;
    std::optional<PrototileId> prototile_id;
    std::optional<PolygonError> polygon_error;
    std::optional<PrototileError> prototile_error;
    std::optional<LatticeOrientationError> orientation_error;
    std::optional<PaletteError> palette_error;
};

// Deterministic handcrafted application content: the seven one-sided tetrominoes
// in authored palette order o, i, t, s, z, j, l (ids 1..7), each with unlimited
// supply and the four quarter-turn orientations, paired with an empty
// arrangement. This is not procedural generation and not a core geometry
// feature. It is fallible only because every core construction path is checked.
Result<State, TetrominoStateError> make_tetromino_state();

} // namespace tiles::engine
