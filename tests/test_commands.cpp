#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/geometry/Alignment.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "content/PrototileCatalog.h"
#include "core/Region.h"
#include "engine/Commands.h"
#include "engine/Level.h"
#include "engine/Palette.h"
#include "engine/State.h"
#include "engine/Supply.h"
#include "engine/TetrominoState.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
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
using tiles::engine::RemoveCommand;
using tiles::engine::State;
using tiles::engine::Supply;
using tiles::engine::SupplyStatus;
using tiles_test::raw_pt;
using tiles_test::reference_orientation;

namespace {

const Orientation R = Orientation::reference();
const Orientation Q = Orientation::quarter();
const Orientation H = Orientation::half();
const Orientation T = Orientation::three_quarter();

// A point on whole game-unit coordinates. One game unit is Coordinate::SCALE raw
// units, so fixtures stay legible while translations remain exact q16.48 values.
Point unit(std::int64_t p_gx, std::int64_t p_gy) {
    return raw_pt(p_gx * Coordinate::SCALE, p_gy * Coordinate::SCALE);
}

// A 2x2 square: achiral, so its four quarter turns collapse to one distinct
// oriented value.
Prototile square(std::uint64_t p_id) {
    auto polygon = Polygon::make({ unit(0, 0), unit(2, 0), unit(2, 2), unit(0, 2) });
    return std::move(Prototile::make(PrototileId(p_id), std::move(polygon).value())).value();
}

// The temporary tetromino construction fixture, built from the canonical
// catalog exactly as the application builds it. Returns nothing if either
// construction fails, so a caller inspects one optional rather than two results.
std::optional<State> tetromino_state() {
    auto catalog = content::make_canonical_prototile_catalog();
    if (!catalog) {
        return std::nullopt;
    }
    auto built = tiles::engine::make_tetromino_state(catalog.value());
    if (!built) {
        return std::nullopt;
    }
    return std::move(built).value();
}

// A 4x1 bar: two distinct orientations under the quarter turns.
Prototile bar(std::uint64_t p_id) {
    auto polygon = Polygon::make({ unit(0, 0), unit(4, 0), unit(4, 1), unit(0, 1) });
    return std::move(Prototile::make(PrototileId(p_id), std::move(polygon).value())).value();
}

PaletteEntry make_entry(
    const Prototile &p_prototile, Supply p_supply, std::vector<Orientation> p_requested) {
    return std::move(PaletteEntry::make(p_prototile, p_supply, std::move(p_requested)).value());
}

Palette palette_of(std::vector<PaletteEntry> p_entries) {
    return std::move(Palette::make(std::move(p_entries)).value());
}

// One region large enough that no fixture below is ever rejected for leaving
// it. Region legality has its own dedicated coverage; these command tests are
// about candidate resolution, supply, exact geometry, and mating.
Region big_region() {
    auto outer = Polygon::make({ unit(-32, -32), unit(32, -32), unit(32, 32), unit(-32, 32) });
    auto region = Region::make(std::move(outer).value(), {});
    return std::move(region).value();
}

Level level_of(Palette p_palette) {
    return Level(std::move(p_palette), big_region());
}

// A region exactly the size of one 2x2 square, so a single placement solves it
// and a single deletion unsolves it.
Region tight_region() {
    auto outer = Polygon::make({ unit(0, 0), unit(2, 0), unit(2, 2), unit(0, 2) });
    auto region = Region::make(std::move(outer).value(), {});
    return std::move(region).value();
}

Palette square_palette(Supply p_supply) {
    std::vector<PaletteEntry> entries;
    entries.push_back(make_entry(square(1), p_supply, { R }));
    return palette_of(std::move(entries));
}

// One 2x2 square entry with the reference orientation only, over an empty
// arrangement: orientation index 1 is therefore always out of range.
State square_state(Supply p_supply) {
    return State(level_of(square_palette(p_supply)));
}

PlaceCommand place_at(
    std::size_t p_entry, std::size_t p_orientation, Point p_translation) {
    return PlaceCommand {
        PaletteEntryIndex(p_entry),
        PaletteOrientationIndex(p_orientation),
        p_translation,
    };
}

bool is_candidate(const PlaceCommandError &p_error, CandidateError p_expected) {
    const CandidateError *found = std::get_if<CandidateError>(&p_error);
    return found != nullptr && *found == p_expected;
}

bool is_candidate(const MateCommandError &p_error, CandidateError p_expected) {
    const CandidateError *found = std::get_if<CandidateError>(&p_error);
    return found != nullptr && *found == p_expected;
}

bool has_vertex(const Polygon &p_polygon, Point p_point) {
    for (const Point &v : p_polygon.vertices()) {
        if (v == p_point) {
            return true;
        }
    }
    return false;
}

// Locate the edge running from p_from to p_to, so tests never depend on where
// canonicalization places index zero.
EdgeIndex find_edge(const Polygon &p_polygon, Point p_from, Point p_to) {
    const Polygon::Vertices &v = p_polygon.vertices();
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == p_from && v[(i + 1) % v.size()] == p_to) {
            return EdgeIndex(i);
        }
    }
    // The test asked for an edge that does not exist; fail loudly.
    return EdgeIndex(v.size());
}

// Exact axis-aligned extent in raw lattice units, used to identify a rotated
// variant without depending on the compiler's turn direction.
std::pair<std::int64_t, std::int64_t> extent(const Polygon &p_polygon) {
    const Polygon::Vertices &v = p_polygon.vertices();
    std::int64_t min_x = v.front().x.raw();
    std::int64_t max_x = min_x;
    std::int64_t min_y = v.front().y.raw();
    std::int64_t max_y = min_y;
    for (const Point &p : v) {
        min_x = p.x.raw() < min_x ? p.x.raw() : min_x;
        max_x = p.x.raw() > max_x ? p.x.raw() : max_x;
        min_y = p.y.raw() < min_y ? p.y.raw() : min_y;
        max_y = p.y.raw() > max_y ? p.y.raw() : max_y;
    }
    return { max_x - min_x, max_y - min_y };
}

} // namespace

// ---------------------------------------------------------------------------
// strong indices and lookup
// ---------------------------------------------------------------------------

TEST_CASE("palette indices retain their exact values and compare by value") {
    CHECK(PaletteEntryIndex(0).value() == 0);
    CHECK(PaletteEntryIndex(37).value() == 37);
    CHECK(PaletteOrientationIndex(0).value() == 0);
    CHECK(PaletteOrientationIndex(3).value() == 3);

    CHECK(PaletteEntryIndex(4) == PaletteEntryIndex(4));
    CHECK(PaletteEntryIndex(4) != PaletteEntryIndex(5));
    CHECK(PaletteOrientationIndex(1) == PaletteOrientationIndex(1));
    CHECK(PaletteOrientationIndex(1) != PaletteOrientationIndex(2));
}

TEST_CASE("palette indices are strong, non-interchangeable types") {
    // Neither index is implicitly constructible from a bare size_t...
    static_assert(
        !std::is_convertible_v<std::size_t, PaletteEntryIndex>,
        "PaletteEntryIndex must not be implicitly constructible from size_t");
    static_assert(
        !std::is_convertible_v<std::size_t, PaletteOrientationIndex>,
        "PaletteOrientationIndex must not be implicitly constructible from size_t");
    // ...though both remain explicitly constructible from one.
    static_assert(
        std::is_constructible_v<PaletteEntryIndex, std::size_t>,
        "PaletteEntryIndex must be explicitly constructible from size_t");
    static_assert(
        std::is_constructible_v<PaletteOrientationIndex, std::size_t>,
        "PaletteOrientationIndex must be explicitly constructible from size_t");
    // ...and neither converts into the other in either direction.
    static_assert(
        !std::is_same_v<PaletteEntryIndex, PaletteOrientationIndex>,
        "the two palette indices must be distinct types");
    static_assert(
        !std::is_constructible_v<PaletteEntryIndex, PaletteOrientationIndex>,
        "a palette orientation index must not become an entry index");
    static_assert(
        !std::is_constructible_v<PaletteOrientationIndex, PaletteEntryIndex>,
        "a palette entry index must not become an orientation index");
    CHECK(true);
}

