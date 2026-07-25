#include "TestHarness.h"

#include "core/Hex12.h"
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
using tiles::engine::PaletteEntryCompilationErrorCode;
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

Orientation twelfth(std::uint32_t p_step) {
    return Orientation::make(p_step, 12).value();
}

std::vector<Orientation> all_twelfth_turns() {
    std::vector<Orientation> requested;
    for (std::uint32_t k = 0; k < 12; ++k) {
        requested.push_back(twelfth(k));
    }
    return requested;
}

// Proof-bearing oriented values, obtained only from the ordinary core
// compilers. There is deliberately no unchecked construction seam: every
// synthesized sequence below is assembled out of values a real compiler
// published.
std::vector<OrientedPrototile> lattice_groups(
    const Prototile &p_prototile, std::vector<Orientation> p_requested) {
    auto compiled = compile_lattice_orientations(p_prototile, std::move(p_requested));
    return compiled ? std::move(compiled).value() : std::vector<OrientedPrototile> {};
}

std::vector<OrientedPrototile> hex12_groups(
    std::uint64_t p_id, Hex12RegularPolygon p_polygon, std::vector<Orientation> p_requested) {
    auto compiled =
        compile_hex12_orientations(PrototileId(p_id), p_polygon, std::move(p_requested));
    return compiled ? std::move(compiled).value() : std::vector<OrientedPrototile> {};
}

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

// ---------------------------------------------------------------------------
// checked precompiled entries
// ---------------------------------------------------------------------------

TEST_CASE("a precompiled entry rejects an empty orientation sequence") {
    auto entry = PaletteEntry::make_compiled(Supply::unlimited(), {});
    CHECK(!entry);
    if (!entry) {
        CHECK(entry.error().code == PaletteEntryCompilationErrorCode::empty_orientations);
        CHECK(entry.error().orientation.has_value() == false);
        CHECK(entry.error().conflicting_orientation.has_value() == false);
    }
}

TEST_CASE("a precompiled lattice entry keeps its supply and complete orientations") {
    std::vector<OrientedPrototile> compiled = lattice_groups(bar(2), { R, Q, H, T });
    CHECK(compiled.size() == 2);

    auto entry = PaletteEntry::make_compiled(Supply::finite(5).value(), compiled);
    CHECK(bool(entry));
    if (!entry) {
        return;
    }
    CHECK(entry.value().prototile().id() == PrototileId(2));
    CHECK(same_boundary(entry.value().prototile().polygon(), bar(2).polygon()));
    CHECK(entry.value().supply() == Supply::finite(5).value());
    CHECK(entry.value().orientations().size() == compiled.size());
    for (std::size_t i = 0; i < entry.value().orientations().size(); ++i) {
        CHECK(entry.value().orientations()[i].orientation() == compiled[i].orientation());
        CHECK(
            entry.value().orientations()[i].equivalent_orientations()
            == compiled[i].equivalent_orientations());
        CHECK(same_boundary(
            entry.value().orientations()[i].canonical_polygon(),
            compiled[i].canonical_polygon()));
    }
}

TEST_CASE("a precompiled hex-12 entry publishes all four triangle variants") {
    std::vector<OrientedPrototile> compiled =
        hex12_groups(35, Hex12RegularPolygon::triangle, all_twelfth_turns());
    CHECK(compiled.size() == 4);

    auto entry = PaletteEntry::make_compiled(Supply::unlimited(), compiled);
    CHECK(bool(entry));
    if (!entry) {
        return;
    }
    CHECK(entry.value().prototile().id() == PrototileId(35));
    CHECK(entry.value().supply().is_unlimited());
    CHECK(entry.value().orientations().size() == 4);
    // The representatives are the four twelfth-turn classes, in angular order.
    const std::uint32_t expected[4] = { 0, 1, 2, 3 };
    for (std::size_t i = 0; i < entry.value().orientations().size() && i < 4; ++i) {
        CHECK(entry.value().orientations()[i].orientation() == twelfth(expected[i]));
        CHECK(entry.value().orientations()[i].equivalent_orientations().size() == 3);
    }
}

TEST_CASE("a precompiled entry rejects an inconsistent prototile id") {
    // Two identical bars under different identities: the id disagrees at index
    // one, which is answered before the representatives are compared.
    std::vector<OrientedPrototile> compiled = lattice_groups(bar(2), { R });
    std::vector<OrientedPrototile> other = lattice_groups(bar(3), { R });
    CHECK(compiled.size() == 1);
    CHECK(other.size() == 1);
    compiled.push_back(other.front());

    auto entry = PaletteEntry::make_compiled(Supply::unlimited(), compiled);
    CHECK(!entry);
    if (!entry) {
        CHECK(entry.error().code
            == PaletteEntryCompilationErrorCode::inconsistent_prototile_id);
        CHECK(entry.error().orientation == std::optional<std::size_t>(1));
        CHECK(entry.error().conflicting_orientation.has_value() == false);
    }
}

