#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/Region.h"
#include "core/geometry/Alignment.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "engine/Commands.h"
#include "engine/Level.h"
#include "engine/Palette.h"
#include "engine/Session.h"
#include "engine/State.h"
#include "engine/Supply.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace tiles;
using tiles::engine::CandidateError;
using tiles::engine::Level;
using tiles::engine::MateCommandError;
using tiles::engine::MateFullEdgesCommand;
using tiles::engine::MateVerticesCommand;
using tiles::engine::Palette;
using tiles::engine::PaletteEntry;
using tiles::engine::PaletteEntryIndex;
using tiles::engine::PaletteOrientationIndex;
using tiles::engine::PlaceCommand;
using tiles::engine::PlaceCommandError;
using tiles::engine::RegionPlacementError;
using tiles::engine::RemoveCommand;
using tiles::engine::RemoveCommandError;
using tiles::engine::Session;
using tiles::engine::State;
using tiles::engine::Supply;
using tiles::engine::SupplyStatus;
using tiles_test::raw_pt;

namespace {

// ---------------------------------------------------------------------------
// fixtures
// ---------------------------------------------------------------------------

Point unit(std::int64_t p_gx, std::int64_t p_gy) {
    return raw_pt(p_gx * Coordinate::SCALE, p_gy * Coordinate::SCALE);
}

Polygon unit_box(
    std::int64_t p_min_x, std::int64_t p_min_y, std::int64_t p_max_x, std::int64_t p_max_y) {
    auto made = Polygon::make({
        unit(p_min_x, p_min_y),
        unit(p_max_x, p_min_y),
        unit(p_max_x, p_max_y),
        unit(p_min_x, p_max_y),
    });
    return std::move(made).value();
}

// A 1 x 1 game-unit square: the tile every fixture below places.
Prototile unit_square(std::uint64_t p_id) {
    return std::move(Prototile::make(PrototileId(p_id), unit_box(0, 0, 1, 1))).value();
}

// A 2 x 1 game-unit bar, so one fixture can hold two distinct prototile
// identities and two distinct orientations.
Prototile bar(std::uint64_t p_id) {
    return std::move(Prototile::make(PrototileId(p_id), unit_box(0, 0, 2, 1))).value();
}

PaletteEntry entry_of(
    const Prototile &p_prototile, Supply p_supply, std::vector<Orientation> p_requested) {
    return std::move(
        PaletteEntry::make(p_prototile, p_supply, std::move(p_requested)).value());
}

Palette palette_of(std::vector<PaletteEntry> p_entries) {
    return std::move(Palette::make(std::move(p_entries)).value());
}

Palette square_palette(Supply p_supply) {
    std::vector<PaletteEntry> entries;
    entries.push_back(entry_of(unit_square(1), p_supply, { Orientation::reference() }));
    return palette_of(std::move(entries));
}

Region region_of(Polygon p_outer, std::vector<Polygon> p_holes) {
    return std::move(Region::make(std::move(p_outer), std::move(p_holes))).value();
}

// A 4 x 4 game-unit region with no holes: room for every fixture that is not
// about completion.
Region open_region() {
    return region_of(unit_box(0, 0, 4, 4), {});
}

State open_state(Supply p_supply) {
    return State(Level(square_palette(p_supply), open_region()));
}

Session open_session(Supply p_supply) {
    return Session(open_state(p_supply));
}

PlaceCommand place_at(std::size_t p_entry, std::size_t p_orientation, Point p_translation) {
    return PlaceCommand {
        PaletteEntryIndex(p_entry),
        PaletteOrientationIndex(p_orientation),
        p_translation,
    };
}

PlaceCommand place_square(Point p_translation) {
    return place_at(0, 0, p_translation);
}

// The unit square footprint's edge 1 runs (1,0)->(1,1) and its edge 3 runs
// (0,1)->(0,0), so mating them drops a second tile immediately to the right.
MateFullEdgesCommand mate_right(PlacementId p_anchor) {
    return MateFullEdgesCommand {
        p_anchor,
        EdgeIndex(1),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(3),
    };
}

// Anchor vertex 2 is the corner (1,1); the candidate's vertex 0 is its local
// origin, so the mated tile touches the anchor at that single point.
MateVerticesCommand mate_at_corner(PlacementId p_anchor) {
    return MateVerticesCommand {
        p_anchor,
        VertexIndex(2),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        VertexIndex(0),
    };
}

// ---------------------------------------------------------------------------
// complete-state equivalence
//
// The domain types deliberately publish no operator==: equality would imply
// congruence, identity, or level semantics the project has kept named or
// absent. These are test-only comparisons, and they compare every observable
// component rather than a convenient summary, so an "undo" that restored the
// right arrangement size with the wrong ids, order, allocator, or supply cannot
// pass as a restoration.
// ---------------------------------------------------------------------------

bool same_oriented(const OrientedPrototile &p_lhs, const OrientedPrototile &p_rhs) {
    if (p_lhs.prototile().id() != p_rhs.prototile().id()) {
        return false;
    }
    if (p_lhs.orientation() != p_rhs.orientation()) {
        return false;
    }
    if (p_lhs.equivalent_orientations() != p_rhs.equivalent_orientations()) {
        return false;
    }
    return same_boundary(p_lhs.canonical_polygon(), p_rhs.canonical_polygon());
}

bool same_palette_entry(const PaletteEntry &p_lhs, const PaletteEntry &p_rhs) {
    if (p_lhs.prototile().id() != p_rhs.prototile().id()) {
        return false;
    }
    if (!same_boundary(p_lhs.prototile().polygon(), p_rhs.prototile().polygon())) {
        return false;
    }
    if (p_lhs.supply() != p_rhs.supply()) {
        return false;
    }
    if (p_lhs.orientations().size() != p_rhs.orientations().size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_lhs.orientations().size(); ++i) {
        if (!same_oriented(p_lhs.orientations()[i], p_rhs.orientations()[i])) {
            return false;
        }
    }
    return true;
}

bool same_palette(const Palette &p_lhs, const Palette &p_rhs) {
    if (p_lhs.order() != p_rhs.order()) {
        return false;
    }
    for (std::size_t i = 0; i < p_lhs.entries().size(); ++i) {
        if (!same_palette_entry(p_lhs.entries()[i], p_rhs.entries()[i])) {
            return false;
        }
    }
    return true;
}

bool same_region(const Region &p_lhs, const Region &p_rhs) {
    if (!same_boundary(p_lhs.outer_boundary(), p_rhs.outer_boundary())) {
        return false;
    }
    if (p_lhs.inner_boundaries().size() != p_rhs.inner_boundaries().size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_lhs.inner_boundaries().size(); ++i) {
        if (!same_boundary(p_lhs.inner_boundaries()[i], p_rhs.inner_boundaries()[i])) {
            return false;
        }
    }
    return p_lhs.doubled_area() == p_rhs.doubled_area();
}

bool same_placement(const Placement &p_lhs, const Placement &p_rhs) {
    return p_lhs.prototile().id() == p_rhs.prototile().id()
        && p_lhs.orientation() == p_rhs.orientation()
        && p_lhs.translation() == p_rhs.translation()
        && same_boundary(p_lhs.oriented_polygon(), p_rhs.oriented_polygon())
        && same_boundary(p_lhs.footprint(), p_rhs.footprint());
}

bool same_arrangement(const Arrangement &p_lhs, const Arrangement &p_rhs) {
    if (p_lhs.entries().size() != p_rhs.entries().size()) {
        return false;
    }
    // Storage order, entry by entry: an arrangement is mathematically unordered,
    // but a restoration that reordered it would not be the same value.
    for (std::size_t i = 0; i < p_lhs.entries().size(); ++i) {
        if (p_lhs.entries()[i].id != p_rhs.entries()[i].id) {
            return false;
        }
        if (!same_placement(p_lhs.entries()[i].placement, p_rhs.entries()[i].placement)) {
            return false;
        }
    }
    // Including exhaustion, which is exactly an empty next id.
    return p_lhs.next_id() == p_rhs.next_id();
}

bool same_supply_status(
    const std::optional<SupplyStatus> &p_lhs, const std::optional<SupplyStatus> &p_rhs) {
    if (p_lhs.has_value() != p_rhs.has_value()) {
        return false;
    }
    if (!p_lhs.has_value()) {
        return true;
    }
    return p_lhs.value().used == p_rhs.value().used
        && p_lhs.value().remaining == p_rhs.value().remaining;
}

bool same_state(const State &p_lhs, const State &p_rhs) {
    if (!same_palette(p_lhs.level().palette(), p_rhs.level().palette())) {
        return false;
    }
    if (!same_region(p_lhs.level().region(), p_rhs.level().region())) {
        return false;
    }
    if (!same_arrangement(p_lhs.arrangement(), p_rhs.arrangement())) {
        return false;
    }
    // Derived, but compared anyway: these are what a palette view and a
    // completion check will actually read.
    for (std::size_t i = 0; i < p_lhs.palette().order(); ++i) {
        if (!same_supply_status(
                p_lhs.supply_status(PaletteEntryIndex(i)),
                p_rhs.supply_status(PaletteEntryIndex(i)))) {
            return false;
        }
    }
    return p_lhs.solved() == p_rhs.solved();
}

} // namespace