TEST_CASE("an out-of-range palette entry is rejected and changes nothing") {
    State state = square_state(Supply::unlimited());
    CHECK(state.palette().order() == 1);

    auto applied = state.apply(place_at(1, 0, unit(0, 0)));
    CHECK(!applied);
    if (!applied) {
        CHECK(is_candidate(applied.error(), CandidateError::palette_entry_out_of_range));
    }
    CHECK(state.arrangement().entries().empty());
    CHECK(state.arrangement().next_id().value() == PlacementId(0));
}

TEST_CASE("an out-of-range orientation of a valid entry is rejected and changes nothing") {
    // The square's four requested quarter turns collapse onto one distinct
    // oriented value, so index 1 is out of range even though four angles were
    // authored: the index selects distinct orientations, never the equivalence
    // labels recorded inside one of them.
    std::vector<PaletteEntry> entries;
    entries.push_back(make_entry(square(1), Supply::unlimited(), { R, Q, H, T }));
    State state(level_of(palette_of(std::move(entries))));
    CHECK(state.palette().entries()[0].orientations().size() == 1);
    CHECK(state.palette().entries()[0].orientations()[0].equivalent_orientations().size() == 4);

    auto applied = state.apply(place_at(0, 1, unit(0, 0)));
    CHECK(!applied);
    if (!applied) {
        CHECK(is_candidate(applied.error(), CandidateError::orientation_out_of_range));
    }
    CHECK(state.arrangement().entries().empty());
    CHECK(state.arrangement().next_id().value() == PlacementId(0));
}

// ---------------------------------------------------------------------------
// exact placement
// ---------------------------------------------------------------------------

TEST_CASE("placing a valid palette candidate at the exact origin succeeds with id zero") {
    State state = square_state(Supply::unlimited());
    auto applied = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(applied));
    if (!applied) {
        return;
    }
    CHECK(applied.value() == PlacementId(0));
    CHECK(state.arrangement().entries().size() == 1);
    CHECK(state.arrangement().entries().front().id == PlacementId(0));
    CHECK(state.arrangement().entries().front().placement.translation() == unit(0, 0));
}

TEST_CASE("a nonzero exact q16.48 translation is preserved bit for bit") {
    State state = square_state(Supply::unlimited());
    const Point t = raw_pt(3 * Coordinate::SCALE + 7, -2 * Coordinate::SCALE - 11);

    auto applied = state.apply(place_at(0, 0, t));
    CHECK(bool(applied));
    if (!applied) {
        return;
    }
    const Placement &placed = state.arrangement().entries().front().placement;
    CHECK(placed.translation() == t);
    CHECK(placed.translation().x.raw() == 3 * Coordinate::SCALE + 7);
    CHECK(placed.translation().y.raw() == -2 * Coordinate::SCALE - 11);
    // Every oriented canonical polygon starts at the local origin, so the
    // footprint's first vertex is exactly the supplied translation.
    CHECK(placed.footprint().vertices().front() == t);
}

TEST_CASE("a fractional translation is accepted unchanged, proving no integer grid") {
    State state = square_state(Supply::unlimited());
    // Neither component is a multiple of Coordinate::SCALE: this translation is
    // unreachable on any cell grid the frontend might later impose.
    const Point t = raw_pt(Coordinate::SCALE / 2 + 1, -(Coordinate::SCALE / 4) - 3);
    CHECK(t.x.raw() % Coordinate::SCALE != 0);
    CHECK(t.y.raw() % Coordinate::SCALE != 0);

    auto applied = state.apply(place_at(0, 0, t));
    CHECK(bool(applied));
    if (!applied) {
        return;
    }
    const Placement &placed = state.arrangement().entries().front().placement;
    // Not rounded, snapped, or clamped toward a whole game unit.
    CHECK(placed.translation() == t);
    CHECK(placed.footprint().vertices().front() == t);
}

TEST_CASE("the stored placement carries the selected prototile id and orientation") {
    std::vector<PaletteEntry> entries;
    entries.push_back(make_entry(bar(9), Supply::unlimited(), { R, Q, H, T }));
    State state(level_of(palette_of(std::move(entries))));
    CHECK(state.palette().entries()[0].orientations().size() == 2);

    auto applied = state.apply(place_at(0, 1, unit(0, 0)));
    CHECK(bool(applied));
    if (!applied) {
        return;
    }
    const Placement &placed = state.arrangement().entries().front().placement;
    CHECK(placed.prototile().id() == PrototileId(9));
    CHECK(placed.orientation() == Q);
    // The quarter turn stands the 4x1 bar upright; the reference variant does
    // not, so index 1 genuinely selected the second distinct variant.
    CHECK(extent(placed.oriented_polygon()) == std::make_pair(Coordinate::SCALE, 4 * Coordinate::SCALE));
    const Polygon &reference_variant =
        state.palette().entries()[0].orientations()[0].canonical_polygon();
    CHECK(extent(reference_variant) == std::make_pair(4 * Coordinate::SCALE, Coordinate::SCALE));
}

TEST_CASE("a second disjoint placement receives the next id") {
    State state = square_state(Supply::unlimited());
    auto first = state.apply(place_at(0, 0, unit(0, 0)));
    // Shares the full edge x == 2 with the first square: legal contact.
    auto second = state.apply(place_at(0, 0, unit(2, 0)));
    CHECK(bool(first));
    CHECK(bool(second));
    if (!first || !second) {
        return;
    }
    CHECK(first.value() == PlacementId(0));
    CHECK(second.value() == PlacementId(1));
    CHECK(state.arrangement().entries().size() == 2);
    CHECK(state.arrangement().next_id().value() == PlacementId(2));
}

TEST_CASE("interior overlap preserves the complete core ArrangementError") {
    State state = square_state(Supply::unlimited());
    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));

    auto applied = state.apply(place_at(0, 0, unit(1, 1)));
    CHECK(!applied);
    if (applied) {
        return;
    }
    const ArrangementError *error = std::get_if<ArrangementError>(&applied.error());
    CHECK(error != nullptr);
    if (error != nullptr) {
        CHECK(error->code == ArrangementErrorCode::interior_overlap);
        CHECK(error->conflicting_placement.has_value());
        CHECK(error->conflicting_placement.value() == PlacementId(0));
    }
    CHECK(state.arrangement().entries().size() == 1);
}

TEST_CASE("identifier exhaustion preserves the core identifier_exhausted code") {
    State state = State::testing_with_empty_arrangement(
        level_of(square_palette(Supply::unlimited())),
        Arrangement::testing_with_next_id(UINT64_MAX));

    auto last = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(last));
    if (last) {
        CHECK(last.value() == PlacementId(UINT64_MAX));
    }
    CHECK(state.arrangement().next_id().has_value() == false);

    auto overflow = state.apply(place_at(0, 0, unit(10, 10)));
    CHECK(!overflow);
    if (overflow) {
        return;
    }
    const ArrangementError *error = std::get_if<ArrangementError>(&overflow.error());
    CHECK(error != nullptr);
    if (error != nullptr) {
        CHECK(error->code == ArrangementErrorCode::identifier_exhausted);
        CHECK(error->conflicting_placement.has_value() == false);
    }
    CHECK(state.arrangement().entries().size() == 1);
}

