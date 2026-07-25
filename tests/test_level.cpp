#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/Orientation.h"
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
#include "engine/State.h"
#include "engine/Supply.h"

#include <cstddef>
#include <cstdint>
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
using tiles::engine::State;
using tiles::engine::Supply;
using tiles_test::raw_pt;

namespace {

Point unit(std::int64_t p_gx, std::int64_t p_gy) {
    return raw_pt(p_gx * Coordinate::SCALE, p_gy * Coordinate::SCALE);
}

// A point on halves of a game unit, for the fractional completion fixture.
Point half(std::int64_t p_hx, std::int64_t p_hy) {
    return raw_pt(p_hx * (Coordinate::SCALE / 2), p_hy * (Coordinate::SCALE / 2));
}

Polygon poly(std::vector<Point> p_vertices) {
    auto made = Polygon::make(std::move(p_vertices));
    return std::move(made).value();
}

Polygon unit_box(std::int64_t p_min_x, std::int64_t p_min_y, std::int64_t p_max_x, std::int64_t p_max_y) {
    return poly({
        unit(p_min_x, p_min_y),
        unit(p_max_x, p_min_y),
        unit(p_max_x, p_max_y),
        unit(p_min_x, p_max_y),
    });
}

Prototile prototile_of(std::uint64_t p_id, Polygon p_polygon) {
    return std::move(Prototile::make(PrototileId(p_id), std::move(p_polygon))).value();
}

// A 1 x 1 game-unit square: the completion fixtures' tile.
Prototile unit_square(std::uint64_t p_id) {
    return prototile_of(p_id, unit_box(0, 0, 1, 1));
}

// A 2 x 1 game-unit bar.
Prototile bar(std::uint64_t p_id) {
    return prototile_of(p_id, unit_box(0, 0, 2, 1));
}

// A half-unit square, so a fixture can prove exact area equality at
// translations that are not whole game units.
Prototile half_square(std::uint64_t p_id) {
    return prototile_of(p_id, poly({ half(0, 0), half(1, 0), half(1, 1), half(0, 1) }));
}

PaletteEntry entry_of(const Prototile &p_prototile, Supply p_supply) {
    return std::move(
        PaletteEntry::make(p_prototile, p_supply, { Orientation::reference() }).value());
}

Palette palette_of(std::vector<PaletteEntry> p_entries) {
    return std::move(Palette::make(std::move(p_entries)).value());
}

Palette unit_square_palette(Supply p_supply) {
    std::vector<PaletteEntry> entries;
    entries.push_back(entry_of(unit_square(1), p_supply));
    return palette_of(std::move(entries));
}

Region region_of(Polygon p_outer, std::vector<Polygon> p_holes) {
    auto made = Region::make(std::move(p_outer), std::move(p_holes));
    return std::move(made).value();
}

PlaceCommand place_at(std::size_t p_entry, std::size_t p_orientation, Point p_translation) {
    return PlaceCommand {
        PaletteEntryIndex(p_entry),
        PaletteOrientationIndex(p_orientation),
        p_translation,
    };
}

bool is_outside_region(const PlaceCommandError &p_error) {
    const RegionPlacementError *found = std::get_if<RegionPlacementError>(&p_error);
    return found != nullptr && *found == RegionPlacementError::outside_region;
}

bool is_outside_region(const MateCommandError &p_error) {
    const RegionPlacementError *found = std::get_if<RegionPlacementError>(&p_error);
    return found != nullptr && *found == RegionPlacementError::outside_region;
}

bool same_geometry(const Placement &p_lhs, const Placement &p_rhs) {
    return p_lhs.prototile().id() == p_rhs.prototile().id()
        && p_lhs.orientation() == p_rhs.orientation()
        && p_lhs.translation() == p_rhs.translation()
        && p_lhs.footprint().vertices() == p_rhs.footprint().vertices();
}

// A 4 x 4 game-unit region with one 1 x 1 hole at (1, 1)-(2, 2).
Region holed_region() {
    std::vector<Polygon> holes;
    holes.push_back(unit_box(1, 1, 2, 2));
    return region_of(unit_box(0, 0, 4, 4), std::move(holes));
}

State holed_state() {
    return State(Level(unit_square_palette(Supply::unlimited()), holed_region()));
}

} // namespace

