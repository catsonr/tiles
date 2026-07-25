#pragma once

#include "core/Arrangement.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Region.h"
#include "core/Result.h"
#include "engine/Commands.h"
#include "engine/Level.h"
#include "engine/Palette.h"

#include <cstdint>

namespace tiles::engine {

// The single state-owning engine aggregate: one level together with one
// authoritative arrangement. It owns both values and never reconstructs,
// normalizes, or copies polygons into a second representation.
//
// Production construction always starts from an empty arrangement. There is
// deliberately no constructor taking a prebuilt arrangement: such a seam could
// inject placements outside the region, absent from the palette, geometrically
// inconsistent with a palette id, in an unoffered orientation, or beyond finite
// supply — and then solved() would no longer be a theorem. Every placement in a
// State has therefore been proven against the palette, its supply, the region,
// and the existing arrangement.
//
// Observation is read-only: no accessor exposes a mutable reference or pointer.
// The apply overload set is the complete mutation surface above the core. State
// is not a tetromino state machine and does not know which geometry tier
// compiled its palette: it branches on no content identity and imposes no
// integer grid, cell size, or snapping rule.
//
// Each overload delegates all geometric construction and validation to the core
// and preserves the core's typed failure unchanged. Candidate lookup and supply
// derivation are read-only, and every mutation routes through the corresponding
// preview and then the core's transactional insertion, so a failed command
// leaves every observable value — palette entries, region, arrangement entries
// and their order, existing placements and ids, next_id(), derived supply usage,
// and solved() — completely unchanged.
class State final {
public:
    explicit State(Level p_level);

    // Test-only construction seam: the same empty-arrangement invariant with a
    // preset id allocator, so identifier exhaustion can be exercised without
    // allocating an astronomical number of placements. It accepts only an empty
    // arrangement, so it cannot smuggle in an unproven placement.
    static State testing_with_empty_arrangement(
        Level p_level, Arrangement p_empty_arrangement);

    const Level &level() const {
        return level_;
    }

    // Convenience views through the owned level; neither is a second copy.
    const Palette &palette() const {
        return level_.palette();
    }

    const Region &region() const {
        return level_.region();
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
    // mutating anything. Candidate resolution, region containment, insertion
    // proof, and their established precedence are shared with apply, and the
    // core's typed failure is preserved unchanged.
    //
    // Preview consumes neither supply nor a placement id: supply is derived from
    // arrangement contents, which only a successful apply changes. So while the
    // state is otherwise unmodified, repeated previews are identical, and a
    // successful preview implies that an immediate matching apply succeeds and
    // stores exactly that geometry. Across an intervening mutation it promises
    // nothing.
    //
    // The direct-placement preview is the pure counterpart of apply(PlaceCommand)
    // and is the only preview an empty arrangement can answer: a mating proposal
    // needs an existing placement feature to derive from.
    Result<Placement, PlaceCommandError> preview(const PlaceCommand &p_command) const;

    Result<Placement, MateCommandError> preview(const MateFullEdgesCommand &p_command) const;

    Result<Placement, MateCommandError> preview(const MateVerticesCommand &p_command) const;

    // Exact completion: the summed doubled area of every placed footprint equals
    // the region's exact doubled area.
    //
    // This is a theorem rather than a coincidence. Every State begins empty,
    // every insertion proves region containment, and the arrangement maintains
    // pairwise interior disjointness, so the summed area is exactly the covered
    // area and never exceeds the region's. Equality between finite closed
    // polygonal sets under those invariants leaves no uncovered relative-open
    // subset. No union polygon, rasterization, tile count, adjacency test,
    // tolerance, floating area, or search is involved.
    bool solved() const;

private:
    // Resolve one palette-authored oriented candidate and enforce its configured
    // supply, in the fixed precedence entry -> orientation -> supply. The
    // returned pointer refers into the state-owned palette, which no command
    // mutates or reorders, and addresses the palette's stored oriented value
    // directly rather than recompiling orientation geometry.
    Result<const OrientedPrototile *, CandidateError> resolve_candidate(
        PaletteEntryIndex p_entry, PaletteOrientationIndex p_orientation) const;

    Level level_;
    Arrangement arrangement_;
};

} // namespace tiles::engine