TEST_CASE("a precompiled entry rejects an inconsistent reference boundary") {
    // One identity, two different reference geometries.
    std::vector<OrientedPrototile> compiled = lattice_groups(square(5), { R });
    std::vector<OrientedPrototile> other = lattice_groups(bar(5), { Q });
    CHECK(compiled.size() == 1);
    CHECK(other.size() == 1);
    compiled.push_back(other.front());

    auto entry = PaletteEntry::make_compiled(Supply::unlimited(), compiled);
    CHECK(!entry);
    if (!entry) {
        CHECK(entry.error().code
            == PaletteEntryCompilationErrorCode::inconsistent_reference_boundary);
        CHECK(entry.error().orientation == std::optional<std::size_t>(1));
        CHECK(entry.error().conflicting_orientation.has_value() == false);
    }
}

TEST_CASE("a precompiled entry rejects representatives which do not strictly increase") {
    std::vector<OrientedPrototile> compiled = lattice_groups(bar(2), { R, Q });
    CHECK(compiled.size() == 2);

    // Descending.
    std::vector<OrientedPrototile> descending = { compiled[1], compiled[0] };
    auto reversed = PaletteEntry::make_compiled(Supply::unlimited(), descending);
    CHECK(!reversed);
    if (!reversed) {
        CHECK(reversed.error().code
            == PaletteEntryCompilationErrorCode::representatives_not_strictly_ordered);
        CHECK(reversed.error().orientation == std::optional<std::size_t>(0));
        CHECK(reversed.error().conflicting_orientation == std::optional<std::size_t>(1));
    }

    // Equal is not strictly increasing either, and it is answered before the
    // duplicate label and duplicate boundary the same pair also carries.
    std::vector<OrientedPrototile> repeated = { compiled[0], compiled[0] };
    auto tied = PaletteEntry::make_compiled(Supply::unlimited(), repeated);
    CHECK(!tied);
    if (!tied) {
        CHECK(tied.error().code
            == PaletteEntryCompilationErrorCode::representatives_not_strictly_ordered);
        CHECK(tied.error().orientation == std::optional<std::size_t>(0));
        CHECK(tied.error().conflicting_orientation == std::optional<std::size_t>(1));
    }
}

TEST_CASE("a precompiled entry rejects one orientation label naming two groups") {
    // The whole first triangle class, then the same class compiled from two of
    // its own members: distinct increasing representatives, shared labels.
    std::vector<OrientedPrototile> compiled =
        hex12_groups(35, Hex12RegularPolygon::triangle, all_twelfth_turns());
    std::vector<OrientedPrototile> partial = hex12_groups(
        35, Hex12RegularPolygon::triangle, { twelfth(4), twelfth(8) });
    CHECK(compiled.size() == 4);
    CHECK(partial.size() == 1);
    if (compiled.size() != 4 || partial.size() != 1) {
        return;
    }

    std::vector<OrientedPrototile> synthesized = { compiled[0], partial[0] };
    CHECK(synthesized[0].orientation() < synthesized[1].orientation());

    auto entry = PaletteEntry::make_compiled(Supply::unlimited(), synthesized);
    CHECK(!entry);
    if (!entry) {
        // The shared label is answered before the shared boundary these same
        // two groups also carry.
        CHECK(entry.error().code
            == PaletteEntryCompilationErrorCode::duplicate_orientation_label);
        CHECK(entry.error().orientation == std::optional<std::size_t>(0));
        CHECK(entry.error().conflicting_orientation == std::optional<std::size_t>(1));
    }
}

TEST_CASE("a precompiled entry rejects two groups sharing one canonical boundary") {
    // Two equivalent triangle phases compiled separately: disjoint labels,
    // strictly increasing representatives, identical geometry.
    std::vector<OrientedPrototile> first =
        hex12_groups(35, Hex12RegularPolygon::triangle, { twelfth(0) });
    std::vector<OrientedPrototile> second =
        hex12_groups(35, Hex12RegularPolygon::triangle, { twelfth(4) });
    CHECK(first.size() == 1);
    CHECK(second.size() == 1);
    if (first.size() != 1 || second.size() != 1) {
        return;
    }

    std::vector<OrientedPrototile> synthesized = { first[0], second[0] };
    CHECK(synthesized[0].orientation() < synthesized[1].orientation());
    CHECK(same_boundary(
        synthesized[0].canonical_polygon(), synthesized[1].canonical_polygon()));

    auto entry = PaletteEntry::make_compiled(Supply::unlimited(), synthesized);
    CHECK(!entry);
    if (!entry) {
        CHECK(entry.error().code
            == PaletteEntryCompilationErrorCode::duplicate_canonical_boundary);
        CHECK(entry.error().orientation == std::optional<std::size_t>(0));
        CHECK(entry.error().conflicting_orientation == std::optional<std::size_t>(1));
    }
}