// ---------------------------------------------------------------------------
// level
// ---------------------------------------------------------------------------

TEST_CASE("a level owns and exposes one palette and one region") {
    Level level(unit_square_palette(Supply::unlimited()), holed_region());

    CHECK(level.palette().order() == 1);
    CHECK(level.palette().entries()[0].prototile().id() == PrototileId(1));
    CHECK(level.region().outer_boundary().vertices().size() == 4);
    CHECK(level.region().inner_boundaries().size() == 1);
    CHECK(level.region().inner_boundaries()[0].vertices().front() == unit(1, 1));
}

// ---------------------------------------------------------------------------
// direct-placement preview
// ---------------------------------------------------------------------------

TEST_CASE("place preview accepts a contained footprint and changes nothing") {
    State state = holed_state();
    const State &observed = state;

    auto previewed = observed.preview(place_at(0, 0, unit(2, 2)));
    CHECK(bool(previewed));
    if (previewed) {
        CHECK(previewed.value().prototile().id() == PrototileId(1));
        CHECK(previewed.value().translation() == unit(2, 2));
        CHECK(previewed.value().footprint().vertices().front() == unit(2, 2));
    }

    // Repeated preview against an unmodified state is identical, and neither
    // preview allocated an id or touched an entry.
    auto again = observed.preview(place_at(0, 0, unit(2, 2)));
    CHECK(bool(again));
    if (previewed && again) {
        CHECK(same_geometry(previewed.value(), again.value()));
    }
    CHECK(state.arrangement().entries().empty());
    CHECK(state.arrangement().next_id().value() == PlacementId(0));
    CHECK(state.solved() == false);
}

TEST_CASE("place preview rejects footprints beyond the outer boundary") {
    State state = holed_state();

    // Wholly outside, partly outside, and one raw unit outside.
    auto far = state.preview(place_at(0, 0, unit(9, 9)));
    CHECK(!far);
    if (!far) {
        CHECK(is_outside_region(far.error()));
    }

    auto straddling = state.preview(place_at(0, 0, unit(-1, 0)));
    CHECK(!straddling);
    if (!straddling) {
        CHECK(is_outside_region(straddling.error()));
    }

    auto barely = state.preview(
        place_at(0, 0, raw_pt(3 * Coordinate::SCALE + 1, Coordinate::SCALE * 2)));
    CHECK(!barely);
    if (!barely) {
        CHECK(is_outside_region(barely.error()));
    }

    CHECK(state.arrangement().entries().empty());
}

TEST_CASE("place preview rejects footprints entering a hole and admits boundary contact") {
    State state = holed_state();

    // Exactly the hole.
    auto in_hole = state.preview(place_at(0, 0, unit(1, 1)));
    CHECK(!in_hole);
    if (!in_hole) {
        CHECK(is_outside_region(in_hole.error()));
    }

    // Half in the hole.
    auto partly = state.preview(place_at(
        0, 0, raw_pt(Coordinate::SCALE / 2 + Coordinate::SCALE, Coordinate::SCALE)));
    CHECK(!partly);
    if (!partly) {
        CHECK(is_outside_region(partly.error()));
    }

    // Flush against the hole's right edge, and against the outer boundary: both
    // are legal closed contact.
    CHECK(bool(state.preview(place_at(0, 0, unit(2, 1)))));
    CHECK(bool(state.preview(place_at(0, 0, unit(0, 0)))));
    CHECK(bool(state.preview(place_at(0, 0, unit(3, 3)))));
}

