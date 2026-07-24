#pragma once

#include "core/Arrangement.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Result.h"
#include "engine/Commands.h"
#include "engine/Palette.h"

namespace tiles::engine {

// The single state-owning engine aggregate: one palette together with one
// authoritative arrangement. It owns both values, accepts any already-valid
// arrangement (including an empty one), and never reconstructs, normalizes, or
// copies polygons into a second representation.
//
// Observation is read-only: neither accessor exposes a mutable reference or
// pointer. The apply overload set is the complete mutation surface above the
// core, so every placement added here comes from the engine-owned palette and
// respects its configured supply. State is not a tetromino state machine and
// does not know which geometry tier compiled its palette: it branches on no
// content identity and imposes no integer grid, cell size, or snapping rule.
//
// Each overload delegates all geometric construction and validation to the core
// and preserves the core's typed failure unchanged. Candidate lookup and supply
// derivation are read-only, and the core's insertion and mating verbs are
// already transactional, so a failed command leaves every observable value —
// palette entries, arrangement entries and their order, existing placements and
// ids, next_id(), and derived supply usage — completely unchanged.
class State final {
public:
    State(Palette p_palette, Arrangement p_arrangement);

    const Palette &palette() const {
        return palette_;
    }

    const Arrangement &arrangement() const {
        return arrangement_;
    }

    // Place a palette-authored candidate at the command's exact translation.
    Result<PlacementId, PlaceCommandError> apply(const PlaceCommand &p_command);

    // Mate a complete anchor footprint edge with a complete candidate edge.
    Result<PlacementId, MateCommandError> apply(const MateFullEdgesCommand &p_command);

    // Mate a selected anchor footprint vertex with a selected candidate vertex.
    Result<PlacementId, MateCommandError> apply(const MateVerticesCommand &p_command);

    // Derive the exact placement the corresponding apply would insert, without
    // mutating anything. Candidate resolution, supply enforcement, and their
    // established precedence are shared with apply, and the core's typed failure
    // is preserved unchanged.
    //
    // Preview consumes neither supply nor a placement id: supply is derived from
    // arrangement contents, which only a successful apply changes. So while the
    // state is otherwise unmodified, repeated previews are identical, and a
    // successful preview implies that an immediate matching apply succeeds and
    // stores exactly that geometry. Across an intervening mutation it promises
    // nothing.
    Result<Placement, MateCommandError> preview(const MateFullEdgesCommand &p_command) const;

    Result<Placement, MateCommandError> preview(const MateVerticesCommand &p_command) const;

private:
    // Resolve one palette-authored oriented candidate and enforce its configured
    // supply, in the fixed precedence entry -> orientation -> supply. The
    // returned pointer refers into the state-owned palette, which no command
    // mutates or reorders, and addresses the palette's stored oriented value
    // directly rather than recompiling orientation geometry.
    Result<const OrientedPrototile *, CandidateError> resolve_candidate(
        PaletteEntryIndex p_entry, PaletteOrientationIndex p_orientation) const;

    Palette palette_;
    Arrangement arrangement_;
};

} // namespace tiles::engine