TEST_CASE("placement coordinate overflow preserves the exact core PlacementError") {
    State state = square_state(Supply::unlimited());

    auto applied = state.apply(place_at(0, 0, raw_pt(INT64_MAX, 0)));
    CHECK(!applied);
    if (applied) {
        return;
    }
    const PlacementError *error = std::get_if<PlacementError>(&applied.error());
    CHECK(error != nullptr);
    if (error != nullptr) {
        CHECK(*error == PlacementError::footprint_overflow);
    }
    CHECK(state.arrangement().entries().empty());
    CHECK(state.arrangement().next_id().value() == PlacementId(0));
}

TEST_CASE("every failed placement leaves entries, ids, and the allocator unchanged") {
    State state = square_state(Supply::unlimited());
    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));
    CHECK(bool(state.apply(place_at(0, 0, unit(4, 0)))));

    const std::size_t before_size = state.arrangement().entries().size();
    const PlacementId before_next = state.arrangement().next_id().value();
    std::vector<PlacementId> before_ids;
    std::vector<Point> before_translations;
    for (const Entry &entry : state.arrangement().entries()) {
        before_ids.push_back(entry.id);
        before_translations.push_back(entry.placement.translation());
    }

    // One failure of each kind the place command can publish.
    CHECK(!state.apply(place_at(1, 0, unit(20, 0))));
    CHECK(!state.apply(place_at(0, 1, unit(20, 0))));
    CHECK(!state.apply(place_at(0, 0, unit(1, 1))));
    CHECK(!state.apply(place_at(0, 0, raw_pt(INT64_MAX, 0))));

    CHECK(state.arrangement().entries().size() == before_size);
    CHECK(state.arrangement().next_id().value() == before_next);
    bool unchanged = true;
    for (std::size_t i = 0; i < before_ids.size(); ++i) {
        if (state.arrangement().entries()[i].id != before_ids[i]) {
            unchanged = false;
        }
        if (state.arrangement().entries()[i].placement.translation() != before_translations[i]) {
            unchanged = false;
        }
    }
    CHECK(unchanged);
    // The palette is equally untouched.
    CHECK(state.palette().order() == 1);
    CHECK(state.palette().entries()[0].prototile().id() == PrototileId(1));
}

// ---------------------------------------------------------------------------
// supply
// ---------------------------------------------------------------------------

TEST_CASE("unlimited supply permits repeated successful placement") {
    State state = square_state(Supply::unlimited());
    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));
    CHECK(bool(state.apply(place_at(0, 0, unit(2, 0)))));
    CHECK(bool(state.apply(place_at(0, 0, unit(4, 0)))));
    CHECK(state.arrangement().entries().size() == 3);
}

TEST_CASE("finite supply one permits one placement and then reports supply_exhausted") {
    State state = square_state(Supply::finite(1).value());
    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));

    // Geometrically legal — only the configured capacity rejects it.
    auto second = state.apply(place_at(0, 0, unit(4, 0)));
    CHECK(!second);
    if (!second) {
        CHECK(is_candidate(second.error(), CandidateError::supply_exhausted));
    }
    CHECK(state.arrangement().entries().size() == 1);
    CHECK(state.arrangement().next_id().value() == PlacementId(1));
}

TEST_CASE("different orientations of one entry draw on the same supply") {
    std::vector<PaletteEntry> entries;
    entries.push_back(make_entry(bar(3), Supply::finite(1).value(), { R, Q, H, T }));
    State state(level_of(palette_of(std::move(entries))));
    CHECK(state.palette().entries()[0].orientations().size() == 2);

    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));
    // A different distinct orientation, far away, still finds the entry's single
    // configured piece used up.
    auto second = state.apply(place_at(0, 1, unit(0, 10)));
    CHECK(!second);
    if (!second) {
        CHECK(is_candidate(second.error(), CandidateError::supply_exhausted));
    }
    CHECK(state.arrangement().entries().size() == 1);
}

TEST_CASE("a failed geometric placement does not consume supply") {
    State state = square_state(Supply::finite(2).value());
    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));

    auto overlapped = state.apply(place_at(0, 0, unit(1, 1)));
    CHECK(!overlapped);
    if (!overlapped) {
        CHECK(std::get_if<ArrangementError>(&overlapped.error()) != nullptr);
    }

    // The second configured piece is still available.
    CHECK(bool(state.apply(place_at(0, 0, unit(4, 0)))));
    // And only now is the capacity used up.
    auto third = state.apply(place_at(0, 0, unit(8, 0)));
    CHECK(!third);
    if (!third) {
        CHECK(is_candidate(third.error(), CandidateError::supply_exhausted));
    }
    CHECK(state.arrangement().entries().size() == 2);
}

TEST_CASE("placements of a different prototile id do not consume an entry's supply") {
    std::vector<PaletteEntry> entries;
    entries.push_back(make_entry(square(1), Supply::finite(1).value(), { R }));
    entries.push_back(make_entry(bar(2), Supply::unlimited(), { R }));
    State state(level_of(palette_of(std::move(entries))));

    // Two bars first; neither draws on the square's single configured piece.
    CHECK(bool(state.apply(place_at(1, 0, unit(0, 10)))));
    CHECK(bool(state.apply(place_at(1, 0, unit(0, 12)))));
    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));

    auto extra_square = state.apply(place_at(0, 0, unit(4, 0)));
    CHECK(!extra_square);
    if (!extra_square) {
        CHECK(is_candidate(extra_square.error(), CandidateError::supply_exhausted));
    }
    // The unlimited bar entry is unaffected by the square's exhaustion.
    CHECK(bool(state.apply(place_at(1, 0, unit(0, 14)))));
}

TEST_CASE("supply counts placements whichever command added them") {
    // Supply identity is the exact PrototileId, and usage is derived from the
    // arrangement rather than stored, so a mated placement draws on the same
    // capacity a directly placed one does. There is no longer any way to
    // preload a placement: a state begins empty and every entry in it was
    // proven against this palette, its supply, and the region.
    State state = square_state(Supply::finite(2).value());
    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    auto mated = state.apply(MateFullEdgesCommand {
        anchor.value(),
        EdgeIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(2),
    });
    CHECK(bool(mated));
    CHECK(state.arrangement().entries().size() == 2);

    auto spent = state.apply(place_at(0, 0, unit(8, 0)));
    CHECK(!spent);
    if (!spent) {
        CHECK(is_candidate(spent.error(), CandidateError::supply_exhausted));
    }
    CHECK(state.arrangement().entries().size() == 2);
}

TEST_CASE("every placement a state holds came from its own palette") {
    // The stronger construction invariant: with no preloading seam, an
    // arrangement entry naming a prototile the palette does not offer is
    // unreachable, so supply derivation never has to tolerate a foreign id.
    std::vector<PaletteEntry> entries;
    entries.push_back(make_entry(square(1), Supply::unlimited(), { R }));
    entries.push_back(make_entry(bar(2), Supply::unlimited(), { R }));
    State state(level_of(palette_of(std::move(entries))));

    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));
    CHECK(bool(state.apply(place_at(1, 0, unit(0, 10)))));
    auto anchor = state.arrangement().entries().front().id;
    CHECK(bool(state.apply(MateVerticesCommand {
        anchor,
        VertexIndex(2),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        VertexIndex(0),
    })));

    CHECK(state.arrangement().entries().size() == 3);
    for (const Entry &entry : state.arrangement().entries()) {
        bool offered = false;
        for (const PaletteEntry &palette_entry : state.palette().entries()) {
            if (palette_entry.prototile().id() == entry.placement.prototile().id()) {
                offered = true;
            }
        }
        CHECK(offered);
    }
}

