#include "TestHarness.h"

#include "content/CanonicalOrientationCompiler.h"
#include "content/GeometryDomain.h"
#include "content/PrototileCatalog.h"
#include "core/Arrangement.h"
#include "core/Hex12.h"
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
#include "engine/State.h"
#include "engine/Supply.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

using namespace tiles;
using tiles::content::CanonicalOrientationCompilationErrorCode;
using tiles::content::CanonicalPrototile;
using tiles::content::GeometryDomain;
using tiles::content::PrototileCatalog;
using tiles::engine::PaletteEntry;
using tiles::engine::PaletteEntryIndex;
using tiles::engine::PaletteOrientationIndex;
using tiles::engine::Supply;
using tiles_test::raw_pt;

namespace {

constexpr std::size_t LATTICE_VIEW_SIZE = 34;
constexpr std::size_t HEX12_VIEW_SIZE = 4;

Point unit(std::int64_t p_gx, std::int64_t p_gy) {
    return raw_pt(p_gx * Coordinate::SCALE, p_gy * Coordinate::SCALE);
}

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

const std::vector<Orientation> &quarter_turns() {
    static const std::vector<Orientation> requested = {
        Orientation::reference(),
        Orientation::quarter(),
        Orientation::half(),
        Orientation::three_quarter(),
    };
    return requested;
}

std::optional<PrototileCatalog> canonical() {
    auto built = content::make_canonical_prototile_catalog();
    if (!built) {
        return std::nullopt;
    }
    return std::move(built).value();
}

// The act-0 symmetry tables, authored here as twelfth-turn steps rather than
// read back out of any production table.
struct HexExpectation final {
    PrototileId::Value id;
    Hex12RegularPolygon polygon;
    std::vector<std::vector<std::uint32_t>> groups;
};

const std::vector<HexExpectation> &hex_expectations() {
    static const std::vector<HexExpectation> expectations = {
        { 35, Hex12RegularPolygon::triangle,
            { { 0, 4, 8 }, { 1, 5, 9 }, { 2, 6, 10 }, { 3, 7, 11 } } },
        { 27, Hex12RegularPolygon::square,
            { { 0, 3, 6, 9 }, { 1, 4, 7, 10 }, { 2, 5, 8, 11 } } },
        { 36, Hex12RegularPolygon::hexagon,
            { { 0, 2, 4, 6, 8, 10 }, { 1, 3, 5, 7, 9, 11 } } },
        { 37, Hex12RegularPolygon::dodecagon,
            { { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } } },
    };
    return expectations;
}

bool same_groups(
    const std::vector<OrientedPrototile> &p_lhs,
    const std::vector<OrientedPrototile> &p_rhs) {
    if (p_lhs.size() != p_rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_lhs.size(); ++i) {
        if (p_lhs[i].prototile().id() != p_rhs[i].prototile().id()) {
            return false;
        }
        if (!same_boundary(p_lhs[i].prototile().polygon(), p_rhs[i].prototile().polygon())) {
            return false;
        }
        if (p_lhs[i].equivalent_orientations() != p_rhs[i].equivalent_orientations()) {
            return false;
        }
        if (!same_boundary(p_lhs[i].canonical_polygon(), p_rhs[i].canonical_polygon())) {
            return false;
        }
    }
    return true;
}

// A generous axis-aligned square region, large enough to contain every hex-12
// footprint the downstream test places. It is ordinary lattice geometry: the
// engine has no idea which domain compiled what it holds.
std::optional<Region> square_region(std::int64_t p_half_side) {
    auto polygon = Polygon::make({
        unit(-p_half_side, -p_half_side),
        unit(p_half_side, -p_half_side),
        unit(p_half_side, p_half_side),
        unit(-p_half_side, p_half_side),
    });
    if (!polygon) {
        return std::nullopt;
    }
    auto region = Region::make(std::move(polygon).value(), {});
    if (!region) {
        return std::nullopt;
    }
    return std::move(region).value();
}

// One palette entry compiled through the act-1 public path: canonical entry,
// domain, checked precompiled publication.
std::optional<PaletteEntry> domain_entry(
    const PrototileCatalog &p_catalog,
    GeometryDomain p_domain,
    PrototileId::Value p_id,
    Supply p_supply) {
    const CanonicalPrototile *canonical_entry = p_catalog.find(PrototileId(p_id));
    if (canonical_entry == nullptr) {
        return std::nullopt;
    }
    auto compiled = content::compile_canonical_orientations(p_domain, *canonical_entry);
    if (!compiled) {
        return std::nullopt;
    }
    auto entry = PaletteEntry::make_compiled(p_supply, std::move(compiled).value());
    if (!entry) {
        return std::nullopt;
    }
    return std::move(entry).value();
}

} // namespace

