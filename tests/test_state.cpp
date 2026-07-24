#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "engine/Palette.h"
#include "engine/State.h"
#include "engine/Supply.h"

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

using namespace tiles;
using tiles::engine::Palette;
using tiles::engine::PaletteEntry;
using tiles::engine::State;
using tiles::engine::Supply;
using tiles_test::raw_pt;
using tiles_test::reference_orientation;

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

} // namespace

TEST_CASE("state owns and exposes the supplied palette") {
    State state(one_square_palette(), Arrangement());
    CHECK(state.palette().order() == 1);
    CHECK(state.palette().entries()[0].prototile().id() == PrototileId(1));
}

TEST_CASE("state owns and exposes an empty arrangement") {
    State state(one_square_palette(), Arrangement());
    CHECK(state.arrangement().entries().empty());
}

TEST_CASE("state can own and expose an independently constructed nonempty arrangement") {
    Arrangement arrangement;
    const OrientedPrototile oriented = reference_orientation(square(1));
    auto placement = Placement::make(oriented, raw_pt(0, 0));
    CHECK(bool(placement));
    if (!placement) {
        return;
    }
    auto inserted = arrangement.try_insert(std::move(placement).value());
    CHECK(bool(inserted));

    State state(one_square_palette(), std::move(arrangement));
    CHECK(state.arrangement().entries().size() == 1);
}

TEST_CASE("state exposes exact core polygon vertices through read-only chains") {
    State state(one_square_palette(), Arrangement());
    const Polygon::Vertices &vertices =
        state.palette().entries()[0].prototile().polygon().vertices();
    CHECK(vertices.size() == 4);
    // The normalized square keeps its lexicographically smallest vertex at the
    // local origin, and coordinates stay exact q16.48 points.
    CHECK(vertices.front() == raw_pt(0, 0));
    CHECK(vertices[1] == unit(2, 0));
}

TEST_CASE("state exposes no mutable palette or arrangement reference") {
    static_assert(
        std::is_same_v<decltype(std::declval<State &>().palette()), const Palette &>,
        "palette() must expose only a const reference");
    static_assert(
        std::is_same_v<decltype(std::declval<State &>().arrangement()), const Arrangement &>,
        "arrangement() must expose only a const reference");
    CHECK(true);
}