TEST_CASE("place preview keeps candidate, placement, region, then arrangement precedence") {
    // Every fixture below makes several failures simultaneously available.
    State state = holed_state();

    // Candidate resolution answers before anything geometric, even though the
    // translation is far outside the region.
    auto bad_entry = state.preview(place_at(9, 0, unit(40, 40)));
    CHECK(!bad_entry);
    if (!bad_entry) {
        CHECK(std::get_if<CandidateError>(&bad_entry.error()) != nullptr);
        CHECK(std::get_if<RegionPlacementError>(&bad_entry.error()) == nullptr);
    }

    auto bad_orientation = state.preview(place_at(0, 9, unit(40, 40)));
    CHECK(!bad_orientation);
    if (!bad_orientation) {
        CHECK(std::get_if<CandidateError>(&bad_orientation.error()) != nullptr);
    }

    // Footprint construction answers before the region: an overflowing
    // translation has no footprint to test containment against.
    auto overflow = state.preview(place_at(0, 0, raw_pt(INT64_MAX, 0)));
    CHECK(!overflow);
    if (!overflow) {
        const PlacementError *error = std::get_if<PlacementError>(&overflow.error());
        CHECK(error != nullptr);
        if (error != nullptr) {
            CHECK(*error == PlacementError::footprint_overflow);
        }
    }

    // Supply exhaustion still answers before the region.
    State scarce(Level(unit_square_palette(Supply::finite(1).value()), holed_region()));
    CHECK(bool(scarce.apply(place_at(0, 0, unit(0, 0)))));
    auto spent = scarce.preview(place_at(0, 0, unit(40, 40)));
    CHECK(!spent);
    if (!spent) {
        CHECK(std::get_if<CandidateError>(&spent.error()) != nullptr);
    }

    // The region answers before the arrangement: this candidate simultaneously
    // overlaps the placed tile and leaves the region.
    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));
    auto both = state.preview(place_at(0, 0, raw_pt(-(Coordinate::SCALE / 2), 0)));
    CHECK(!both);
    if (!both) {
        CHECK(is_outside_region(both.error()));
        CHECK(std::get_if<ArrangementError>(&both.error()) == nullptr);
    }

    // And an overlap inside the region is still an ArrangementError.
    auto overlapped =
        state.preview(place_at(0, 0, raw_pt(Coordinate::SCALE / 2, 0)));
    CHECK(!overlapped);
    if (!overlapped) {
        const ArrangementError *error = std::get_if<ArrangementError>(&overlapped.error());
        CHECK(error != nullptr);
        if (error != nullptr) {
            CHECK(error->code == ArrangementErrorCode::interior_overlap);
            CHECK(error->conflicting_placement.value() == PlacementId(0));
        }
    }
}

TEST_CASE("place preview and an immediate apply agree exactly") {
    State state = holed_state();

    auto previewed = state.preview(place_at(0, 0, unit(2, 1)));
    CHECK(bool(previewed));
    if (!previewed) {
        return;
    }

    auto applied = state.apply(place_at(0, 0, unit(2, 1)));
    CHECK(bool(applied));
    if (!applied) {
        return;
    }

    const Entry &stored = state.arrangement().entries().back();
    CHECK(stored.id == applied.value());
    CHECK(same_geometry(stored.placement, previewed.value()));
}

TEST_CASE("a failed place apply leaves entries, ids, supply, and completion unchanged") {
    State state(Level(unit_square_palette(Supply::finite(3).value()), holed_region()));
    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));
    CHECK(bool(state.apply(place_at(0, 0, unit(1, 0)))));

    const std::size_t before_size = state.arrangement().entries().size();
    const PlacementId before_next = state.arrangement().next_id().value();
    const bool before_solved = state.solved();

    // One failure of each published alternative.
    CHECK(!state.apply(place_at(9, 0, unit(0, 0))));
    CHECK(!state.apply(place_at(0, 9, unit(0, 0))));
    CHECK(!state.apply(place_at(0, 0, raw_pt(INT64_MAX, 0))));
    CHECK(!state.apply(place_at(0, 0, unit(1, 1))));
    CHECK(!state.apply(place_at(0, 0, unit(9, 9))));
    CHECK(!state.apply(place_at(0, 0, unit(0, 0))));

    CHECK(state.arrangement().entries().size() == before_size);
    CHECK(state.arrangement().next_id().value() == before_next);
    CHECK(state.solved() == before_solved);
    // The third configured piece was never spent by a rejected command.
    CHECK(bool(state.apply(place_at(0, 0, unit(2, 0)))));
    auto exhausted = state.apply(place_at(0, 0, unit(3, 0)));
    CHECK(!exhausted);
    if (!exhausted) {
        CHECK(std::get_if<CandidateError>(&exhausted.error()) != nullptr);
    }
}

// ---------------------------------------------------------------------------
// region-aware mating
// ---------------------------------------------------------------------------

