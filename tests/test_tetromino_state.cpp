#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Prototile.h"
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
