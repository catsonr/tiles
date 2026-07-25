#include "engine/State.h"

#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/geometry/ExactInteger.h"
#include "core/geometry/Predicates.h"
#include "engine/Supply.h"

#include <cassert>
#include <optional>
#include <utility>
#include <vector>

namespace tiles::engine {

namespace {

// Configured supply is immutable capacity, never a stored counter: the single
// authoritative source of usage is the arrangement, so every command derives it
// afresh. Identity is the exact PrototileId — not geometry comparison, pointer
// identity, storage order, orientation, or which command added a placement.
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

// The impossible immediate insertion failure, mapped back into the complete
// JoinError representation. Every alternative keeps its exact payload: an
// overlap keeps the conflicting id, an exhausted allocator carries none.
JoinError join_error_from_arrangement(const ArrangementError &p_error) {
    switch (p_error.code) {
        case ArrangementErrorCode::interior_overlap:
            return JoinError { JoinErrorCode::interior_overlap, p_error.conflicting_placement };
        case ArrangementErrorCode::identifier_exhausted:
            return JoinError { JoinErrorCode::identifier_exhausted, std::nullopt };
    }
    // Unreachable: every ArrangementErrorCode is mapped above.
    return JoinError { JoinErrorCode::identifier_exhausted, std::nullopt };
}

} // namespace

State::State(Level p_level) :
    level_(std::move(p_level)),
    arrangement_() {}

State State::testing_with_empty_arrangement(
    Level p_level, Arrangement p_empty_arrangement) {
    // The seam presets an id allocator, nothing else. An empty arrangement is
    // always valid, so this cannot fabricate an unproven placement.
    assert(p_empty_arrangement.entries().empty());
    State state(std::move(p_level));
    state.arrangement_ = std::move(p_empty_arrangement);
    return state;
}

Result<const OrientedPrototile *, CandidateError> State::resolve_candidate(
    PaletteEntryIndex p_entry, PaletteOrientationIndex p_orientation) const {
    using Resolved = Result<const OrientedPrototile *, CandidateError>;

    // Every index is range-checked before it indexes anything. Nothing asserts,
    // clamps, wraps, or silently selects another entry or orientation.
    const std::vector<PaletteEntry> &entries = palette().entries();
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

Result<Placement, PlaceCommandError> State::preview(const PlaceCommand &p_command) const {
    using Previewed = Result<Placement, PlaceCommandError>;

    auto candidate = resolve_candidate(p_command.palette_entry, p_command.orientation);
    if (!candidate) {
        return Previewed::failure(PlaceCommandError(candidate.error()));
    }

    // The command's point reaches the core exactly as authored.
    auto placement = Placement::make(*candidate.value(), p_command.translation);
    if (!placement) {
        return Previewed::failure(PlaceCommandError(placement.error()));
    }

    // Region legality is proven over the authoritative footprint polygon —
    // never over the translation alone, the untranslated oriented polygon, a
    // vertex subset, a bounding box, or an area comparison.
    if (!region().contains(placement.value().footprint())) {
        return Previewed::failure(
            PlaceCommandError(RegionPlacementError::outside_region));
    }

    // Overlap and identifier availability are never pre-tested here: the
    // arrangement's own insertion proof remains the single whole-footprint
    // authority, and this preview neither reserves nor consumes anything.
    auto insertable = arrangement_.preview_insert(std::move(placement).value());
    if (!insertable) {
        return Previewed::failure(PlaceCommandError(insertable.error()));
    }

    return Previewed::success(std::move(insertable).value());
}

Result<PlacementId, PlaceCommandError> State::apply(const PlaceCommand &p_command) {
    using Applied = Result<PlacementId, PlaceCommandError>;

    // Candidate resolution, footprint construction, the region proof, and the
    // insertion proof all belong to the preview above; none of it is repeated
    // here. The previewed placement then flows through the one operation that
    // mutates, so insertion stays the sole invariant-preserving path.
    auto previewed = preview(p_command);
    if (!previewed) {
        return Applied::failure(previewed.error());
    }

    auto inserted = arrangement_.try_insert(std::move(previewed).value());
    if (!inserted) {
        return Applied::failure(PlaceCommandError(inserted.error()));
    }

    return Applied::success(inserted.value());
}

Result<Placement, MateCommandError> State::preview(
    const MateFullEdgesCommand &p_command) const {
    using Previewed = Result<Placement, MateCommandError>;

    // The same resolution apply performs, in the same precedence, against the
    // same authoritative arrangement. Nothing is reserved or counted here:
    // supply is derived from arrangement contents, which this cannot change.
    auto candidate =
        resolve_candidate(p_command.candidate_entry, p_command.candidate_orientation);
    if (!candidate) {
        return Previewed::failure(MateCommandError(candidate.error()));
    }

    auto previewed = arrangement_.preview_join_full_edges(
        p_command.anchor,
        p_command.anchor_edge,
        *candidate.value(),
        p_command.candidate_edge);
    if (!previewed) {
        return Previewed::failure(MateCommandError(previewed.error()));
    }

    // The core join preview already proved anchor identity, feature bounds,
    // compatibility, footprint construction, overlap, and identifier
    // availability. Region containment is the engine's own condition and is
    // deliberately applied last, after those completed core alternatives.
    if (!region().contains(previewed.value().footprint())) {
        return Previewed::failure(
            MateCommandError(RegionPlacementError::outside_region));
    }

    return Previewed::success(std::move(previewed).value());
}

Result<Placement, MateCommandError> State::preview(
    const MateVerticesCommand &p_command) const {
    using Previewed = Result<Placement, MateCommandError>;

    auto candidate =
        resolve_candidate(p_command.candidate_entry, p_command.candidate_orientation);
    if (!candidate) {
        return Previewed::failure(MateCommandError(candidate.error()));
    }

    auto previewed = arrangement_.preview_join_vertices(
        p_command.anchor,
        p_command.anchor_vertex,
        *candidate.value(),
        p_command.candidate_vertex);
    if (!previewed) {
        return Previewed::failure(MateCommandError(previewed.error()));
    }

    if (!region().contains(previewed.value().footprint())) {
        return Previewed::failure(
            MateCommandError(RegionPlacementError::outside_region));
    }

    return Previewed::success(std::move(previewed).value());
}

Result<PlacementId, MateCommandError> State::apply(const MateFullEdgesCommand &p_command) {
    using Applied = Result<PlacementId, MateCommandError>;

    // The mating geometry is derived exactly once, by the preview. Inserting its
    // returned placement directly avoids a second derivation and keeps preview
    // and apply from becoming two command paths to hold in agreement.
    auto previewed = preview(p_command);
    if (!previewed) {
        return Applied::failure(previewed.error());
    }

    auto inserted = arrangement_.try_insert(std::move(previewed).value());
    if (!inserted) {
        return Applied::failure(
            MateCommandError(join_error_from_arrangement(inserted.error())));
    }

    return Applied::success(inserted.value());
}

Result<PlacementId, MateCommandError> State::apply(const MateVerticesCommand &p_command) {
    using Applied = Result<PlacementId, MateCommandError>;

    auto previewed = preview(p_command);
    if (!previewed) {
        return Applied::failure(previewed.error());
    }

    auto inserted = arrangement_.try_insert(std::move(previewed).value());
    if (!inserted) {
        return Applied::failure(
            MateCommandError(join_error_from_arrangement(inserted.error())));
    }

    return Applied::success(inserted.value());
}

bool State::solved() const {
    Int256 covered;
    for (const Entry &entry : arrangement_.entries()) {
        covered = covered + signed_double_area(entry.placement.footprint().vertices());
    }
    return covered == region().doubled_area();
}

} // namespace tiles::engine
