#include "engine/State.h"

#include "core/Placement.h"
#include "core/Prototile.h"
#include "engine/Supply.h"

#include <optional>
#include <utility>
#include <vector>

namespace tiles::engine {

namespace {

// Configured supply is immutable capacity, never a stored counter: the single
// authoritative source of usage is the arrangement, so every command derives it
// afresh. Identity is the exact PrototileId — not geometry comparison, pointer
// identity, storage order, orientation, or how a placement entered the
// arrangement — so a matching placement preloaded through the constructor counts
// just as one added by a command does.
//
// Counting stops at the configured amount, so the running count never exceeds a
// value the capacity itself holds and no remaining count is derived by
// subtraction. A finite UINT64_MAX capacity therefore has no overflow path.
bool supply_available(
    const Arrangement &p_arrangement, PrototileId p_id, Supply p_supply) {
    const std::optional<Supply::Amount> capacity = p_supply.finite_amount();
    if (!capacity.has_value()) {
        return true;
    }

    Supply::Amount used = 0;
    for (const Entry &entry : p_arrangement.entries()) {
        if (entry.placement.prototile().id() != p_id) {
            continue;
        }
        ++used;
        if (used == capacity.value()) {
            return false;
        }
    }
    return true;
}

} // namespace

State::State(Palette p_palette, Arrangement p_arrangement) :
    palette_(std::move(p_palette)),
    arrangement_(std::move(p_arrangement)) {}

Result<const OrientedPrototile *, CandidateError> State::resolve_candidate(
    PaletteEntryIndex p_entry, PaletteOrientationIndex p_orientation) const {
    using Resolved = Result<const OrientedPrototile *, CandidateError>;

    // Every index is range-checked before it indexes anything. Nothing asserts,
    // clamps, wraps, or silently selects another entry or orientation.
    const std::vector<PaletteEntry> &entries = palette_.entries();
    if (p_entry.value() >= entries.size()) {
        return Resolved::failure(CandidateError::palette_entry_out_of_range);
    }
    const PaletteEntry &entry = entries[p_entry.value()];

    const std::vector<OrientedPrototile> &orientations = entry.orientations();
    if (p_orientation.value() >= orientations.size()) {
        return Resolved::failure(CandidateError::orientation_out_of_range);
    }

    // Supply belongs to the entry, so every orientation of one entry draws from
    // the same capacity.
    if (!supply_available(arrangement_, entry.prototile().id(), entry.supply())) {
        return Resolved::failure(CandidateError::supply_exhausted);
    }

    return Resolved::success(&orientations[p_orientation.value()]);
}

Result<PlacementId, PlaceCommandError> State::apply(const PlaceCommand &p_command) {
    using Applied = Result<PlacementId, PlaceCommandError>;

    auto candidate = resolve_candidate(p_command.palette_entry, p_command.orientation);
    if (!candidate) {
        return Applied::failure(PlaceCommandError(candidate.error()));
    }

    // The command's point reaches the core exactly as authored.
    auto placement = Placement::make(*candidate.value(), p_command.translation);
    if (!placement) {
        return Applied::failure(PlaceCommandError(placement.error()));
    }

    // Overlap is never pre-tested here: arrangement insertion remains the single
    // whole-footprint proof, and the only thing that mutates state.
    auto inserted = arrangement_.try_insert(std::move(placement).value());
    if (!inserted) {
        return Applied::failure(PlaceCommandError(inserted.error()));
    }

    return Applied::success(inserted.value());
}

Result<PlacementId, MateCommandError> State::apply(const MateFullEdgesCommand &p_command) {
    using Applied = Result<PlacementId, MateCommandError>;

    auto candidate =
        resolve_candidate(p_command.candidate_entry, p_command.candidate_orientation);
    if (!candidate) {
        return Applied::failure(MateCommandError(candidate.error()));
    }

    // Anchor lookup, edge bounds, edge compatibility, translation derivation,
    // footprint construction, overlap testing, and id allocation all belong to
    // the core operation; none of it is duplicated or pre-tested here.
    auto joined = arrangement_.try_join_full_edges(
        p_command.anchor,
        p_command.anchor_edge,
        *candidate.value(),
        p_command.candidate_edge);
    if (!joined) {
        return Applied::failure(MateCommandError(joined.error()));
    }

    return Applied::success(joined.value());
}

Result<PlacementId, MateCommandError> State::apply(const MateVerticesCommand &p_command) {
    using Applied = Result<PlacementId, MateCommandError>;

    auto candidate =
        resolve_candidate(p_command.candidate_entry, p_command.candidate_orientation);
    if (!candidate) {
        return Applied::failure(MateCommandError(candidate.error()));
    }

    auto joined = arrangement_.try_join_vertices(
        p_command.anchor,
        p_command.anchor_vertex,
        *candidate.value(),
        p_command.candidate_vertex);
    if (!joined) {
        return Applied::failure(MateCommandError(joined.error()));
    }

    return Applied::success(joined.value());
}

} // namespace tiles::engine