// ---------------------------------------------------------------------------
// lattice compilation
// ---------------------------------------------------------------------------

TEST_CASE("every lattice entry compiles exactly what the lattice compiler produces") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::vector<const CanonicalPrototile *> view =
        catalog->entries_for(GeometryDomain::lattice);
    CHECK(view.size() == LATTICE_VIEW_SIZE);

    for (const CanonicalPrototile *entry : view) {
        auto compiled =
            content::compile_canonical_orientations(GeometryDomain::lattice, *entry);
        CHECK(bool(compiled));
        auto direct = compile_lattice_orientations(entry->prototile(), quarter_turns());
        CHECK(bool(direct));
        if (!compiled || !direct) {
            continue;
        }
        CHECK(same_groups(compiled.value(), direct.value()));
    }
}

TEST_CASE("representative lattice entries keep their established group counts") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    // Authored here, not read from the catalog: the o tetromino, the i
    // tetromino, the t tetromino, the x pentomino, the domino, and the unit
    // square.
    const std::pair<PrototileId::Value, std::size_t> expectations[6] = {
        { 1, 1 }, { 2, 2 }, { 3, 4 }, { 21, 1 }, { 26, 2 }, { 27, 1 },
    };
    for (const auto &expectation : expectations) {
        const CanonicalPrototile *entry = catalog->find(PrototileId(expectation.first));
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        auto compiled =
            content::compile_canonical_orientations(GeometryDomain::lattice, *entry);
        CHECK(bool(compiled));
        if (compiled) {
            CHECK(compiled.value().size() == expectation.second);
            CHECK(compiled.value().front().prototile().id()
                == PrototileId(expectation.first));
        }
    }
}

// ---------------------------------------------------------------------------
// hex-12 compilation
// ---------------------------------------------------------------------------

TEST_CASE("the hex-12 view compiles into groups of four, three, two, and one") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::vector<const CanonicalPrototile *> view =
        catalog->entries_for(GeometryDomain::hex12);
    CHECK(view.size() == HEX12_VIEW_SIZE);
    if (view.size() != HEX12_VIEW_SIZE) {
        return;
    }

    const std::size_t expected_counts[HEX12_VIEW_SIZE] = { 4, 3, 2, 1 };
    for (std::size_t i = 0; i < HEX12_VIEW_SIZE; ++i) {
        auto compiled =
            content::compile_canonical_orientations(GeometryDomain::hex12, *view[i]);
        CHECK(bool(compiled));
        if (compiled) {
            CHECK(compiled.value().size() == expected_counts[i]);
        }
    }
}

TEST_CASE("hex-12 equivalence-label tables are exactly the act-0 tables") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    for (const HexExpectation &expectation : hex_expectations()) {
        const CanonicalPrototile *entry = catalog->find(PrototileId(expectation.id));
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        auto compiled =
            content::compile_canonical_orientations(GeometryDomain::hex12, *entry);
        CHECK(bool(compiled));
        if (!compiled) {
            continue;
        }
        CHECK(compiled.value().size() == expectation.groups.size());
        if (compiled.value().size() != expectation.groups.size()) {
            continue;
        }
        for (std::size_t g = 0; g < expectation.groups.size(); ++g) {
            const std::vector<std::uint32_t> &steps = expectation.groups[g];
            CHECK(compiled.value()[g].orientation() == twelfth(steps.front()));
            CHECK(compiled.value()[g].equivalent_orientations().size() == steps.size());
            if (compiled.value()[g].equivalent_orientations().size() != steps.size()) {
                continue;
            }
            for (std::size_t k = 0; k < steps.size(); ++k) {
                CHECK(compiled.value()[g].equivalent_orientations()[k] == twelfth(steps[k]));
            }
        }

        // And the whole product is exactly what the ordinary hex-12 compiler
        // returns for the same identity and source: nothing is re-sorted,
        // re-grouped, or deduplicated on the way through.
        auto direct = compile_hex12_orientations(
            PrototileId(expectation.id), expectation.polygon, all_twelfth_turns());
        CHECK(bool(direct));
        if (direct) {
            CHECK(same_groups(compiled.value(), direct.value()));
        }
    }
}

