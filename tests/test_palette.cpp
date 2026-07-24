#include "TestHarness.h"

#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Prototile.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "engine/Palette.h"
#include "engine/Supply.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

using namespace tiles;
using tiles::engine::Palette;
using tiles::engine::PaletteEntry;
using tiles::engine::PaletteError;
using tiles::engine::Supply;
using tiles_test::raw_pt;

namespace {

Point unit(std::int64_t p_gx, std::int64_t p_gy) {
    return raw_pt(p_gx * Coordinate::SCALE, p_gy * Coordinate::SCALE);
}

// A 2x2 square: achiral, so it compiles to one distinct orientation.
Prototile square(std::uint64_t p_id) {
    auto polygon = Polygon::make({ unit(0, 0), unit(2, 0), unit(2, 2), unit(0, 2) });
    return std::move(Prototile::make(PrototileId(p_id), std::move(polygon).value())).value();
}

// The i tetromino: two distinct orientations under the quarter turns.
Prototile bar(std::uint64_t p_id) {
    auto polygon = Polygon::make({ unit(0, 0), unit(4, 0), unit(4, 1), unit(0, 1) });
    return std::move(Prototile::make(PrototileId(p_id), std::move(polygon).value())).value();
}

const Orientation R = Orientation::reference();
const Orientation Q = Orientation::quarter();
const Orientation H = Orientation::half();
const Orientation T = Orientation::three_quarter();

} // namespace

TEST_CASE("palette entry rejects an empty requested orientation set") {
    auto entry = PaletteEntry::make(square(1), Supply::unlimited(), {});
    CHECK(!entry);
    if (!entry) {
        CHECK(entry.error().code == LatticeOrientationErrorCode::empty_orientation_set);
    }
}

TEST_CASE("palette entry propagates an unsupported orientation error unchanged") {
    auto eighth = Orientation::make(1, 8);
    CHECK(bool(eighth));
    if (!eighth) {
        return;
    }
    auto entry = PaletteEntry::make(square(1), Supply::unlimited(), { eighth.value() });
    CHECK(!entry);
    if (!entry) {
        CHECK(entry.error().code == LatticeOrientationErrorCode::unsupported_orientation);
        CHECK(entry.error().orientation == std::optional<Orientation>(eighth.value()));
    }
}

TEST_CASE("a successful palette entry retains its prototile id and supply") {
    auto entry = PaletteEntry::make(square(7), Supply::finite(3).value(), { R, Q, H, T });
    CHECK(bool(entry));
    if (entry) {
        CHECK(entry.value().prototile().id() == PrototileId(7));
        CHECK(entry.value().supply() == Supply::finite(3).value());
        CHECK(!entry.value().orientations().empty());
    }
}

TEST_CASE("palette entry orientations are nonempty and deterministically ordered") {
    auto entry = PaletteEntry::make(bar(2), Supply::unlimited(), { R, Q, H, T });
    CHECK(bool(entry));
    if (entry) {
        const std::vector<OrientedPrototile> &os = entry.value().orientations();
        CHECK(os.size() == 2);
        if (os.size() == 2) {
            // Representative angular order: reference before quarter.
            CHECK(os[0].orientation() == R);
            CHECK(os[1].orientation() == Q);
        }
    }
}

TEST_CASE("a symmetric piece stores one distinct geometry group, not four raw labels") {
    auto entry = PaletteEntry::make(square(1), Supply::unlimited(), { R, Q, H, T });
    CHECK(bool(entry));
    if (entry) {
        const std::vector<OrientedPrototile> &os = entry.value().orientations();
        CHECK(os.size() == 1);
        if (os.size() == 1) {
            // The four requested angles collapse onto one boundary but remain
            // recorded as its complete equivalent group.
            CHECK(os[0].equivalent_orientations().size() == 4);
        }
    }
}

TEST_CASE("palette rejects an empty entry sequence") {
    auto palette = Palette::make({});
    CHECK(!palette);
    if (!palette) {
        CHECK(palette.error() == PaletteError::empty);
    }
}

TEST_CASE("palette rejects duplicate prototile ids despite differing geometry and supply") {
    std::vector<PaletteEntry> entries;
    entries.push_back(std::move(
        PaletteEntry::make(square(5), Supply::unlimited(), { R }).value()));
    entries.push_back(std::move(
        PaletteEntry::make(bar(5), Supply::finite(2).value(), { R }).value()));
    auto palette = Palette::make(std::move(entries));
    CHECK(!palette);
    if (!palette) {
        CHECK(palette.error() == PaletteError::duplicate_prototile_id);
    }
}

TEST_CASE("palette preserves authored order and counts entries, not variants") {
    std::vector<PaletteEntry> entries;
    entries.push_back(std::move(
        PaletteEntry::make(square(10), Supply::unlimited(), { R, Q, H, T }).value()));
    entries.push_back(std::move(
        PaletteEntry::make(bar(11), Supply::unlimited(), { R, Q, H, T }).value()));
    auto palette = Palette::make(std::move(entries));
    CHECK(bool(palette));
    if (palette) {
        CHECK(palette.value().order() == 2);
        CHECK(palette.value().entries()[0].prototile().id() == PrototileId(10));
        CHECK(palette.value().entries()[1].prototile().id() == PrototileId(11));
        // order() is the number of entries (2), not the total oriented variant
        // count (square 1 + bar 2 = 3).
        const std::size_t variants = palette.value().entries()[0].orientations().size()
            + palette.value().entries()[1].orientations().size();
        CHECK(variants == 3);
        CHECK(palette.value().order() != variants);
    }
}