// ---------------------------------------------------------------------------
// the initial session
// ---------------------------------------------------------------------------

TEST_CASE("a new session owns the exact initial state and no history") {
    Session session(open_state(Supply::finite(5).value()));

    CHECK(session.undo_depth() == 0);
    CHECK(session.can_undo() == false);

    // The state it was handed, exactly.
    CHECK(same_state(session.state(), open_state(Supply::finite(5).value())));
    CHECK(session.state().arrangement().entries().empty());
    CHECK(session.state().arrangement().next_id().value() == PlacementId(0));
    CHECK(session.state().palette().order() == 1);
    CHECK(session.state().region().inner_boundaries().empty());
    CHECK(session.state().solved() == false);
}

TEST_CASE("can_undo, undo_depth, and undo agree on an empty history") {
    Session session = open_session(Supply::unlimited());
    const State before = session.state();

    CHECK(session.undo_depth() == 0);
    CHECK(session.can_undo() == (session.undo_depth() != 0));
    // Nothing to undo is ordinary interface state, not a domain failure.
    CHECK(session.undo() == false);
    CHECK(session.undo() == false);
    CHECK(session.undo_depth() == 0);
    CHECK(session.can_undo() == false);
    CHECK(same_state(session.state(), before));
}

TEST_CASE("a session exposes only a const state and no history view") {
    static_assert(
        std::is_same_v<decltype(std::declval<Session &>().state()), const State &>,
        "state() must expose only a const reference, even through a mutable session");
    // No second accessor smuggles out a mutable arrangement, level, or history.
    static_assert(
        std::is_same_v<
            decltype(std::declval<Session &>().state().arrangement()), const Arrangement &>,
        "the arrangement must remain read-only through the session");
    static_assert(
        std::is_same_v<decltype(std::declval<Session &>().undo_depth()), std::size_t>,
        "undo_depth() must report a plain size");
    static_assert(
        std::is_same_v<decltype(std::declval<Session &>().undo()), bool>,
        "undo() must report a plain bool rather than a fabricated error type");
    // A session is not constructible from nothing, and not from a level: it
    // wraps a State that has already proven itself.
    static_assert(
        std::is_constructible_v<Session, State>,
        "Session must be constructible from a state");
    static_assert(
        !std::is_default_constructible_v<Session>,
        "Session must not be default constructible");
    CHECK(true);
}