TEST_CASE("finite maximum uint64 supply has no counting overflow path") {
    State state = square_state(Supply::finite(UINT64_MAX).value());
    CHECK(state.palette().entries()[0].supply().finite_amount().value() == UINT64_MAX);

    // Usage is one increment per placed entry, so it is bounded by the
    // arrangement's own size and a maximal capacity never approaches an
    // addition or a subtraction that could leave the type.
    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));
    CHECK(bool(state.apply(place_at(0, 0, unit(2, 0)))));
    CHECK(bool(state.apply(place_at(0, 0, unit(4, 0)))));
    CHECK(state.arrangement().entries().size() == 3);
}

// ---------------------------------------------------------------------------
// derived supply status
// ---------------------------------------------------------------------------

namespace {

// The status of the first palette entry, which every fixture below queries.
SupplyStatus status_of(const State &p_state, std::size_t p_entry) {
    return p_state.supply_status(PaletteEntryIndex(p_entry)).value();
}

} // namespace

TEST_CASE("an out-of-range palette entry has no supply status") {
    State state = square_state(Supply::unlimited());
    CHECK(state.palette().order() == 1);

    CHECK(state.supply_status(PaletteEntryIndex(1)).has_value() == false);
    CHECK(state.supply_status(PaletteEntryIndex(9999)).has_value() == false);
    // The one entry that does exist answers.
    CHECK(state.supply_status(PaletteEntryIndex(0)).has_value());
}

TEST_CASE("an unused finite entry reports no usage and its complete capacity") {
    State state = square_state(Supply::finite(3).value());
    const SupplyStatus status = status_of(state, 0);
    CHECK(status.used == 0);
    CHECK(status.remaining.has_value());
    if (status.remaining.has_value()) {
        CHECK(status.remaining.value() == 3);
    }
}

TEST_CASE("an unlimited entry reports usage but never a remaining amount") {
    State state = square_state(Supply::unlimited());
    const SupplyStatus empty = status_of(state, 0);
    CHECK(empty.used == 0);
    // Empty means unlimited. It is not zero and not unknown.
    CHECK(empty.remaining.has_value() == false);

    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));
    CHECK(bool(state.apply(place_at(0, 0, unit(2, 0)))));
    const SupplyStatus used = status_of(state, 0);
    CHECK(used.used == 2);
    CHECK(used.remaining.has_value() == false);
}

TEST_CASE("every distinct orientation of one entry counts against the same supply") {
    std::vector<PaletteEntry> entries;
    entries.push_back(make_entry(bar(3), Supply::finite(4).value(), { R, Q, H, T }));
    State state(level_of(palette_of(std::move(entries))));
    CHECK(state.palette().entries()[0].orientations().size() == 2);

    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));
    CHECK(bool(state.apply(place_at(0, 1, unit(0, 4)))));
    const SupplyStatus status = status_of(state, 0);
    CHECK(status.used == 2);
    CHECK(status.remaining.value() == 2);
}

TEST_CASE("placements of another prototile identity never count against an entry") {
    std::vector<PaletteEntry> entries;
    entries.push_back(make_entry(square(1), Supply::finite(2).value(), { R }));
    entries.push_back(make_entry(bar(2), Supply::finite(5).value(), { R }));
    State state(level_of(palette_of(std::move(entries))));

    CHECK(bool(state.apply(place_at(1, 0, unit(0, 10)))));
    CHECK(bool(state.apply(place_at(1, 0, unit(0, 12)))));
    CHECK(bool(state.apply(place_at(1, 0, unit(0, 14)))));

    const SupplyStatus squares = status_of(state, 0);
    CHECK(squares.used == 0);
    CHECK(squares.remaining.value() == 2);
    const SupplyStatus bars = status_of(state, 1);
    CHECK(bars.used == 3);
    CHECK(bars.remaining.value() == 2);
}

TEST_CASE("finite remaining reaches exactly zero and that same zero exhausts the command") {
    State state = square_state(Supply::finite(2).value());
    CHECK(status_of(state, 0).remaining.value() == 2);

    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));
    CHECK(status_of(state, 0).used == 1);
    CHECK(status_of(state, 0).remaining.value() == 1);

    CHECK(bool(state.apply(place_at(0, 0, unit(2, 0)))));
    const SupplyStatus spent = status_of(state, 0);
    CHECK(spent.used == 2);
    // Zero, not empty: the entry is finite and used up.
    CHECK(spent.remaining.has_value());
    if (spent.remaining.has_value()) {
        CHECK(spent.remaining.value() == 0);
    }

    // Command legality reads that very count, so it rejects exactly here.
    auto third = state.apply(place_at(0, 0, unit(4, 0)));
    CHECK(!third);
    if (!third) {
        CHECK(is_candidate(third.error(), CandidateError::supply_exhausted));
    }
    auto previewed = state.preview(place_at(0, 0, unit(4, 0)));
    CHECK(!previewed);
    if (!previewed) {
        CHECK(is_candidate(previewed.error(), CandidateError::supply_exhausted));
    }
}

TEST_CASE("deletion restores exactly one finite piece, with no counter to put back") {
    State state = square_state(Supply::finite(2).value());
    auto first = state.apply(place_at(0, 0, unit(0, 0)));
    auto second = state.apply(place_at(0, 0, unit(2, 0)));
    CHECK(bool(first));
    CHECK(bool(second));
    if (!first || !second) {
        return;
    }
    CHECK(status_of(state, 0).remaining.value() == 0);

    CHECK(bool(state.apply(RemoveCommand { second.value() })));
    const SupplyStatus after = status_of(state, 0);
    CHECK(after.used == 1);
    CHECK(after.remaining.value() == 1);

    // Exactly one, not two: the entry is available again, once.
    CHECK(bool(state.apply(place_at(0, 0, unit(4, 0)))));
    CHECK(status_of(state, 0).remaining.value() == 0);
    auto spent = state.apply(place_at(0, 0, unit(6, 0)));
    CHECK(!spent);
    if (!spent) {
        CHECK(is_candidate(spent.error(), CandidateError::supply_exhausted));
    }
}

TEST_CASE("deletion changes unlimited usage while remaining stays empty") {
    State state = square_state(Supply::unlimited());
    auto first = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(first));
    CHECK(bool(state.apply(place_at(0, 0, unit(2, 0)))));
    if (!first) {
        return;
    }
    CHECK(status_of(state, 0).used == 2);

    CHECK(bool(state.apply(RemoveCommand { first.value() })));
    const SupplyStatus after = status_of(state, 0);
    CHECK(after.used == 1);
    CHECK(after.remaining.has_value() == false);
}

TEST_CASE("a maximum finite capacity counts and subtracts without leaving the type") {
    State state = square_state(Supply::finite(UINT64_MAX).value());
    const SupplyStatus empty = status_of(state, 0);
    CHECK(empty.used == 0);
    CHECK(empty.remaining.value() == UINT64_MAX);

    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));
    CHECK(bool(state.apply(place_at(0, 0, unit(2, 0)))));
    const SupplyStatus used = status_of(state, 0);
    CHECK(used.used == 2);
    CHECK(used.remaining.value() == UINT64_MAX - 2);
    // Still available, which is what a maximal capacity should mean.
    CHECK(bool(state.apply(place_at(0, 0, unit(4, 0)))));
    CHECK(status_of(state, 0).remaining.value() == UINT64_MAX - 3);
}

TEST_CASE("a supply status query observes without changing anything") {
    State state = square_state(Supply::finite(3).value());
    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));

    const State &observed = state;
    const std::size_t before_size = observed.arrangement().entries().size();
    const PlacementId before_next = observed.arrangement().next_id().value();

    // Repeated queries against an unmodified state are identical, and neither
    // consumed a piece, an id, or an entry.
    const SupplyStatus first = observed.supply_status(PaletteEntryIndex(0)).value();
    const SupplyStatus second = observed.supply_status(PaletteEntryIndex(0)).value();
    CHECK(first.used == second.used);
    CHECK(first.remaining.value() == second.remaining.value());
    CHECK(first.used == 1);
    CHECK(first.remaining.value() == 2);

    CHECK(observed.arrangement().entries().size() == before_size);
    CHECK(observed.arrangement().next_id().value() == before_next);
    CHECK(observed.palette().order() == 1);
}

