#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Prototile.h"
#include "core/Region.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/ExactInteger.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "engine/Palette.h"
#include "engine/State.h"
#include "engine/TetrominoState.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace tiles;
using tiles::engine::make_tetromino_state;
using tiles::engine::PaletteEntry;
using tiles::engine::State;
using tiles_test::raw_pt;

namespace {

Point game_unit(std::int64_t p_gx, std::int64_t p_gy) {
    return raw_pt(p_gx * Coordinate::SCALE, p_gy * Coordinate::SCALE);
}

} // namespace

TEST_CASE("tetromino state construction succeeds") {
    auto built = make_tetromino_state();
    CHECK(bool(built));
}

TEST_CASE("tetromino state arrangement is empty") {
    auto built = make_tetromino_state();
    CHECK(bool(built));
    if (built) {
        CHECK(built.value().arrangement().entries().empty());
    }
}

TEST_CASE("tetromino state carries a valid no-hole debug region") {
    auto built = make_tetromino_state();
    CHECK(bool(built));
    if (!built) {
        return;
    }
    const State &state = built.value();

    CHECK(state.region().inner_boundaries().empty());
    CHECK(state.region().outer_boundary().vertices().size() == 4);
    // The authored bounds: x in [-8, 36], y in [-30, 8] game units.
    CHECK(state.region().outer_boundary().vertices().front() == game_unit(-8, -30));
    CHECK(state.region().doubled_area()
        == Int256::multiply(
            static_cast<__int128>(2 * 44 * 38) * Coordinate::SCALE, Coordinate::SCALE));
    // An empty arrangement over a positive-area region is not solved.
    CHECK(state.solved() == false);
}

TEST_CASE("the debug region contains the whole debug arrangement extent") {
    auto built = make_tetromino_state();
    CHECK(bool(built));
    if (!built) {
        return;
    }
    const Region &region = built.value().region();

    // The orientation grid spans x in [0, 28], y in [-16, 0] and the overlap
    // fixture reaches y = -22; a mated tetromino reaches at most four units
    // past an anchor. All of that is inside, and the margin ends where the
    // authored bounds do.
    auto grid = Polygon::make({
        game_unit(0, -16), game_unit(28, -16), game_unit(28, 0), game_unit(0, 0) });
    CHECK(bool(grid));
    if (grid) {
        CHECK(region.contains(grid.value()));
    }

    auto with_margin = Polygon::make({
        game_unit(-4, -26), game_unit(32, -26), game_unit(32, 4), game_unit(-4, 4) });
    CHECK(bool(with_margin));
    if (with_margin) {
        CHECK(region.contains(with_margin.value()));
    }

    auto beyond = Polygon::make({
        game_unit(36, -30), game_unit(40, -30), game_unit(40, 8), game_unit(36, 8) });
    CHECK(bool(beyond));
    if (beyond) {
        CHECK(region.contains(beyond.value()) == false);
    }
}

TEST_CASE("tetromino palette order and ids are o,i,t,s,z,j,l / 1..7") {
    auto built = make_tetromino_state();
    CHECK(bool(built));
    if (!built) {
        return;
    }
    const State &state = built.value();
    CHECK(state.palette().order() == 7);
    const std::vector<PaletteEntry> &entries = state.palette().entries();
    CHECK(entries.size() == 7);
    const std::uint64_t expected_ids[7] = { 1, 2, 3, 4, 5, 6, 7 };
    for (std::size_t i = 0; i < entries.size() && i < 7; ++i) {
        CHECK(entries[i].prototile().id() == PrototileId(expected_ids[i]));
    }
}

TEST_CASE("every tetromino supply is unlimited") {
    auto built = make_tetromino_state();
    CHECK(bool(built));
    if (!built) {
        return;
    }
    for (const PaletteEntry &entry : built.value().palette().entries()) {
        CHECK(entry.supply().is_unlimited());
    }
}

TEST_CASE("per-entry distinct orientation counts are 1,2,4,2,2,4,4 totalling nineteen") {
    auto built = make_tetromino_state();
    CHECK(bool(built));
    if (!built) {
        return;
    }
    const std::size_t expected[7] = { 1, 2, 4, 2, 2, 4, 4 };
    const std::vector<PaletteEntry> &entries = built.value().palette().entries();
    CHECK(entries.size() == 7);
    std::size_t total = 0;
    for (std::size_t i = 0; i < entries.size() && i < 7; ++i) {
        CHECK(entries[i].orientations().size() == expected[i]);
        total += entries[i].orientations().size();
    }
    CHECK(total == 19);
}

TEST_CASE("tetromino representatives and equivalent groups agree with the core compiler") {
    auto built = make_tetromino_state();
    CHECK(bool(built));
    if (!built) {
        return;
    }
    const std::vector<Orientation> requested = {
        Orientation::reference(),
        Orientation::quarter(),
        Orientation::half(),
        Orientation::three_quarter(),
    };
    for (const PaletteEntry &entry : built.value().palette().entries()) {
        auto recompiled = compile_lattice_orientations(entry.prototile(), requested);
        CHECK(bool(recompiled));
        if (!recompiled) {
            continue;
        }
        const std::vector<OrientedPrototile> &engine_variants = entry.orientations();
        const std::vector<OrientedPrototile> &core_variants = recompiled.value();
        CHECK(engine_variants.size() == core_variants.size());
        for (std::size_t i = 0; i < engine_variants.size() && i < core_variants.size(); ++i) {
            CHECK(engine_variants[i].orientation() == core_variants[i].orientation());
            CHECK(engine_variants[i].equivalent_orientations()
                == core_variants[i].equivalent_orientations());
            CHECK(same_boundary(
                engine_variants[i].canonical_polygon(),
                core_variants[i].canonical_polygon()));
        }
    }
}

TEST_CASE("every tetromino reference and oriented polygon is valid core geometry") {
    auto built = make_tetromino_state();
    CHECK(bool(built));
    if (!built) {
        return;
    }
    for (const PaletteEntry &entry : built.value().palette().entries()) {
        const Polygon &reference = entry.prototile().polygon();
        CHECK(reference.vertices().size() >= 3);
        CHECK(reference.triangulation().size() == reference.vertices().size() - 2);
        for (const OrientedPrototile &oriented : entry.orientations()) {
            const Polygon &polygon = oriented.canonical_polygon();
            CHECK(polygon.vertices().size() >= 3);
            CHECK(polygon.triangulation().size() == polygon.vertices().size() - 2);
        }
    }
}