// ---------------------------------------------------------------------------
// one snapshot per success
// ---------------------------------------------------------------------------

TEST_CASE("a successful exact placement adds exactly one snapshot") {
    Session session = open_session(Supply::unlimited());
    const State initial = session.state();

    auto applied = session.apply(place_square(unit(0, 0)));
    CHECK(bool(applied));
    if (applied) {
        CHECK(applied.value() == PlacementId(0));
    }
    CHECK(session.undo_depth() == 1);
    CHECK(session.can_undo());
    CHECK(session.state().arrangement().entries().size() == 1);

    CHECK(session.undo());
    CHECK(session.undo_depth() == 0);
    CHECK(same_state(session.state(), initial));
}

TEST_CASE("a successful full-edge mating adds exactly one snapshot") {
    Session session = open_session(Supply::unlimited());
    auto anchor = session.apply(place_square(unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }
    const State after_anchor = session.state();

    auto mated = session.apply(mate_right(anchor.value()));
    CHECK(bool(mated));
    if (mated) {
        CHECK(mated.value() == PlacementId(1));
        CHECK(session.state().arrangement().entries().back().placement.translation()
            == unit(1, 0));
    }
    CHECK(session.undo_depth() == 2);

    CHECK(session.undo());
    CHECK(session.undo_depth() == 1);
    CHECK(same_state(session.state(), after_anchor));
}

TEST_CASE("a successful vertex mating adds exactly one snapshot") {
    Session session = open_session(Supply::unlimited());
    auto anchor = session.apply(place_square(unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }
    const State after_anchor = session.state();

    auto mated = session.apply(mate_at_corner(anchor.value()));
    CHECK(bool(mated));
    if (mated) {
        CHECK(mated.value() == PlacementId(1));
        CHECK(session.state().arrangement().entries().back().placement.translation()
            == unit(1, 1));
    }
    CHECK(session.undo_depth() == 2);

    CHECK(session.undo());
    CHECK(session.undo_depth() == 1);
    CHECK(same_state(session.state(), after_anchor));
}

TEST_CASE("a successful deletion adds exactly one snapshot") {
    Session session = open_session(Supply::unlimited());
    auto placed = session.apply(place_square(unit(0, 0)));
    CHECK(bool(placed));
    if (!placed) {
        return;
    }
    const State after_place = session.state();

    auto removed = session.apply(RemoveCommand { placed.value() });
    CHECK(bool(removed));
    if (removed) {
        CHECK(removed.value() == placed.value());
    }
    CHECK(session.undo_depth() == 2);
    CHECK(session.state().arrangement().entries().empty());

    CHECK(session.undo());
    CHECK(session.undo_depth() == 1);
    CHECK(same_state(session.state(), after_place));
}

TEST_CASE("every command result and error alternative survives the session unchanged") {
    // The session forwards State's own return types verbatim: nothing is
    // unified into a common command variant merely to share a signature.
    static_assert(
        std::is_same_v<
            decltype(std::declval<Session &>().apply(std::declval<const PlaceCommand &>())),
            Result<PlacementId, PlaceCommandError>>,
        "placement must keep its own error type through the session");
    static_assert(
        std::is_same_v<
            decltype(std::declval<Session &>().apply(
                std::declval<const MateFullEdgesCommand &>())),
            Result<PlacementId, MateCommandError>>,
        "full-edge mating must keep its own error type through the session");
    static_assert(
        std::is_same_v<
            decltype(std::declval<Session &>().apply(
                std::declval<const MateVerticesCommand &>())),
            Result<PlacementId, MateCommandError>>,
        "vertex mating must keep its own error type through the session");
    static_assert(
        std::is_same_v<
            decltype(std::declval<Session &>().apply(std::declval<const RemoveCommand &>())),
            Result<PlacementId, RemoveCommandError>>,
        "removal must keep its own error type through the session");

    Session session = open_session(Supply::finite(2).value());
    auto anchor = session.apply(place_square(unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    // Each published alternative arrives with its complete payload.
    auto bad_entry = session.apply(place_at(9, 0, unit(2, 2)));
    CHECK(!bad_entry);
    if (!bad_entry) {
        const CandidateError *error = std::get_if<CandidateError>(&bad_entry.error());
        CHECK(error != nullptr && *error == CandidateError::palette_entry_out_of_range);
    }

    auto overflow = session.apply(place_square(raw_pt(INT64_MAX, 0)));
    CHECK(!overflow);
    if (!overflow) {
        const PlacementError *error = std::get_if<PlacementError>(&overflow.error());
        CHECK(error != nullptr && *error == PlacementError::footprint_overflow);
    }

    auto outside = session.apply(place_square(unit(9, 9)));
    CHECK(!outside);
    if (!outside) {
        const RegionPlacementError *error =
            std::get_if<RegionPlacementError>(&outside.error());
        CHECK(error != nullptr && *error == RegionPlacementError::outside_region);
    }

    // An overlap still names the exact conflicting placement.
    auto overlapped = session.apply(place_square(unit(0, 0)));
    CHECK(!overlapped);
    if (!overlapped) {
        const ArrangementError *error = std::get_if<ArrangementError>(&overlapped.error());
        CHECK(error != nullptr);
        if (error != nullptr) {
            CHECK(error->code == ArrangementErrorCode::interior_overlap);
            CHECK(error->conflicting_placement.value() == anchor.value());
        }
    }

    auto mated = session.apply(MateFullEdgesCommand {
        anchor.value(),
        EdgeIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(0),
    });
    CHECK(!mated);
    if (!mated) {
        const JoinError *error = std::get_if<JoinError>(&mated.error());
        CHECK(error != nullptr);
        if (error != nullptr) {
            CHECK(error->code == JoinErrorCode::incompatible_edges);
            CHECK(error->conflicting_placement.has_value() == false);
        }
    }

    auto missing = session.apply(RemoveCommand { PlacementId(999) });
    CHECK(!missing);
    if (!missing) {
        CHECK(missing.error() == RemovalError::placement_not_found);
    }

    // A success still returns the exact allocated id, not a session-local value.
    auto second = session.apply(place_square(unit(2, 2)));
    CHECK(bool(second));
    if (second) {
        CHECK(second.value() == PlacementId(1));
        CHECK(session.state().arrangement().entries().back().id == second.value());
    }
}

// ---------------------------------------------------------------------------
// failure adds nothing
// ---------------------------------------------------------------------------

TEST_CASE("no failed mutation adds a snapshot or disturbs the current state") {
    Session session = open_session(Supply::finite(2).value());
    auto anchor = session.apply(place_square(unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }
    // One snapshot exists, so a hidden extra push would be observable here and
    // nowhere in the current arrangement.
    CHECK(session.undo_depth() == 1);
    const State after_anchor = session.state();

    // Candidate resolution, footprint construction, region containment, overlap
    // insertion, mating, and deletion: one failure of each.
    CHECK(!session.apply(place_at(9, 0, unit(2, 2))));
    CHECK(!session.apply(place_at(0, 9, unit(2, 2))));
    CHECK(!session.apply(place_square(raw_pt(INT64_MAX, 0))));
    CHECK(!session.apply(place_square(unit(9, 9))));
    CHECK(!session.apply(place_square(unit(0, 0))));
    CHECK(!session.apply(MateFullEdgesCommand {
        anchor.value(),
        EdgeIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(0),
    }));
    CHECK(!session.apply(MateVerticesCommand {
        PlacementId(999),
        VertexIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        VertexIndex(0),
    }));
    CHECK(!session.apply(RemoveCommand { PlacementId(999) }));

    CHECK(session.undo_depth() == 1);
    CHECK(same_state(session.state(), after_anchor));

    // And the one snapshot that does exist is the initial state, so no failure
    // quietly replaced it either.
    CHECK(session.undo());
    CHECK(session.undo_depth() == 0);
    CHECK(same_state(session.state(), open_state(Supply::finite(2).value())));
    CHECK(session.undo() == false);
}

TEST_CASE("a failed exhausted-supply mutation adds no snapshot") {
    Session session = open_session(Supply::finite(1).value());
    auto only = session.apply(place_square(unit(0, 0)));
    CHECK(bool(only));
    if (!only) {
        return;
    }
    const State spent = session.state();

    auto place_again = session.apply(place_square(unit(2, 2)));
    CHECK(!place_again);
    if (!place_again) {
        const CandidateError *error = std::get_if<CandidateError>(&place_again.error());
        CHECK(error != nullptr && *error == CandidateError::supply_exhausted);
    }
    auto mate_again = session.apply(mate_right(only.value()));
    CHECK(!mate_again);

    CHECK(session.undo_depth() == 1);
    CHECK(same_state(session.state(), spent));
}

TEST_CASE("previews create no history, whether they succeed or fail") {
    Session session = open_session(Supply::unlimited());
    auto anchor = session.apply(place_square(unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }
    const State after_anchor = session.state();
    const std::size_t before_depth = session.undo_depth();

    // Previews are reached through the const state and are never wrapped as
    // mutations.
    CHECK(bool(session.state().preview(place_square(unit(2, 2)))));
    CHECK(bool(session.state().preview(mate_right(anchor.value()))));
    CHECK(bool(session.state().preview(mate_at_corner(anchor.value()))));
    CHECK(!session.state().preview(place_square(unit(9, 9))));
    CHECK(!session.state().preview(place_square(unit(0, 0))));
    CHECK(!session.state().preview(mate_right(PlacementId(999))));

    // Nor do the read-only observations.
    CHECK(session.state().solved() == false);
    CHECK(session.state().supply_status(PaletteEntryIndex(0)).has_value());
    CHECK(session.state().level().palette().order() == 1);
    CHECK(session.can_undo());

    CHECK(session.undo_depth() == before_depth);
    CHECK(same_state(session.state(), after_anchor));
}

// ---------------------------------------------------------------------------
// undo
// ---------------------------------------------------------------------------

TEST_CASE("one undo restores the complete prior state and drops one snapshot") {
    Session session = open_session(Supply::finite(4).value());
    auto first = session.apply(place_square(unit(0, 0)));
    CHECK(bool(first));
    if (!first) {
        return;
    }
    const State after_first = session.state();

    CHECK(bool(session.apply(place_square(unit(1, 0)))));
    CHECK(session.undo_depth() == 2);

    CHECK(session.undo());
    CHECK(session.undo_depth() == 1);
    // Every observable component, not merely the entry count.
    CHECK(same_state(session.state(), after_first));
    CHECK(session.state().arrangement().entries().size() == 1);
    CHECK(session.state().arrangement().entries()[0].id == first.value());
    CHECK(session.state().supply_status(PaletteEntryIndex(0)).value().remaining.value() == 3);
}

TEST_CASE("repeated undo walks a mixed sequence back in exact reverse success order") {
    Session session = open_session(Supply::finite(6).value());

    // Every state the session has ever presented, oldest first.
    std::vector<State> timeline;
    timeline.push_back(session.state());

    auto a = session.apply(place_square(unit(0, 0)));
    CHECK(bool(a));
    if (!a) {
        return;
    }
    timeline.push_back(session.state());

    // place / place
    auto b = session.apply(place_square(unit(2, 2)));
    CHECK(bool(b));
    if (!b) {
        return;
    }
    timeline.push_back(session.state());

    // place / mate
    auto c = session.apply(mate_right(a.value()));
    CHECK(bool(c));
    if (!c) {
        return;
    }
    timeline.push_back(session.state());

    auto d = session.apply(mate_at_corner(b.value()));
    CHECK(bool(d));
    if (!d) {
        return;
    }
    timeline.push_back(session.state());

    // place / delete, deleting from the middle of storage
    CHECK(bool(session.apply(RemoveCommand { b.value() })));
    timeline.push_back(session.state());

    CHECK(session.undo_depth() == 5);
    CHECK(timeline.size() == 6);

    // Walk the whole thing back. Each undo must land exactly on the state that
    // preceded the corresponding success.
    for (std::size_t remaining = timeline.size() - 1; remaining > 0; --remaining) {
        CHECK(same_state(session.state(), timeline[remaining]));
        CHECK(session.undo_depth() == remaining);
        CHECK(session.undo());
    }
    CHECK(session.undo_depth() == 0);
    CHECK(same_state(session.state(), timeline.front()));
    CHECK(session.state().arrangement().entries().empty());

    // One more, at the bottom, changes nothing.
    CHECK(session.undo() == false);
    CHECK(session.undo_depth() == 0);
    CHECK(same_state(session.state(), timeline.front()));
}

TEST_CASE("undoing a placement rewinds the allocator and frees its identity for reuse") {
    Session session = open_session(Supply::unlimited());
    CHECK(bool(session.apply(place_square(unit(0, 0)))));
    auto second = session.apply(place_square(unit(1, 0)));
    CHECK(bool(second));
    if (!second) {
        return;
    }
    CHECK(second.value() == PlacementId(1));
    CHECK(session.state().arrangement().next_id().value() == PlacementId(2));

    CHECK(session.undo());
    // The pre-placement allocator is restored, not merely the entry removed.
    CHECK(session.state().arrangement().next_id().value() == PlacementId(1));
    CHECK(session.state().arrangement().entries().size() == 1);

    // So a later placement may legitimately receive that same identity. This is
    // exactly why no cached proposal or hit-test result may outlive a mutation.
    auto reused = session.apply(place_square(unit(2, 2)));
    CHECK(bool(reused));
    if (reused) {
        CHECK(reused.value() == PlacementId(1));
        CHECK(reused.value() == second.value());
        // A different tile at a different place, holding the same identity.
        CHECK(session.state().arrangement().entries().back().placement.translation()
            == unit(2, 2));
    }
}

TEST_CASE("ordinary deletion does not rewind the allocator, and undoing it restores order") {
    Session session = open_session(Supply::unlimited());
    auto a = session.apply(place_square(unit(0, 0)));
    auto b = session.apply(place_square(unit(1, 0)));
    auto c = session.apply(place_square(unit(2, 0)));
    CHECK(bool(a));
    CHECK(bool(b));
    CHECK(bool(c));
    if (!a || !b || !c) {
        return;
    }
    const State before_delete = session.state();
    CHECK(session.state().arrangement().next_id().value() == PlacementId(3));

    CHECK(bool(session.apply(RemoveCommand { b.value() })));
    // Deletion frees space, never an identity: the allocator is untouched.
    CHECK(session.state().arrangement().next_id().value() == PlacementId(3));
    CHECK(session.state().arrangement().entries().size() == 2);

    CHECK(session.undo());
    // The deleted entry comes back with its original identity, in its original
    // storage position, and the allocator is still exactly where it was.
    CHECK(same_state(session.state(), before_delete));
    CHECK(session.state().arrangement().entries().size() == 3);
    CHECK(session.state().arrangement().entries()[0].id == a.value());
    CHECK(session.state().arrangement().entries()[1].id == b.value());
    CHECK(session.state().arrangement().entries()[2].id == c.value());
    CHECK(session.state().arrangement().entries()[1].placement.translation() == unit(1, 0));
    CHECK(session.state().arrangement().next_id().value() == PlacementId(3));
}

TEST_CASE("finite supply, deletion, and undo all report the same derived counts") {
    Session session = open_session(Supply::finite(2).value());
    const PaletteEntryIndex entry(0);
    CHECK(session.state().supply_status(entry).value().remaining.value() == 2);

    auto first = session.apply(place_square(unit(0, 0)));
    auto second = session.apply(place_square(unit(1, 0)));
    CHECK(bool(first));
    CHECK(bool(second));
    if (!first || !second) {
        return;
    }
    CHECK(session.state().supply_status(entry).value().used == 2);
    CHECK(session.state().supply_status(entry).value().remaining.value() == 0);

    // Exhausted, and the very same zero is what rejects the next command.
    auto spent = session.apply(place_square(unit(2, 2)));
    CHECK(!spent);
    if (!spent) {
        const CandidateError *error = std::get_if<CandidateError>(&spent.error());
        CHECK(error != nullptr && *error == CandidateError::supply_exhausted);
    }

    // Deleting restores exactly one piece...
    CHECK(bool(session.apply(RemoveCommand { first.value() })));
    CHECK(session.state().supply_status(entry).value().used == 1);
    CHECK(session.state().supply_status(entry).value().remaining.value() == 1);

    // ...and undoing that deletion spends it again, with no counter anywhere to
    // fall out of agreement.
    CHECK(session.undo());
    CHECK(session.state().supply_status(entry).value().used == 2);
    CHECK(session.state().supply_status(entry).value().remaining.value() == 0);

    // Undoing the placements themselves walks the count back down.
    CHECK(session.undo());
    CHECK(session.state().supply_status(entry).value().used == 1);
    CHECK(session.state().supply_status(entry).value().remaining.value() == 1);
    CHECK(session.undo());
    CHECK(session.state().supply_status(entry).value().used == 0);
    CHECK(session.state().supply_status(entry).value().remaining.value() == 2);
    CHECK(session.undo_depth() == 0);
}

TEST_CASE("undo restores an entire two-entry palette, region, and holed geometry") {
    // Two prototile identities, two distinct orientations on the second, one
    // finite and one unlimited supply, and a region with a hole: everything a
    // snapshot has to carry.
    std::vector<PaletteEntry> entries;
    entries.push_back(entry_of(
        unit_square(1), Supply::finite(9).value(), { Orientation::reference() }));
    entries.push_back(entry_of(
        bar(2),
        Supply::unlimited(),
        { Orientation::reference(), Orientation::quarter(), Orientation::half(),
          Orientation::three_quarter() }));

    std::vector<Polygon> holes;
    holes.push_back(unit_box(1, 1, 2, 2));
    Session session(State(Level(
        palette_of(std::move(entries)), region_of(unit_box(0, 0, 4, 4), std::move(holes)))));
    CHECK(session.state().palette().entries()[1].orientations().size() == 2);

    const State initial = session.state();
    CHECK(bool(session.apply(place_at(0, 0, unit(0, 0)))));
    CHECK(bool(session.apply(place_at(1, 1, unit(3, 0)))));
    const State after_two = session.state();
    CHECK(bool(session.apply(place_at(1, 0, unit(0, 3)))));

    CHECK(session.undo());
    CHECK(same_state(session.state(), after_two));
    // Spot-checked beyond the helper: the level's own values never moved.
    CHECK(session.state().region().inner_boundaries().size() == 1);
    CHECK(session.state().region().inner_boundaries()[0].vertices().front() == unit(1, 1));
    CHECK(session.state().palette().entries()[1].orientations().size() == 2);
    CHECK(session.state().arrangement().entries()[1].placement.orientation()
        == Orientation::quarter());

    CHECK(session.undo());
    CHECK(session.undo());
    CHECK(same_state(session.state(), initial));
    CHECK(session.undo() == false);
}

// ---------------------------------------------------------------------------
// undo and completion
// ---------------------------------------------------------------------------

TEST_CASE("undoing the final placement of a solved no-hole level unsolves it") {
    Session session(State(Level(
        square_palette(Supply::unlimited()), region_of(unit_box(0, 0, 2, 2), {}))));

    const Point cells[4] = { unit(0, 0), unit(1, 0), unit(0, 1), unit(1, 1) };
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(bool(session.apply(place_square(cells[i]))));
        CHECK(session.state().solved() == (i == 3));
    }
    CHECK(session.undo_depth() == 4);

    CHECK(session.undo());
    // Completion is derived from covered area, so it follows the restored
    // arrangement without any flag to put back.
    CHECK(session.state().solved() == false);
    CHECK(session.state().arrangement().entries().size() == 3);

    // Replacing the last cell solves it again, at a fresh identity.
    auto again = session.apply(place_square(unit(1, 1)));
    CHECK(bool(again));
    if (again) {
        CHECK(again.value() == PlacementId(3));
    }
    CHECK(session.state().solved());
}

TEST_CASE("undoing a deletion from a solved holed level makes it solved again") {
    // A 3 x 3 outer square with a centered 1 x 1 hole: eight unit tiles.
    std::vector<Polygon> holes;
    holes.push_back(unit_box(1, 1, 2, 2));
    Session session(State(Level(
        square_palette(Supply::unlimited()),
        region_of(unit_box(0, 0, 3, 3), std::move(holes)))));

    const Point cells[8] = {
        unit(0, 0), unit(1, 0), unit(2, 0),
        unit(0, 1), unit(2, 1),
        unit(0, 2), unit(1, 2), unit(2, 2),
    };
    std::vector<PlacementId> ids;
    for (std::size_t i = 0; i < 8; ++i) {
        auto applied = session.apply(place_square(cells[i]));
        CHECK(bool(applied));
        if (applied) {
            ids.push_back(applied.value());
        }
    }
    CHECK(ids.size() == 8);
    if (ids.size() != 8) {
        return;
    }
    CHECK(session.state().solved());
    const State solved = session.state();

    // Delete from the middle of storage, which both unsolves the level and
    // disturbs storage order.
    CHECK(bool(session.apply(RemoveCommand { ids[3] })));
    CHECK(session.state().solved() == false);
    CHECK(session.state().arrangement().entries().size() == 7);

    CHECK(session.undo());
    CHECK(session.state().solved());
    CHECK(same_state(session.state(), solved));
    CHECK(session.state().arrangement().entries()[3].id == ids[3]);
    CHECK(session.state().arrangement().entries()[3].placement.translation() == unit(0, 1));
}

// ---------------------------------------------------------------------------
// one branch, no redo
// ---------------------------------------------------------------------------

TEST_CASE("a mutation after undo continues one branch and leaves no redo behind") {
    Session session = open_session(Supply::unlimited());
    auto a = session.apply(place_square(unit(0, 0)));
    CHECK(bool(a));
    if (!a) {
        return;
    }
    const State after_a = session.state();

    auto abandoned = session.apply(place_square(unit(1, 0)));
    CHECK(bool(abandoned));
    if (!abandoned) {
        return;
    }
    const Point abandoned_translation =
        session.state().arrangement().entries().back().placement.translation();
    CHECK(abandoned_translation == unit(1, 0));

    CHECK(session.undo());
    CHECK(same_state(session.state(), after_a));
    CHECK(session.undo_depth() == 1);

    // The discarded future is unreachable. There is no redo api at all, and no
    // observation exposes the abandoned placement.
    auto replacement = session.apply(place_square(unit(2, 2)));
    CHECK(bool(replacement));
    if (replacement) {
        // The freed identity, at the new position.
        CHECK(replacement.value() == abandoned.value());
    }
    CHECK(session.state().arrangement().entries().size() == 2);
    CHECK(session.state().arrangement().entries().back().placement.translation()
        == unit(2, 2));

    // The new mutation pushed onto the restored history normally: exactly one
    // snapshot deeper, not a reopened branch.
    CHECK(session.undo_depth() == 2);
    CHECK(session.undo());
    CHECK(same_state(session.state(), after_a));
    CHECK(session.undo());
    CHECK(session.state().arrangement().entries().empty());
    CHECK(session.undo() == false);
}

// ---------------------------------------------------------------------------
// exactness across a copy
// ---------------------------------------------------------------------------

TEST_CASE("snapshot geometry stays exact at fractional q16.48 translations") {
    Session session = open_session(Supply::unlimited());

    // Neither component is a multiple of Coordinate::SCALE, so no whole-unit
    // rounding could reproduce these values by accident.
    const Point fractional = raw_pt(Coordinate::SCALE / 2 + 7, Coordinate::SCALE / 4 - 11);
    CHECK(fractional.x.raw() % Coordinate::SCALE != 0);
    CHECK(fractional.y.raw() % Coordinate::SCALE != 0);

    auto placed = session.apply(place_square(fractional));
    CHECK(bool(placed));
    if (!placed) {
        return;
    }
    const Polygon::Vertices expected =
        session.state().arrangement().entries().front().placement.footprint().vertices();
    const State after_placement = session.state();

    // Two further mutations, so the fractional entry is copied into a snapshot
    // and back out again.
    CHECK(bool(session.apply(place_square(unit(2, 2)))));
    CHECK(bool(session.apply(place_square(unit(3, 3)))));
    CHECK(session.undo());
    CHECK(session.undo());

    CHECK(same_state(session.state(), after_placement));
    const Placement &restored =
        session.state().arrangement().entries().front().placement;
    // Bit for bit, not merely geometrically similar.
    CHECK(restored.translation() == fractional);
    CHECK(restored.translation().x.raw() == Coordinate::SCALE / 2 + 7);
    CHECK(restored.translation().y.raw() == Coordinate::SCALE / 4 - 11);
    CHECK(restored.footprint().vertices() == expected);
}