// ---------------------------------------------------------------------------
// exact deletion
// ---------------------------------------------------------------------------

TEST_CASE("a remove command deletes exactly the one placement it names") {
    State state = square_state(Supply::unlimited());
    auto first = state.apply(place_at(0, 0, unit(0, 0)));
    auto second = state.apply(place_at(0, 0, unit(2, 0)));
    auto third = state.apply(place_at(0, 0, unit(4, 0)));
    CHECK(bool(first));
    CHECK(bool(second));
    CHECK(bool(third));
    if (!first || !second || !third) {
        return;
    }

    auto removed = state.apply(RemoveCommand { second.value() });
    CHECK(bool(removed));
    if (removed) {
        CHECK(removed.value() == second.value());
    }

    CHECK(state.arrangement().entries().size() == 2);
    CHECK(state.arrangement().entries()[0].id == first.value());
    CHECK(state.arrangement().entries()[1].id == third.value());
    CHECK(state.arrangement().entries()[0].placement.translation() == unit(0, 0));
    CHECK(state.arrangement().entries()[1].placement.translation() == unit(4, 0));
    // Deletion allocates nothing, so the allocator is exactly where it was.
    CHECK(state.arrangement().next_id().value() == PlacementId(3));
}

TEST_CASE("removing a missing id is typed and leaves the state completely unchanged") {
    State state = square_state(Supply::finite(4).value());
    auto first = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(first));
    CHECK(bool(state.apply(place_at(0, 0, unit(2, 0)))));
    if (!first) {
        return;
    }

    // Delete once, then name the same id again: gone is gone.
    CHECK(bool(state.apply(RemoveCommand { first.value() })));

    const std::size_t before_size = state.arrangement().entries().size();
    const PlacementId before_next = state.arrangement().next_id().value();
    const SupplyStatus before_supply = status_of(state, 0);

    auto again = state.apply(RemoveCommand { first.value() });
    CHECK(!again);
    if (!again) {
        CHECK(again.error() == RemovalError::placement_not_found);
    }
    auto never = state.apply(RemoveCommand { PlacementId(9999) });
    CHECK(!never);
    if (!never) {
        CHECK(never.error() == RemovalError::placement_not_found);
    }

    CHECK(state.arrangement().entries().size() == before_size);
    CHECK(state.arrangement().next_id().value() == before_next);
    CHECK(status_of(state, 0).used == before_supply.used);
    CHECK(status_of(state, 0).remaining.value() == before_supply.remaining.value());
}

TEST_CASE("deletion leaves the level, palette, region, and other placements untouched") {
    std::vector<PaletteEntry> entries;
    entries.push_back(make_entry(square(1), Supply::finite(3).value(), { R }));
    entries.push_back(make_entry(bar(2), Supply::unlimited(), { R }));
    State state(level_of(palette_of(std::move(entries))));

    auto target = state.apply(place_at(0, 0, unit(0, 0)));
    auto survivor = state.apply(place_at(1, 0, unit(0, 10)));
    CHECK(bool(target));
    CHECK(bool(survivor));
    if (!target || !survivor) {
        return;
    }
    const Polygon::Vertices before_footprint =
        state.arrangement().entries()[1].placement.footprint().vertices();

    CHECK(bool(state.apply(RemoveCommand { target.value() })));

    // The level's own values are not arrangement state and cannot be deleted.
    CHECK(state.palette().order() == 2);
    CHECK(state.palette().entries()[0].prototile().id() == PrototileId(1));
    CHECK(state.palette().entries()[1].prototile().id() == PrototileId(2));
    CHECK(state.palette().entries()[0].supply() == Supply::finite(3).value());
    CHECK(state.region().outer_boundary().vertices().front() == unit(-32, -32));
    CHECK(state.region().inner_boundaries().empty());

    // The surviving placement keeps its identity and its exact geometry.
    CHECK(state.arrangement().entries().size() == 1);
    CHECK(state.arrangement().entries()[0].id == survivor.value());
    CHECK(state.arrangement().entries()[0].placement.prototile().id() == PrototileId(2));
    CHECK(state.arrangement().entries()[0].placement.footprint().vertices()
        == before_footprint);
}

TEST_CASE("deleting any storage position preserves the survivors' order") {
    for (std::size_t removed_index = 0; removed_index < 3; ++removed_index) {
        State state = square_state(Supply::unlimited());
        std::vector<PlacementId> placed;
        for (std::int64_t i = 0; i < 3; ++i) {
            auto applied = state.apply(place_at(0, 0, unit(2 * i, 0)));
            CHECK(bool(applied));
            if (applied) {
                placed.push_back(applied.value());
            }
        }
        if (placed.size() != 3) {
            continue;
        }

        CHECK(bool(state.apply(RemoveCommand { placed[removed_index] })));
        CHECK(state.arrangement().entries().size() == 2);

        std::size_t position = 0;
        for (std::size_t i = 0; i < 3; ++i) {
            if (i == removed_index) {
                continue;
            }
            CHECK(state.arrangement().entries()[position].id == placed[i]);
            CHECK(state.arrangement().entries()[position].placement.translation()
                == unit(2 * static_cast<std::int64_t>(i), 0));
            ++position;
        }
    }
}

TEST_CASE("a deleted placement is no longer an anchor, while survivors still are") {
    State state = square_state(Supply::unlimited());
    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    auto doomed = state.apply(place_at(0, 0, unit(8, 0)));
    CHECK(bool(anchor));
    CHECK(bool(doomed));
    if (!anchor || !doomed) {
        return;
    }

    CHECK(bool(state.apply(RemoveCommand { doomed.value() })));

    // The deleted identity is simply absent; it reports the completed
    // anchor_not_found rather than any deletion-specific error.
    auto orphaned_edges = state.apply(MateFullEdgesCommand {
        doomed.value(),
        EdgeIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(2),
    });
    CHECK(!orphaned_edges);
    if (!orphaned_edges) {
        const JoinError *error = std::get_if<JoinError>(&orphaned_edges.error());
        CHECK(error != nullptr && error->code == JoinErrorCode::anchor_not_found);
    }

    auto orphaned_vertices = state.apply(MateVerticesCommand {
        doomed.value(),
        VertexIndex(2),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        VertexIndex(0),
    });
    CHECK(!orphaned_vertices);
    if (!orphaned_vertices) {
        const JoinError *error = std::get_if<JoinError>(&orphaned_vertices.error());
        CHECK(error != nullptr && error->code == JoinErrorCode::anchor_not_found);
    }

    // The survivor remains a perfectly ordinary anchor, and direct placement
    // into the vacated space still succeeds.
    CHECK(bool(state.apply(MateFullEdgesCommand {
        anchor.value(),
        EdgeIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(2),
    })));
    CHECK(bool(state.apply(place_at(0, 0, unit(8, 0)))));
    CHECK(state.arrangement().entries().size() == 3);
}

TEST_CASE("deletion makes a solved state unsolved, with no stored completion flag") {
    State state(Level(square_palette(Supply::unlimited()), tight_region()));
    CHECK(state.solved() == false);

    auto only = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(only));
    if (!only) {
        return;
    }
    CHECK(state.solved());

    // Completion is derived from covered area alone, so removing any
    // positive-area placement necessarily unsolves it.
    CHECK(bool(state.apply(RemoveCommand { only.value() })));
    CHECK(state.solved() == false);

    // And placing it back solves it again, at the next fresh identity.
    auto replaced = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(replaced));
    if (replaced) {
        CHECK(replaced.value() == PlacementId(1));
    }
    CHECK(state.solved());
}