TEST_CASE("both mating previews reject a derived placement outside the region") {
    // A region exactly covering two tiles, with one tile already placed at its
    // left half. Every mating below derives a legal, non-overlapping placement
    // that simply leaves the region.
    State state(Level(unit_square_palette(Supply::unlimited()), region_of(unit_box(0, 0, 2, 1), {})));
    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    // Edge 3 of the anchor runs (0,1)->(0,0); mating it with candidate edge 1
    // ((1,0)->(1,1)) drops a tile to the left of the region.
    const MateFullEdgesCommand to_the_left {
        anchor.value(),
        EdgeIndex(3),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(1),
    };
    auto edges = state.preview(to_the_left);
    CHECK(!edges);
    if (!edges) {
        CHECK(is_outside_region(edges.error()));
    }

    // Anchor vertex 3 is (0,1); mating candidate vertex 1 ((1,0)) onto it puts
    // the tile above and to the left of the region.
    const MateVerticesCommand above_left {
        anchor.value(),
        VertexIndex(3),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        VertexIndex(1),
    };
    auto vertices = state.preview(above_left);
    CHECK(!vertices);
    if (!vertices) {
        CHECK(is_outside_region(vertices.error()));
    }

    // Both applies route through the same previews and reject identically.
    CHECK(!state.apply(to_the_left));
    CHECK(!state.apply(above_left));
    CHECK(state.arrangement().entries().size() == 1);
    CHECK(state.arrangement().next_id().value() == PlacementId(1));

    // The mating that stays inside the region still succeeds, and stores
    // exactly what its preview returned.
    const MateFullEdgesCommand to_the_right {
        anchor.value(),
        EdgeIndex(1),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(3),
    };
    auto previewed = state.preview(to_the_right);
    CHECK(bool(previewed));
    auto applied = state.apply(to_the_right);
    CHECK(bool(applied));
    if (previewed && applied) {
        CHECK(same_geometry(state.arrangement().entries().back().placement, previewed.value()));
        CHECK(state.arrangement().entries().back().id == applied.value());
    }
    CHECK(state.solved());
}

TEST_CASE("mating reports an available core join error before the region") {
    // The region holds exactly one unit tile, which is already placed. Mating
    // the bar onto the anchor's origin vertex derives a placement that both
    // overlaps the anchor and leaves the region; the completed core precedence
    // answers first, deliberately.
    std::vector<PaletteEntry> entries;
    entries.push_back(entry_of(unit_square(1), Supply::unlimited()));
    entries.push_back(entry_of(bar(2), Supply::unlimited()));
    State state(Level(palette_of(std::move(entries)), region_of(unit_box(0, 0, 1, 1), {})));

    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    const MateVerticesCommand onto_anchor {
        anchor.value(),
        VertexIndex(0),
        PaletteEntryIndex(1),
        PaletteOrientationIndex(0),
        VertexIndex(0),
    };

    auto previewed = state.preview(onto_anchor);
    CHECK(!previewed);
    if (!previewed) {
        const JoinError *error = std::get_if<JoinError>(&previewed.error());
        CHECK(error != nullptr);
        if (error != nullptr) {
            CHECK(error->code == JoinErrorCode::interior_overlap);
            CHECK(error->conflicting_placement.value() == anchor.value());
        }
        CHECK(std::get_if<RegionPlacementError>(&previewed.error()) == nullptr);
    }

    // Candidate resolution still precedes both.
    auto bad_entry = state.preview(MateVerticesCommand {
        anchor.value(),
        VertexIndex(0),
        PaletteEntryIndex(9),
        PaletteOrientationIndex(0),
        VertexIndex(0),
    });
    CHECK(!bad_entry);
    if (!bad_entry) {
        CHECK(std::get_if<CandidateError>(&bad_entry.error()) != nullptr);
    }

    CHECK(state.arrangement().entries().size() == 1);
    CHECK(state.arrangement().next_id().value() == PlacementId(1));
}

// ---------------------------------------------------------------------------
// exact completion
// ---------------------------------------------------------------------------

