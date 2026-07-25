#pragma once

#include "core/Arrangement.h"
#include "core/Orientation.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/Result.h"
#include "core/geometry/Point.h"
#include "engine/Palette.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace tiles::engine {

// One authored record naming which palette identity, which of that identity's
// distinct compiled orientations, and which exact translation one placement
// carries.
//
// It is a domain-blind transport value: the orientation is the representative
// orientation of one distinct compiled variant, never an arbitrary equivalent
// label and never a transient index into a palette vector, so the record stays
// meaningful across recompilation and across any reordering of the values it
// names. It owns no geometry: the palette and the compiler below resolve it into
// exact values every time.
struct BlueprintPlacement final {
    PrototileId prototile_id;
    Orientation orientation;
    Point translation;
};

enum class BlueprintCompilationErrorCode {
    prototile_not_in_palette,
    orientation_not_in_palette,
    supply_exhausted,
    placement_construction_failed,
    arrangement_insertion_failed,
};

// The complete typed failure of one record.
//
// `placement` is the failing record's index and `prototile_id` its named
// identity; both are populated by every failure. `orientation` is populated by
// the orientation failure and by every later failure, because those have already
// resolved one distinct variant. `placement_error` is populated only by
// placement construction and `arrangement_error` only by arrangement insertion;
// every other optional is empty.
struct BlueprintCompilationError final {
    BlueprintCompilationErrorCode code;
    std::size_t placement;
    PrototileId prototile_id;
    std::optional<Orientation> orientation;
    std::optional<PlacementError> placement_error;
    std::optional<ArrangementError> arrangement_error;
};

// Compile a complete stored sequence of blueprint records into one exact
// arrangement against one exact palette.
//
// The compilation is pure, total, and transactional: records are resolved in
// stored order, the first failing record stops compilation, and a failure
// returns no partial arrangement. An empty sequence succeeds with an empty
// arrangement — emptiness is an ordinary authoring state, not a defect.
//
// For each record the compiler finds its exact id in authored palette order,
// finds the oriented value whose representative orientation equals the record's,
// rejects the record when the identity's finite supply is already consumed by
// prior records, builds the placement through the ordinary Placement factory,
// and inserts it through the ordinary arrangement proof. Nothing here reorders
// records, treats a palette index as identity, reinterprets an equivalent
// orientation label as a representative, clamps a supply, or repairs an overlap.
//
// This is authoring support, not a second runtime State. It owns no geometry
// domain, resource, color, selection, history, or region.
Result<Arrangement, BlueprintCompilationError> compile_blueprint(
    const Palette &p_palette,
    const std::vector<BlueprintPlacement> &p_placements);

} // namespace tiles::engine