// ---------------------------------------------------------------------------
// feature mating
// ---------------------------------------------------------------------------

TEST_CASE("full-edge mating resolves its candidate through the palette and succeeds") {
    auto built = tetromino_state();
    CHECK(built.has_value());
    if (!built) {
        return;
    }
    State state = std::move(built).value();

    auto first = state.apply(PlaceCommand {
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        Point { Coordinate::from_raw(0), Coordinate::from_raw(0) },
    });
    CHECK(bool(first));
    if (!first) {
        return;
    }

    // Edges 0 and 2 are opposite complete edges of the o tetromino: edge 0 runs
    // (0,0)->(2,0) and edge 2 runs (2,2)->(0,2).
    auto second = state.apply(MateFullEdgesCommand {
        first.value(),
        EdgeIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(2),
    });
    CHECK(bool(second));
    if (!second) {
        return;
    }
    CHECK(second.value() == PlacementId(1));
    CHECK(state.arrangement().entries().size() == 2);
    // The returned id is exactly the one stored in the appended entry.
    CHECK(state.arrangement().entries().back().id == second.value());
    // The derived translation is exact: one piece height below the anchor.
    CHECK(state.arrangement().entries().back().placement.translation() == unit(0, -2));
    CHECK(state.arrangement().entries().back().placement.prototile().id() == PrototileId(1));
}

TEST_CASE("vertex mating resolves its candidate through the palette and succeeds") {
    State state = square_state(Supply::unlimited());
    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    // Anchor vertex 2 is the corner (2,2); candidate vertex 0 is its local
    // origin, so the mated square touches the anchor at that single point.
    auto joined = state.apply(MateVerticesCommand {
        anchor.value(),
        VertexIndex(2),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        VertexIndex(0),
    });
    CHECK(bool(joined));
    if (!joined) {
        return;
    }
    CHECK(joined.value() == PlacementId(1));
    CHECK(state.arrangement().entries().size() == 2);
    CHECK(state.arrangement().entries().back().id == joined.value());
    CHECK(state.arrangement().entries().back().placement.translation() == unit(2, 2));
}

TEST_CASE("full-edge mating failures preserve the complete core JoinError") {
    State state = square_state(Supply::unlimited());
    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }
    auto blocker = state.apply(place_at(0, 0, unit(2, 0)));
    CHECK(bool(blocker));
    if (!blocker) {
        return;
    }

    // Anchor edge 1 ((2,0)->(2,2)) mates candidate edge 3 ((0,2)->(0,0)), which
    // derives exactly the square already occupying that space.
    auto overlapped = state.apply(MateFullEdgesCommand {
        anchor.value(),
        EdgeIndex(1),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(3),
    });
    CHECK(!overlapped);
    if (!overlapped) {
        const JoinError *error = std::get_if<JoinError>(&overlapped.error());
        CHECK(error != nullptr);
        if (error != nullptr) {
            CHECK(error->code == JoinErrorCode::interior_overlap);
            CHECK(error->conflicting_placement.has_value());
            CHECK(error->conflicting_placement.value() == blocker.value());
        }
    }

    // Two same-direction edges are incompatible, and carry no conflict identity.
    auto incompatible = state.apply(MateFullEdgesCommand {
        anchor.value(),
        EdgeIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(0),
    });
    CHECK(!incompatible);
    if (!incompatible) {
        const JoinError *error = std::get_if<JoinError>(&incompatible.error());
        CHECK(error != nullptr);
        if (error != nullptr) {
            CHECK(error->code == JoinErrorCode::incompatible_edges);
            CHECK(error->conflicting_placement.has_value() == false);
        }
    }

    // A missing anchor and an out-of-range edge are equally preserved.
    auto missing = state.apply(MateFullEdgesCommand {
        PlacementId(999),
        EdgeIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(2),
    });
    CHECK(!missing);
    if (!missing) {
        const JoinError *error = std::get_if<JoinError>(&missing.error());
        CHECK(error != nullptr && error->code == JoinErrorCode::anchor_not_found);
    }

    auto bad_edge = state.apply(MateFullEdgesCommand {
        anchor.value(),
        EdgeIndex(4),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(0),
    });
    CHECK(!bad_edge);
    if (!bad_edge) {
        const JoinError *error = std::get_if<JoinError>(&bad_edge.error());
        CHECK(error != nullptr && error->code == JoinErrorCode::anchor_edge_out_of_range);
    }

    CHECK(state.arrangement().entries().size() == 2);
    CHECK(state.arrangement().next_id().value() == PlacementId(2));
}

TEST_CASE("vertex mating failures preserve the complete core JoinError") {
    State state = square_state(Supply::unlimited());
    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    auto bad_anchor_vertex = state.apply(MateVerticesCommand {
        anchor.value(),
        VertexIndex(4),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        VertexIndex(0),
    });
    CHECK(!bad_anchor_vertex);
    if (!bad_anchor_vertex) {
        const JoinError *error = std::get_if<JoinError>(&bad_anchor_vertex.error());
        CHECK(error != nullptr);
        if (error != nullptr) {
            CHECK(error->code == JoinErrorCode::anchor_vertex_out_of_range);
            CHECK(error->conflicting_placement.has_value() == false);
        }
    }

    auto bad_candidate_vertex = state.apply(MateVerticesCommand {
        anchor.value(),
        VertexIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        VertexIndex(4),
    });
    CHECK(!bad_candidate_vertex);
    if (!bad_candidate_vertex) {
        const JoinError *error = std::get_if<JoinError>(&bad_candidate_vertex.error());
        CHECK(error != nullptr);
        if (error != nullptr) {
            CHECK(error->code == JoinErrorCode::candidate_vertex_out_of_range);
        }
    }

    // Mating vertex 0 onto vertex 0 lands the candidate exactly on the anchor.
    auto overlapped = state.apply(MateVerticesCommand {
        anchor.value(),
        VertexIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        VertexIndex(0),
    });
    CHECK(!overlapped);
    if (!overlapped) {
        const JoinError *error = std::get_if<JoinError>(&overlapped.error());
        CHECK(error != nullptr);
        if (error != nullptr) {
            CHECK(error->code == JoinErrorCode::interior_overlap);
            CHECK(error->conflicting_placement.value() == anchor.value());
        }
    }

    CHECK(state.arrangement().entries().size() == 1);
    CHECK(state.arrangement().next_id().value() == PlacementId(1));
}

TEST_CASE("mating rejects an invalid candidate before any anchor or feature error") {
    State state = square_state(Supply::unlimited());
    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));

    // Every core failure is simultaneously available — a missing anchor and two
    // out-of-range feature indices — yet candidate resolution answers first and
    // no JoinError is published.
    auto bad_entry = state.apply(MateFullEdgesCommand {
        PlacementId(999),
        EdgeIndex(99),
        PaletteEntryIndex(9),
        PaletteOrientationIndex(9),
        EdgeIndex(99),
    });
    CHECK(!bad_entry);
    if (!bad_entry) {
        CHECK(is_candidate(bad_entry.error(), CandidateError::palette_entry_out_of_range));
        CHECK(std::get_if<JoinError>(&bad_entry.error()) == nullptr);
    }

    // A valid entry with an invalid orientation reports the orientation, still
    // without inspecting the anchor or the vertices.
    auto bad_orientation = state.apply(MateVerticesCommand {
        PlacementId(999),
        VertexIndex(99),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(9),
        VertexIndex(99),
    });
    CHECK(!bad_orientation);
    if (!bad_orientation) {
        CHECK(is_candidate(bad_orientation.error(), CandidateError::orientation_out_of_range));
        CHECK(std::get_if<JoinError>(&bad_orientation.error()) == nullptr);
    }

    CHECK(state.arrangement().entries().size() == 1);
    CHECK(state.arrangement().next_id().value() == PlacementId(1));
}

