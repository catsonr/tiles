#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/Region.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/ExactInteger.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "engine/Level.h"
#include "engine/Palette.h"
#include "engine/State.h"
#include "engine/Supply.h"

#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

using namespace tiles;
using tiles::engine::Level;
using tiles::engine::Palette;
using tiles::engine::PaletteEntry;
using tiles::engine::PaletteEntryIndex;
using tiles::engine::State;
using tiles::engine::Supply;
using tiles::engine::SupplyStatus;
using tiles_test::raw_pt;

namespace {

Point unit(std::int64_t p_gx, std::int64_t p_gy) {
    return raw_pt(p_gx * Coordinate::SCALE, p_gy * Coordinate::SCALE);
}

Prototile square(std::uint64_t p_id) {
    auto polygon = Polygon::make({ unit(0, 0), unit(2, 0), unit(2, 2), unit(0, 2) });
    return std::move(Prototile::make(PrototileId(p_id), std::move(polygon).value())).value();
}

Palette one_square_palette() {
    std::vector<PaletteEntry> entries;
    entries.push_back(std::move(
        PaletteEntry::make(square(1), Supply::unlimited(), { Orientation::reference() }).value()));
    return std::move(Palette::make(std::move(entries)).value());
}

// One 8 x 8 game-unit region with a single 2 x 2 hole, big enough for the
// fixtures below and specific enough to observe through the level.
Region small_region() {
    auto outer = Polygon::make({ unit(0, 0), unit(8, 0), unit(8, 8), unit(0, 8) });
    auto hole = Polygon::make({ unit(4, 4), unit(6, 4), unit(6, 6), unit(4, 6) });
    std::vector<Polygon> holes;
    holes.push_back(std::move(hole).value());
    auto region = Region::make(std::move(outer).value(), std::move(holes));
    return std::move(region).value();
}

Level small_level() {
    return Level(one_square_palette(), small_region());
}

} // namespace

TEST_CASE("state owns and exposes its level") {
    State state(small_level());
    CHECK(state.level().palette().order() == 1);
    CHECK(state.level().region().inner_boundaries().size() == 1);
}

TEST_CASE("palette and region convenience views name the level's own values") {
    State state(small_level());
    // Not copies: the convenience accessors address the level-owned values.
    CHECK(&state.palette() == &state.level().palette());
    CHECK(&state.region() == &state.level().region());
    CHECK(state.palette().entries()[0].prototile().id() == PrototileId(1));
    // 8 x 8 minus a 2 x 2 hole is 60 square game units; doubled area is 120 of
    // them, expressed exactly in q16.48 raw units.
    const Int256 expected = Int256::multiply(
        static_cast<__int128>(120) * Coordinate::SCALE, Coordinate::SCALE);
    CHECK(state.region().doubled_area() == expected);
}

TEST_CASE("a newly constructed state owns an empty arrangement") {
    State state(small_level());
    CHECK(state.arrangement().entries().empty());
    CHECK(state.arrangement().next_id().value() == PlacementId(0));
    // A valid region has positive area, so nothing is covered and nothing is
    // solved yet.
    CHECK(state.solved() == false);
}

TEST_CASE("no production construction path accepts a prebuilt arrangement") {
    // The only public constructor takes a level. A palette-and-arrangement
    // constructor could inject placements that were never proven against the
    // palette, its supply, or the region, so it does not exist.
    static_assert(
        std::is_constructible_v<State, Level>,
        "State must be constructible from a level");
    static_assert(
        !std::is_constructible_v<State, Palette, Arrangement>,
        "State must not accept an arbitrary palette and arrangement");
    static_assert(
        !std::is_constructible_v<State, Level, Arrangement>,
        "State must not accept a prebuilt arrangement alongside its level");
    static_assert(
        !std::is_constructible_v<State, Palette>,
        "State must not be constructible from a palette alone");
    static_assert(
        !std::is_default_constructible_v<State>,
        "State must not be default constructible");
    CHECK(true);
}

TEST_CASE("the test-only seam presets the allocator of an empty arrangement") {
    State state = State::testing_with_empty_arrangement(
        small_level(), Arrangement::testing_with_next_id(UINT64_MAX));
    CHECK(state.arrangement().entries().empty());
    CHECK(state.arrangement().next_id().value() == PlacementId(UINT64_MAX));
    CHECK(state.palette().order() == 1);
}

TEST_CASE("state exposes exact core polygon vertices through read-only chains") {
    State state(small_level());
    const Polygon::Vertices &vertices =
        state.palette().entries()[0].prototile().polygon().vertices();
    CHECK(vertices.size() == 4);
    // The normalized square keeps its lexicographically smallest vertex at the
    // local origin, and coordinates stay exact q16.48 points.
    CHECK(vertices.front() == raw_pt(0, 0));
    CHECK(vertices[1] == unit(2, 0));

    // The same is true through the region.
    const Polygon::Vertices &outer = state.region().outer_boundary().vertices();
    CHECK(outer.size() == 4);
    CHECK(outer.front() == unit(0, 0));
}

TEST_CASE("supply status is a const observation returning a value, not a stored counter") {
    const State state(small_level());

    // Reachable through a const state, and it hands back a value rather than a
    // reference into anything the caller could then write through.
    static_assert(
        std::is_same_v<
            decltype(std::declval<const State &>().supply_status(PaletteEntryIndex(0))),
            std::optional<SupplyStatus>>,
        "supply_status() must return an owned optional value");

    auto status = state.supply_status(PaletteEntryIndex(0));
    CHECK(status.has_value());
    if (status.has_value()) {
        CHECK(status.value().used == 0);
        // The one palette entry is unlimited, so nothing remains to report.
        CHECK(status.value().remaining.has_value() == false);
    }
    // An index past the palette is the only failure a constructible state has.
    CHECK(state.supply_status(PaletteEntryIndex(1)).has_value() == false);
}

TEST_CASE("no history lives inside a state") {
    // Undo belongs to the session above the state, not to the exact value: a
    // State that contained its own history would recursively contain snapshots
    // of itself.
    static_assert(
        std::is_copy_constructible_v<State>,
        "State must be copyable so a session can snapshot it by value");
    static_assert(
        std::is_move_assignable_v<State>,
        "State must be move-assignable so a session can install a candidate");
    CHECK(true);
}

TEST_CASE("state exposes no mutable level, palette, region, or arrangement reference") {
    static_assert(
        std::is_same_v<decltype(std::declval<State &>().level()), const Level &>,
        "level() must expose only a const reference");
    static_assert(
        std::is_same_v<decltype(std::declval<State &>().palette()), const Palette &>,
        "palette() must expose only a const reference");
    static_assert(
        std::is_same_v<decltype(std::declval<State &>().region()), const Region &>,
        "region() must expose only a const reference");
    static_assert(
        std::is_same_v<decltype(std::declval<State &>().arrangement()), const Arrangement &>,
        "arrangement() must expose only a const reference");
    CHECK(true);
}