TEST_CASE("the shared unit square compiles differently in each domain") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const CanonicalPrototile *square = catalog->find(PrototileId(27));
    CHECK(square != nullptr);
    if (square == nullptr) {
        return;
    }

    auto lattice = content::compile_canonical_orientations(GeometryDomain::lattice, *square);
    auto hex12 = content::compile_canonical_orientations(GeometryDomain::hex12, *square);
    CHECK(bool(lattice));
    CHECK(bool(hex12));
    if (!lattice || !hex12) {
        return;
    }
    CHECK(lattice.value().size() == 1);
    CHECK(hex12.value().size() == 3);
    // One identity and one reference boundary; only the admitted orientations
    // differ.
    CHECK(lattice.value().front().prototile().id() == PrototileId(27));
    CHECK(hex12.value().front().prototile().id() == PrototileId(27));
    CHECK(same_boundary(
        lattice.value().front().prototile().polygon(),
        hex12.value().front().prototile().polygon()));
    CHECK(same_boundary(
        lattice.value().front().canonical_polygon(),
        hex12.value().front().canonical_polygon()));
}

// ---------------------------------------------------------------------------
// domain refusals
// ---------------------------------------------------------------------------

TEST_CASE("a canonical entry compiles only through a domain it supports") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }

    // A lattice-only identity refused by hex-12.
    const CanonicalPrototile *tetromino = catalog->find(PrototileId(1));
    CHECK(tetromino != nullptr);
    if (tetromino != nullptr) {
        auto compiled =
            content::compile_canonical_orientations(GeometryDomain::hex12, *tetromino);
        CHECK(!compiled);
        if (!compiled) {
            CHECK(compiled.error().code
                == CanonicalOrientationCompilationErrorCode::prototile_unavailable_in_domain);
            CHECK(compiled.error().lattice_error.has_value() == false);
            CHECK(compiled.error().hex12_error.has_value() == false);
        }
    }

    // And each hex-only identity refused by the lattice.
    const PrototileId::Value hex_only[3] = { 35, 36, 37 };
    for (const PrototileId::Value id : hex_only) {
        const CanonicalPrototile *entry = catalog->find(PrototileId(id));
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        auto compiled =
            content::compile_canonical_orientations(GeometryDomain::lattice, *entry);
        CHECK(!compiled);
        if (!compiled) {
            CHECK(compiled.error().code
                == CanonicalOrientationCompilationErrorCode::prototile_unavailable_in_domain);
            CHECK(compiled.error().lattice_error.has_value() == false);
            CHECK(compiled.error().hex12_error.has_value() == false);
        }
    }
}

TEST_CASE("an invalid geometry domain is refused before membership") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const GeometryDomain invalid_domains[3] = {
        static_cast<GeometryDomain>(2),
        static_cast<GeometryDomain>(9),
        static_cast<GeometryDomain>(200),
    };
    // Both a lattice identity and a hex-only one: neither is consulted, so both
    // report the domain rather than membership.
    const PrototileId::Value ids[2] = { 1, 37 };
    for (const GeometryDomain domain : invalid_domains) {
        for (const PrototileId::Value id : ids) {
            const CanonicalPrototile *entry = catalog->find(PrototileId(id));
            CHECK(entry != nullptr);
            if (entry == nullptr) {
                continue;
            }
            auto compiled = content::compile_canonical_orientations(domain, *entry);
            CHECK(!compiled);
            if (!compiled) {
                CHECK(compiled.error().code
                    == CanonicalOrientationCompilationErrorCode::unsupported_domain);
                CHECK(compiled.error().lattice_error.has_value() == false);
                CHECK(compiled.error().hex12_error.has_value() == false);
            }
        }
    }
}