TEST_CASE("mating rejects exhausted supply before core mating and changes nothing") {
    State state = square_state(Supply::finite(1).value());
    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    const std::size_t before_size = state.arrangement().entries().size();
    const PlacementId before_next = state.arrangement().next_id().value();

    // Both matings would otherwise succeed, so only supply can be rejecting them.
    auto edges = state.apply(MateFullEdgesCommand {
        anchor.value(),
        EdgeIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(2),
    });
    CHECK(!edges);
    if (!edges) {
        CHECK(is_candidate(edges.error(), CandidateError::supply_exhausted));
    }

    auto vertices = state.apply(MateVerticesCommand {
        anchor.value(),
        VertexIndex(2),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        VertexIndex(0),
    });
    CHECK(!vertices);
    if (!vertices) {
        CHECK(is_candidate(vertices.error(), CandidateError::supply_exhausted));
    }

    CHECK(state.arrangement().entries().size() == before_size);
    CHECK(state.arrangement().next_id().value() == before_next);
}

TEST_CASE("a failed mating consumes neither supply nor a placement id") {
    State state = square_state(Supply::finite(2).value());
    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    auto failed = state.apply(MateFullEdgesCommand {
        anchor.value(),
        EdgeIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(0),
    });
    CHECK(!failed);
    if (!failed) {
        CHECK(std::get_if<JoinError>(&failed.error()) != nullptr);
    }
    CHECK(state.arrangement().entries().size() == 1);
    CHECK(state.arrangement().next_id().value() == PlacementId(1));

    // The second configured piece is still available and still receives id 1.
    auto joined = state.apply(MateFullEdgesCommand {
        anchor.value(),
        EdgeIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(2),
    });
    CHECK(bool(joined));
    if (joined) {
        CHECK(joined.value() == PlacementId(1));
    }
    CHECK(state.arrangement().entries().size() == 2);
}

// ---------------------------------------------------------------------------
// contact semantics
// ---------------------------------------------------------------------------

TEST_CASE("exact placement creates legal partial-edge contact that no mating derives") {
    std::vector<PaletteEntry> entries;
    entries.push_back(make_entry(bar(1), Supply::unlimited(), { R }));
    entries.push_back(make_entry(square(2), Supply::unlimited(), { R }));
    State state(level_of(palette_of(std::move(entries))));

    // The anchor bar footprint spans x in [0,4], y in [0,1].
    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    // The square sits on the anchor's long top edge, strictly inside it, at an
    // exact authored translation.
    auto applied = state.apply(place_at(1, 0, unit(1, 1)));
    CHECK(bool(applied));
    if (!applied) {
        return;
    }
    CHECK(applied.value() == PlacementId(1));
    // Insertion succeeded, which is exactly the core's whole-footprint proof
    // that the two footprints share boundary with no positive-area overlap.
    CHECK(state.arrangement().entries().size() == 2);

    const Polygon &anchor_footprint =
        state.arrangement().entries()[0].placement.footprint();
    const Polygon &square_footprint =
        state.arrangement().entries()[1].placement.footprint();

    // The shared boundary is the segment (1,1)->(3,1), strictly inside the
    // anchor's top edge (4,1)->(0,1).
    CHECK(has_vertex(square_footprint, unit(1, 1)));
    CHECK(has_vertex(square_footprint, unit(3, 1)));
    CHECK(has_vertex(anchor_footprint, unit(4, 1)));
    CHECK(has_vertex(anchor_footprint, unit(0, 1)));

    // No vertex mating could derive this placement: no pair of polygon vertices
    // coincides.
    bool shares_a_vertex = false;
    for (const Point &a : anchor_footprint.vertices()) {
        for (const Point &b : square_footprint.vertices()) {
            if (a == b) {
                shares_a_vertex = true;
            }
        }
    }
    CHECK(!shares_a_vertex);

    // Nor could full-edge mating: the two selected complete edges have different
    // lengths, so their displacement vectors are not opposite.
    const EdgeIndex anchor_top = find_edge(anchor_footprint, unit(4, 1), unit(0, 1));
    const EdgeIndex candidate_bottom = find_edge(
        state.palette().entries()[1].orientations()[0].canonical_polygon(),
        unit(0, 0),
        unit(2, 0));
    CHECK(anchor_top.value() < anchor_footprint.vertices().size());
    CHECK(candidate_bottom.value() < 4);

    auto mated = state.apply(MateFullEdgesCommand {
        anchor.value(),
        anchor_top,
        PaletteEntryIndex(1),
        PaletteOrientationIndex(0),
        candidate_bottom,
    });
    CHECK(!mated);
    if (!mated) {
        const JoinError *error = std::get_if<JoinError>(&mated.error());
        CHECK(error != nullptr);
        if (error != nullptr) {
            CHECK(error->code == JoinErrorCode::incompatible_edges);
        }
    }

    // The failed mating added nothing: the exact placement above remains the
    // only expression of this contact, and no partial-edge path exists anywhere.
    CHECK(state.arrangement().entries().size() == 2);
    CHECK(state.arrangement().next_id().value() == PlacementId(2));
}

// ---------------------------------------------------------------------------
// mating preview
// ---------------------------------------------------------------------------

namespace {

// The o tetromino's opposite complete edges: edge 0 runs (0,0)->(2,0) and edge 2
// runs (2,2)->(0,2), so mating them drops a second square exactly below.
MateFullEdgesCommand mate_below(PlacementId p_anchor) {
    return MateFullEdgesCommand {
        p_anchor,
        EdgeIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(2),
    };
}

// Anchor corner (2,2) against the candidate's local origin: legal isolated point
// contact.
MateVerticesCommand mate_at_corner(PlacementId p_anchor) {
    return MateVerticesCommand {
        p_anchor,
        VertexIndex(2),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        VertexIndex(0),
    };
}

bool same_geometry(const Placement &p_lhs, const Placement &p_rhs) {
    return p_lhs.prototile().id() == p_rhs.prototile().id()
        && p_lhs.orientation() == p_rhs.orientation()
        && p_lhs.translation() == p_rhs.translation()
        && p_lhs.footprint().vertices() == p_rhs.footprint().vertices();
}

} // namespace

TEST_CASE("mating preview resolves the palette candidate and derives the exact placement") {
    State state = square_state(Supply::unlimited());
    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    // Deliberately through a const reference: preview is an observation.
    const State &observed = state;

    auto edges = observed.preview(mate_below(anchor.value()));
    CHECK(bool(edges));
    if (edges) {
        // The identity and orientation are the palette-owned selected ones, and
        // the translation is the exact one the core derived.
        CHECK(edges.value().prototile().id() == PrototileId(1));
        CHECK(edges.value().orientation() == R);
        CHECK(edges.value().translation() == unit(0, -2));
        CHECK(has_vertex(edges.value().footprint(), unit(0, 0)));
        CHECK(has_vertex(edges.value().footprint(), unit(2, 0)));
    }

    auto vertices = observed.preview(mate_at_corner(anchor.value()));
    CHECK(bool(vertices));
    if (vertices) {
        CHECK(vertices.value().prototile().id() == PrototileId(1));
        CHECK(vertices.value().translation() == unit(2, 2));
    }

    // Neither preview touched a single observable value.
    CHECK(state.arrangement().entries().size() == 1);
    CHECK(state.arrangement().next_id().value() == PlacementId(1));
    CHECK(state.palette().order() == 1);
}

