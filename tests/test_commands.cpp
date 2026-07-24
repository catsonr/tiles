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
#include "engine/Commands.h"
#include "engine/Palette.h"
#include "engine/State.h"
#include "engine/Supply.h"
#include "engine/TetrominoState.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

using namespace tiles;
using tiles::engine::CandidateError;
using tiles::engine::MateCommandError;
using tiles::engine::MateFullEdgesCommand;
using tiles::engine::MateVerticesCommand;
using tiles::engine::Palette;
using tiles::engine::PaletteEntry;
using tiles::engine::PaletteEntryIndex;
using tiles::engine::PaletteOrientationIndex;
using tiles::engine::PlaceCommand;
using tiles::engine::PlaceCommandError;
using tiles::engine::State;
using tiles::engine::Supply;
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

Palette square_palette(Supply p_supply) {
    std::vector<PaletteEntry> entries;
    entries.push_back(make_entry(square(1), p_supply, { R }));
    return palette_of(std::move(entries));
}

// One 2x2 square entry with the reference orientation only, over an empty
// arrangement: orientation index 1 is therefore always out of range.
State square_state(Supply p_supply) {
    return State(square_palette(p_supply), Arrangement());
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
    State state(palette_of(std::move(entries)), Arrangement());
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
    State state(palette_of(std::move(entries)), Arrangement());
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
    State state(
        square_palette(Supply::unlimited()), Arrangement::testing_with_next_id(UINT64_MAX));

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
    State state(palette_of(std::move(entries)), Arrangement());
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
    State state(palette_of(std::move(entries)), Arrangement());

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

TEST_CASE("a matching placement preloaded through the constructor consumes supply") {
    Arrangement preloaded;
    auto placement = Placement::make(reference_orientation(square(1)), unit(0, 0));
    CHECK(bool(placement));
    if (!placement) {
        return;
    }
    CHECK(bool(preloaded.try_insert(std::move(placement).value())));

    // Supply identity is the exact PrototileId, so it makes no difference that
    // this placement did not enter through an engine command.
    State state(square_palette(Supply::finite(1).value()), std::move(preloaded));
    auto applied = state.apply(place_at(0, 0, unit(4, 0)));
    CHECK(!applied);
    if (!applied) {
        CHECK(is_candidate(applied.error(), CandidateError::supply_exhausted));
    }
    CHECK(state.arrangement().entries().size() == 1);
}

TEST_CASE("an arrangement entry absent from the palette is tolerated and left unchanged") {
    Arrangement preloaded;
    auto foreign = Placement::make(reference_orientation(bar(99)), unit(0, 10));
    CHECK(bool(foreign));
    if (!foreign) {
        return;
    }
    CHECK(bool(preloaded.try_insert(std::move(foreign).value())));

    // The palette offers only prototile 1; prototile 99 is neither rejected nor
    // rewritten, and does not count against the square's supply.
    State state(square_palette(Supply::finite(1).value()), std::move(preloaded));
    auto applied = state.apply(place_at(0, 0, unit(0, 0)));
    CHECK(bool(applied));
    if (applied) {
        CHECK(applied.value() == PlacementId(1));
    }
    CHECK(state.arrangement().entries().size() == 2);
    CHECK(state.arrangement().entries()[0].id == PlacementId(0));
    CHECK(state.arrangement().entries()[0].placement.prototile().id() == PrototileId(99));
    CHECK(state.arrangement().entries()[0].placement.translation() == unit(0, 10));
}

TEST_CASE("finite maximum uint64 supply has no counting overflow path") {
    State state = square_state(Supply::finite(UINT64_MAX).value());
    CHECK(state.palette().entries()[0].supply().finite_amount().value() == UINT64_MAX);

    // Usage is counted only up to the configured amount, so a maximal capacity
    // never approaches an addition that could leave the type.
    CHECK(bool(state.apply(place_at(0, 0, unit(0, 0)))));
    CHECK(bool(state.apply(place_at(0, 0, unit(2, 0)))));
    CHECK(bool(state.apply(place_at(0, 0, unit(4, 0)))));
    CHECK(state.arrangement().entries().size() == 3);
}

// ---------------------------------------------------------------------------
// feature mating
// ---------------------------------------------------------------------------

TEST_CASE("full-edge mating resolves its candidate through the palette and succeeds") {
    auto built = tiles::engine::make_tetromino_state();
    CHECK(bool(built));
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
    State state(palette_of(std::move(entries)), Arrangement());

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