TEST_CASE("every successful compilation carries no error payload at all") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    // The complement of the payload rule: a success is a sequence, never a
    // partially populated error.
    for (const CanonicalPrototile &entry : catalog->entries()) {
        const GeometryDomain domains[2] = { GeometryDomain::lattice, GeometryDomain::hex12 };
        for (const GeometryDomain domain : domains) {
            auto compiled = content::compile_canonical_orientations(domain, entry);
            CHECK(bool(compiled) == entry.supports(domain));
            if (compiled) {
                CHECK(compiled.value().empty() == false);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// downstream domain blindness
// ---------------------------------------------------------------------------

TEST_CASE("hex-12 palette entries reach placements, arrangements, and joins") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }

    // A hex-12 palette built entirely through the act-1 path: hexagon first,
    // then triangle.
    std::optional<PaletteEntry> hexagon = domain_entry(
        catalog.value(), GeometryDomain::hex12, 36, Supply::finite(2).value());
    std::optional<PaletteEntry> triangle =
        domain_entry(catalog.value(), GeometryDomain::hex12, 35, Supply::unlimited());
    CHECK(hexagon.has_value());
    CHECK(triangle.has_value());
    if (!hexagon || !triangle) {
        return;
    }

    std::vector<PaletteEntry> entries;
    entries.push_back(hexagon.value());
    entries.push_back(triangle.value());
    auto palette = engine::Palette::make(std::move(entries));
    CHECK(bool(palette));
    std::optional<Region> region = square_region(6);
    CHECK(region.has_value());
    if (!palette || !region) {
        return;
    }

    // An ordinary engine state: it carries no domain, and nothing below asks it
    // to.
    engine::State state(
        engine::Level(std::move(palette).value(), std::move(region).value()));
    const PaletteEntryIndex hexagon_entry(0);
    const PaletteEntryIndex triangle_entry(1);

    const engine::PlaceCommand place {
        hexagon_entry, PaletteOrientationIndex(0), unit(0, 0)
    };

    auto preview = state.preview(place);
    CHECK(bool(preview));
    auto placed = state.apply(place);
    CHECK(bool(placed));
    if (!placed) {
        return;
    }
    CHECK(state.arrangement().entries().size() == 1);

    // Every hexagon edge accepts exactly one triangle variant and edge, through
    // the ordinary mating command.
    const std::size_t hexagon_edges =
        state.arrangement().entries().front().placement.footprint().vertices().size();
    CHECK(hexagon_edges == 6);

    std::size_t joined = 0;
    std::size_t ambiguous = 0;
    for (std::size_t edge = 0; edge < hexagon_edges; ++edge) {
        std::size_t accepted_here = 0;
        std::optional<engine::MateFullEdgesCommand> chosen;
        for (std::size_t variant = 0;
            variant < state.palette().entries()[1].orientations().size(); ++variant) {
            const std::size_t candidate_edges = state.palette()
                                                    .entries()[1]
                                                    .orientations()[variant]
                                                    .canonical_polygon()
                                                    .vertices()
                                                    .size();
            for (std::size_t candidate = 0; candidate < candidate_edges; ++candidate) {
                const engine::MateFullEdgesCommand mate {
                    placed.value(),
                    EdgeIndex(edge),
                    triangle_entry,
                    PaletteOrientationIndex(variant),
                    EdgeIndex(candidate),
                };
                if (state.preview(mate)) {
                    ++accepted_here;
                    if (!chosen.has_value()) {
                        chosen = mate;
                    }
                }
            }
        }
        if (accepted_here != 1) {
            ++ambiguous;
        }
        if (chosen.has_value()) {
            auto mated = state.apply(chosen.value());
            CHECK(bool(mated));
            if (mated) {
                ++joined;
            }
        }
    }
    CHECK(joined == 6);
    CHECK(ambiguous == 0);
    CHECK(state.arrangement().entries().size() == 7);

    // Supply is derived from the arrangement exactly as it always was, with no
    // domain knowledge anywhere.
    std::optional<engine::SupplyStatus> hexagon_supply = state.supply_status(hexagon_entry);
    std::optional<engine::SupplyStatus> triangle_supply = state.supply_status(triangle_entry);
    CHECK(hexagon_supply.has_value());
    CHECK(triangle_supply.has_value());
    if (hexagon_supply.has_value()) {
        CHECK(hexagon_supply->used == 1);
        CHECK(hexagon_supply->remaining == std::optional<Supply::Amount>(1));
    }
    if (triangle_supply.has_value()) {
        CHECK(triangle_supply->used == 6);
        CHECK(triangle_supply->remaining.has_value() == false);
    }

    // The arrangement it built is an ordinary one: interiors are pairwise
    // disjoint by construction, and the region contains every footprint.
    for (const Entry &entry : state.arrangement().entries()) {
        CHECK(state.region().contains(entry.placement.footprint()));
    }
    CHECK(state.solved() == false);
}

TEST_CASE("a hex-12 candidate also mates through the vertex operation") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    std::optional<PaletteEntry> dodecagon =
        domain_entry(catalog.value(), GeometryDomain::hex12, 37, Supply::unlimited());
    std::optional<PaletteEntry> square = domain_entry(
        catalog.value(), GeometryDomain::hex12, 27, Supply::finite(3).value());
    CHECK(dodecagon.has_value());
    CHECK(square.has_value());
    if (!dodecagon || !square) {
        return;
    }

    std::vector<PaletteEntry> entries;
    entries.push_back(dodecagon.value());
    entries.push_back(square.value());
    auto palette = engine::Palette::make(std::move(entries));
    std::optional<Region> region = square_region(8);
    CHECK(bool(palette));
    CHECK(region.has_value());
    if (!palette || !region) {
        return;
    }

    engine::State state(
        engine::Level(std::move(palette).value(), std::move(region).value()));

    const engine::PlaceCommand place {
        PaletteEntryIndex(0), PaletteOrientationIndex(0), unit(0, 0)
    };
    auto anchor = state.apply(place);
    CHECK(bool(anchor));
    if (!anchor) {
        return;
    }

    // A unit square shares a complete edge with a dodecagon in the 3.4.6.4 and
    // 4.6.12 stars, so an exact vertex mating exists; the search is over the
    // ordinary command surface only.
    std::size_t accepted = 0;
    for (std::size_t anchor_vertex = 0; anchor_vertex < 12; ++anchor_vertex) {
        for (std::size_t variant = 0;
            variant < state.palette().entries()[1].orientations().size(); ++variant) {
            for (std::size_t candidate_vertex = 0; candidate_vertex < 4;
                ++candidate_vertex) {
                const engine::MateVerticesCommand mate {
                    anchor.value(),
                    VertexIndex(anchor_vertex),
                    PaletteEntryIndex(1),
                    PaletteOrientationIndex(variant),
                    VertexIndex(candidate_vertex),
                };
                if (state.preview(mate)) {
                    ++accepted;
                }
            }
        }
    }
    CHECK(accepted > 0);

    // One of them really inserts, and the supply it draws on is the ordinary
    // derived one.
    bool inserted = false;
    for (std::size_t anchor_vertex = 0; anchor_vertex < 12 && !inserted; ++anchor_vertex) {
        for (std::size_t variant = 0;
            variant < state.palette().entries()[1].orientations().size() && !inserted;
            ++variant) {
            for (std::size_t candidate_vertex = 0; candidate_vertex < 4 && !inserted;
                ++candidate_vertex) {
                const engine::MateVerticesCommand mate {
                    anchor.value(),
                    VertexIndex(anchor_vertex),
                    PaletteEntryIndex(1),
                    PaletteOrientationIndex(variant),
                    VertexIndex(candidate_vertex),
                };
                if (state.apply(mate)) {
                    inserted = true;
                }
            }
        }
    }
    CHECK(inserted);
    CHECK(state.arrangement().entries().size() == 2);

    std::optional<engine::SupplyStatus> square_supply =
        state.supply_status(PaletteEntryIndex(1));
    CHECK(square_supply.has_value());
    if (square_supply.has_value()) {
        CHECK(square_supply->used == 1);
        CHECK(square_supply->remaining == std::optional<Supply::Amount>(2));
    }
}