TEST_CASE("a successful preview consumes neither finite supply nor a placement id") {
    // Exactly one piece configured, and the anchor already uses it up... except
    // that supply is derived from arrangement contents, which preview cannot
    // change. So two pieces are configured and one is spent on the anchor.
    State state = square_state(Supply::finite(2).value());
    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    const std::size_t before_size = state.arrangement().entries().size();
    const PlacementId before_next = state.arrangement().next_id().value();

    // Repeated previews of the same command stay successful and identical while
    // the state is unmodified.
    auto first = state.preview(mate_below(anchor.value()));
    auto second = state.preview(mate_below(anchor.value()));
    auto third = state.preview(mate_at_corner(anchor.value()));
    CHECK(bool(first));
    CHECK(bool(second));
    CHECK(bool(third));
    if (!first || !second) {
        return;
    }
    CHECK(same_geometry(first.value(), second.value()));

    CHECK(state.arrangement().entries().size() == before_size);
    CHECK(state.arrangement().next_id().value() == before_next);

    // The second configured piece is still available to an actual apply, which
    // proves no preview spent it.
    auto applied = state.apply(mate_below(anchor.value()));
    CHECK(bool(applied));
    if (applied) {
        CHECK(applied.value() == PlacementId(1));
    }
    // And only now, with both pieces in the arrangement, is capacity used up.
    auto exhausted = state.preview(mate_at_corner(anchor.value()));
    CHECK(!exhausted);
    if (!exhausted) {
        CHECK(is_candidate(exhausted.error(), CandidateError::supply_exhausted));
    }
}

TEST_CASE("a successful preview and an immediate apply agree exactly") {
    auto built = tetromino_state();
    CHECK(built.has_value());
    if (!built) {
        return;
    }
    State state = std::move(built).value();

    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    auto previewed = state.preview(mate_below(anchor.value()));
    CHECK(bool(previewed));
    if (!previewed) {
        return;
    }

    auto applied = state.apply(mate_below(anchor.value()));
    CHECK(bool(applied));
    if (!applied) {
        return;
    }

    // The stored entry the returned id names carries the previewed geometry:
    // same prototile identity, orientation, translation, and footprint.
    const Entry &stored = state.arrangement().entries().back();
    CHECK(stored.id == applied.value());
    CHECK(same_geometry(stored.placement, previewed.value()));
}

TEST_CASE("preview retains the candidate precedence entry, orientation, then supply") {
    State state = square_state(Supply::unlimited());
    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));

    // Every core failure is simultaneously available, yet candidate resolution
    // answers first and no JoinError is published — exactly as apply behaves.
    auto bad_entry = state.preview(MateFullEdgesCommand {
        PlacementId(999),
        EdgeIndex(99),
        PaletteEntryIndex(9),
        PaletteOrientationIndex(9),
        EdgeIndex(99),
    });
    CHECK(!bad_entry);
    if (!bad_entry) {
        CHECK(is_candidate(bad_entry.error(), CandidateError::palette_entry_out_of_range));
        CHECK(std::get_if<JoinError>(&bad_entry.error()) == nullptr);
    }

    auto bad_orientation = state.preview(MateVerticesCommand {
        PlacementId(999),
        VertexIndex(99),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(9),
        VertexIndex(99),
    });
    CHECK(!bad_orientation);
    if (!bad_orientation) {
        CHECK(is_candidate(bad_orientation.error(), CandidateError::orientation_out_of_range));
        CHECK(std::get_if<JoinError>(&bad_orientation.error()) == nullptr);
    }

    // Supply comes last, and equally before any core mating.
    State finite = square_state(Supply::finite(1).value());
    auto used = finite.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(used));
    if (!used) {
        return;
    }
    auto spent = finite.preview(mate_below(used.value()));
    CHECK(!spent);
    if (!spent) {
        CHECK(is_candidate(spent.error(), CandidateError::supply_exhausted));
        CHECK(std::get_if<JoinError>(&spent.error()) == nullptr);
    }
    CHECK(finite.arrangement().entries().size() == 1);
}

TEST_CASE("a failed preview preserves the complete core JoinError and changes nothing") {
    State state = square_state(Supply::unlimited());
    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }
    auto blocker = state.apply(place_at(0, 0, unit(2, 0)));
    CHECK(bool(blocker));
    if (!blocker) {
        return;
    }

    const std::size_t before_size = state.arrangement().entries().size();
    const PlacementId before_next = state.arrangement().next_id().value();

    // An overlap preview names the conflicting placement, not just a code.
    auto overlapped = state.preview(MateFullEdgesCommand {
        anchor.value(),
        EdgeIndex(1),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(3),
    });
    CHECK(!overlapped);
    if (!overlapped) {
        const JoinError *error = std::get_if<JoinError>(&overlapped.error());
        CHECK(error != nullptr);
        if (error != nullptr) {
            CHECK(error->code == JoinErrorCode::interior_overlap);
            CHECK(error->conflicting_placement.value() == blocker.value());
        }
    }

    // Two same-direction edges carry no conflict identity.
    auto incompatible = state.preview(MateFullEdgesCommand {
        anchor.value(),
        EdgeIndex(0),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        EdgeIndex(0),
    });
    CHECK(!incompatible);
    if (!incompatible) {
        const JoinError *error = std::get_if<JoinError>(&incompatible.error());
        CHECK(error != nullptr);
        if (error != nullptr) {
            CHECK(error->code == JoinErrorCode::incompatible_edges);
            CHECK(error->conflicting_placement.has_value() == false);
        }
    }

    auto missing = state.preview(mate_below(PlacementId(999)));
    CHECK(!missing);
    if (!missing) {
        const JoinError *error = std::get_if<JoinError>(&missing.error());
        CHECK(error != nullptr && error->code == JoinErrorCode::anchor_not_found);
    }

    auto bad_vertex = state.preview(MateVerticesCommand {
        anchor.value(),
        VertexIndex(4),
        PaletteEntryIndex(0),
        PaletteOrientationIndex(0),
        VertexIndex(0),
    });
    CHECK(!bad_vertex);
    if (!bad_vertex) {
        const JoinError *error = std::get_if<JoinError>(&bad_vertex.error());
        CHECK(error != nullptr && error->code == JoinErrorCode::anchor_vertex_out_of_range);
    }

    CHECK(state.arrangement().entries().size() == before_size);
    CHECK(state.arrangement().next_id().value() == before_next);
    CHECK(state.arrangement().entries()[0].id == anchor.value());
    CHECK(state.arrangement().entries()[1].id == blocker.value());
}

TEST_CASE("preview uses the selected distinct orientation, not merely the entry") {
    std::vector<PaletteEntry> entries;
    entries.push_back(make_entry(bar(9), Supply::unlimited(), { R, Q, H, T }));
    State state(level_of(palette_of(std::move(entries))));
    CHECK(state.palette().entries()[0].orientations().size() == 2);

    auto anchor = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    // The anchor bar lies flat over [0,4]x[0,1]; its vertex 1 is the corner
    // (4,0), so an upright candidate mated there stands clear of it.
    const Placement &placed = state.arrangement().entries().front().placement;
    CHECK(placed.footprint().vertices().size() == 4);
    CHECK(placed.footprint().vertices()[1] == unit(4, 0));
    const VertexIndex corner = VertexIndex(1);

    auto upright = state.preview(MateVerticesCommand {
        anchor.value(),
        corner,
        PaletteEntryIndex(0),
        PaletteOrientationIndex(1),
        VertexIndex(0),
    });
    CHECK(bool(upright));
    if (!upright) {
        return;
    }
    CHECK(upright.value().orientation() == Q);
    CHECK(upright.value().prototile().id() == PrototileId(9));
    // The quarter turn stands the 4x1 bar upright: the preview carries the
    // selected distinct geometry, not the entry's first variant.
    CHECK(extent(upright.value().oriented_polygon())
        == std::make_pair(Coordinate::SCALE, 4 * Coordinate::SCALE));
}