TEST_CASE("precompiled entry precedence is exact when several defects coexist") {
    std::vector<OrientedPrototile> bars = lattice_groups(bar(2), { R, Q });
    std::vector<OrientedPrototile> squares = lattice_groups(square(2), { R });
    std::vector<OrientedPrototile> renamed = lattice_groups(bar(9), { R });
    CHECK(bars.size() == 2);
    CHECK(squares.size() == 1);
    CHECK(renamed.size() == 1);
    if (bars.size() != 2 || squares.size() != 1 || renamed.size() != 1) {
        return;
    }

    {
        // Index one disagrees about geometry, index two about identity. The
        // earlier index answers, and it answers as a boundary failure.
        std::vector<OrientedPrototile> synthesized = { bars[0], squares[0], renamed[0] };
        auto entry = PaletteEntry::make_compiled(Supply::unlimited(), synthesized);
        CHECK(!entry);
        if (!entry) {
            CHECK(entry.error().code
                == PaletteEntryCompilationErrorCode::inconsistent_reference_boundary);
            CHECK(entry.error().orientation == std::optional<std::size_t>(1));
        }
    }

    {
        // The same index carrying both an identity and a geometry disagreement
        // answers as identity.
        std::vector<OrientedPrototile> synthesized = { bars[0], squares[0] };
        std::vector<OrientedPrototile> foreign = lattice_groups(square(4), { R });
        CHECK(foreign.size() == 1);
        if (foreign.size() == 1) {
            synthesized[1] = foreign[0];
            auto entry = PaletteEntry::make_compiled(Supply::unlimited(), synthesized);
            CHECK(!entry);
            if (!entry) {
                CHECK(entry.error().code
                    == PaletteEntryCompilationErrorCode::inconsistent_prototile_id);
                CHECK(entry.error().orientation == std::optional<std::size_t>(1));
            }
        }
    }

    {
        // A consistent entry whose groups descend is an ordering failure, even
        // though every label and boundary is distinct.
        std::vector<OrientedPrototile> synthesized = { bars[1], bars[0] };
        auto entry = PaletteEntry::make_compiled(Supply::unlimited(), synthesized);
        CHECK(!entry);
        if (!entry) {
            CHECK(entry.error().code
                == PaletteEntryCompilationErrorCode::representatives_not_strictly_ordered);
        }
    }
}

TEST_CASE("the lattice convenience factory agrees with the checked precompiled path") {
    const std::vector<Orientation> requested = { R, Q, H, T };
    auto made = PaletteEntry::make(bar(6), Supply::finite(2).value(), requested);
    auto compiled = PaletteEntry::make_compiled(
        Supply::finite(2).value(), lattice_groups(bar(6), requested));
    CHECK(bool(made));
    CHECK(bool(compiled));
    if (!made || !compiled) {
        return;
    }
    CHECK(made.value().prototile().id() == compiled.value().prototile().id());
    CHECK(made.value().supply() == compiled.value().supply());
    CHECK(made.value().orientations().size() == compiled.value().orientations().size());
    for (std::size_t i = 0; i < made.value().orientations().size(); ++i) {
        CHECK(
            made.value().orientations()[i].equivalent_orientations()
            == compiled.value().orientations()[i].equivalent_orientations());
        CHECK(same_boundary(
            made.value().orientations()[i].canonical_polygon(),
            compiled.value().orientations()[i].canonical_polygon()));
    }
}

TEST_CASE("a palette accepts entries published through either factory") {
    std::vector<engine::PaletteEntry> entries;
    entries.push_back(std::move(
        PaletteEntry::make(square(27), Supply::unlimited(), { R, Q, H, T }).value()));
    entries.push_back(std::move(PaletteEntry::make_compiled(
        Supply::finite(4).value(),
        hex12_groups(36, Hex12RegularPolygon::hexagon, all_twelfth_turns()))
                                    .value()));
    auto palette = Palette::make(std::move(entries));
    CHECK(bool(palette));
    if (palette) {
        CHECK(palette.value().order() == 2);
        CHECK(palette.value().entries()[0].orientations().size() == 1);
        CHECK(palette.value().entries()[1].orientations().size() == 2);
    }
}