TEST_CASE("a no-hole region becomes solved exactly on its final tile") {
    State state(Level(unit_square_palette(Supply::unlimited()), region_of(unit_box(0, 0, 2, 2), {})));
    CHECK(state.solved() == false);

    const Point cells[4] = { unit(0, 0), unit(1, 0), unit(0, 1), unit(1, 1) };
    for (std::size_t i = 0; i < 4; ++i) {
        auto applied = state.apply(place_at(0, 0, cells[i]));
        CHECK(bool(applied));
        // Not solved until the covered area equals the region's area exactly.
        CHECK(state.solved() == (i == 3));
    }
}

TEST_CASE("a holed region becomes solved exactly on its final tile") {
    // A 3 x 3 outer square with a centered 1 x 1 hole: eight unit tiles.
    std::vector<Polygon> holes;
    holes.push_back(unit_box(1, 1, 2, 2));
    State state(Level(
        unit_square_palette(Supply::unlimited()),
        region_of(unit_box(0, 0, 3, 3), std::move(holes))));

    const Point cells[8] = {
        unit(0, 0), unit(1, 0), unit(2, 0),
        unit(0, 1), unit(2, 1),
        unit(0, 2), unit(1, 2), unit(2, 2),
    };
    for (std::size_t i = 0; i < 8; ++i) {
        auto applied = state.apply(place_at(0, 0, cells[i]));
        CHECK(bool(applied));
        CHECK(state.solved() == (i == 7));
    }
    CHECK(state.arrangement().entries().size() == 8);

    // The excluded hole can never be covered, so the solved state is stable.
    auto into_hole = state.apply(place_at(0, 0, unit(1, 1)));
    CHECK(!into_hole);
    if (!into_hole) {
        CHECK(is_outside_region(into_hole.error()));
    }
    CHECK(state.solved());
}

TEST_CASE("a multi-hole region becomes solved exactly on its final tile") {
    // A 5 x 3 outer rectangle with two separated 1 x 1 holes: thirteen tiles.
    std::vector<Polygon> holes;
    holes.push_back(unit_box(1, 1, 2, 2));
    holes.push_back(unit_box(3, 1, 4, 2));
    State state(Level(
        unit_square_palette(Supply::unlimited()),
        region_of(unit_box(0, 0, 5, 3), std::move(holes))));

    const Point cells[13] = {
        unit(0, 0), unit(1, 0), unit(2, 0), unit(3, 0), unit(4, 0),
        unit(0, 1), unit(2, 1), unit(4, 1),
        unit(0, 2), unit(1, 2), unit(2, 2), unit(3, 2), unit(4, 2),
    };
    for (std::size_t i = 0; i < 13; ++i) {
        auto applied = state.apply(place_at(0, 0, cells[i]));
        CHECK(bool(applied));
        CHECK(state.solved() == (i == 12));
    }
}

TEST_CASE("a legal but incomplete arrangement is never solved") {
    State state(Level(unit_square_palette(Supply::unlimited()), region_of(unit_box(0, 0, 4, 4), {})));

    // Fifteen of the sixteen cells: contained, disjoint, and one unit short.
    std::size_t placed = 0;
    for (std::int64_t x = 0; x < 4; ++x) {
        for (std::int64_t y = 0; y < 4; ++y) {
            if (x == 3 && y == 3) {
                continue;
            }
            CHECK(bool(state.apply(place_at(0, 0, unit(x, y)))));
            ++placed;
            CHECK(state.solved() == false);
        }
    }
    CHECK(placed == 15);
    CHECK(bool(state.apply(place_at(0, 0, unit(3, 3)))));
    CHECK(state.solved());
}

TEST_CASE("exact completion works at fractional q16.48 translations") {
    // Four half-unit squares exactly cover one square game unit, at
    // translations that are not whole game units.
    std::vector<PaletteEntry> entries;
    entries.push_back(entry_of(half_square(1), Supply::unlimited()));
    State state(Level(palette_of(std::move(entries)), region_of(unit_box(0, 0, 1, 1), {})));

    const Point cells[4] = { half(0, 0), half(1, 0), half(0, 1), half(1, 1) };
    for (std::size_t i = 0; i < 4; ++i) {
        auto applied = state.apply(place_at(0, 0, cells[i]));
        CHECK(bool(applied));
        CHECK(state.solved() == (i == 3));
    }
    // The final two translations were genuinely fractional game units.
    CHECK(half(1, 1).x.raw() % Coordinate::SCALE != 0);
}
