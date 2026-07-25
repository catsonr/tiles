#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/Hex12.h"
#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/geometry/Alignment.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "engine/Blueprint.h"
#include "engine/Palette.h"
#include "engine/Supply.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

using namespace tiles;
using tiles::engine::BlueprintCompilationErrorCode;
using tiles::engine::BlueprintPlacement;
using tiles::engine::compile_blueprint;
using tiles::engine::Palette;
using tiles::engine::PaletteEntry;
using tiles::engine::Supply;
using tiles_test::raw_pt;

namespace {

// --- exact fixtures ---
//
// Every prototile, oriented value, placement, and arrangement below is produced
// by an ordinary checked factory or compiler. Nothing here fabricates geometry.

constexpr std::uint64_t SQUARE_ID = 27;
constexpr std::uint64_t DOMINO_ID = 26;
constexpr std::uint64_t TRIANGLE_ID = 35;
constexpr std::uint64_t HEXAGON_ID = 36;

const Orientation R = Orientation::reference();
const Orientation Q = Orientation::quarter();
const Orientation H = Orientation::half();
const Orientation T = Orientation::three_quarter();

Point units(std::int64_t p_x, std::int64_t p_y) {
    return raw_pt(p_x * Coordinate::SCALE, p_y * Coordinate::SCALE);
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

std::vector<Orientation> quarter_turns() {
    return { R, Q, H, T };
}

Prototile rectangle(std::uint64_t p_id, std::int64_t p_width, std::int64_t p_height) {
    auto polygon = Polygon::make({
        units(0, 0),
        units(p_width, 0),
        units(p_width, p_height),
        units(0, p_height),
    });
    return std::move(Prototile::make(PrototileId(p_id), std::move(polygon).value()))
        .value();
}

PaletteEntry lattice_entry(const Prototile &p_prototile, Supply p_supply) {
    auto entry = PaletteEntry::make(p_prototile, p_supply, quarter_turns());
    return std::move(entry).value();
}

PaletteEntry hex12_entry(
    std::uint64_t p_id, Hex12RegularPolygon p_polygon, Supply p_supply) {
    auto compiled =
        compile_hex12_orientations(PrototileId(p_id), p_polygon, all_twelfth_turns());
    auto entry = PaletteEntry::make_compiled(p_supply, std::move(compiled).value());
    return std::move(entry).value();
}

Palette palette_of(std::vector<PaletteEntry> p_entries) {
    auto palette = Palette::make(std::move(p_entries));
    return std::move(palette).value();
}

// One unit square, unlimited.
Palette square_palette() {
    std::vector<PaletteEntry> entries;
    entries.push_back(lattice_entry(rectangle(SQUARE_ID, 1, 1), Supply::unlimited()));
    return palette_of(std::move(entries));
}

// A unit square and a 2 x 1 domino, both unlimited.
Palette lattice_palette() {
    std::vector<PaletteEntry> entries;
    entries.push_back(lattice_entry(rectangle(DOMINO_ID, 2, 1), Supply::unlimited()));
    entries.push_back(lattice_entry(rectangle(SQUARE_ID, 1, 1), Supply::unlimited()));
    return palette_of(std::move(entries));
}

// The unit hex-12 hexagon and triangle, both unlimited.
Palette hex12_palette() {
    std::vector<PaletteEntry> entries;
    entries.push_back(
        hex12_entry(HEXAGON_ID, Hex12RegularPolygon::hexagon, Supply::unlimited()));
    entries.push_back(
        hex12_entry(TRIANGLE_ID, Hex12RegularPolygon::triangle, Supply::unlimited()));
    return palette_of(std::move(entries));
}

const PaletteEntry *entry_for(const Palette &p_palette, std::uint64_t p_id) {
    for (const PaletteEntry &entry : p_palette.entries()) {
        if (entry.prototile().id() == PrototileId(p_id)) {
            return &entry;
        }
    }
    return nullptr;
}

// The palette-owned oriented value whose representative is exactly p_orientation.
const OrientedPrototile *variant(
    const Palette &p_palette, std::uint64_t p_id, Orientation p_orientation) {
    const PaletteEntry *entry = entry_for(p_palette, p_id);
    if (entry == nullptr) {
        return nullptr;
    }
    for (const OrientedPrototile &oriented : entry->orientations()) {
        if (oriented.orientation() == p_orientation) {
            return &oriented;
        }
    }
    return nullptr;
}

BlueprintPlacement record(
    std::uint64_t p_id, Orientation p_orientation, Point p_translation) {
    return BlueprintPlacement { PrototileId(p_id), p_orientation, p_translation };
}

// Read one arrangement back as the records which would reproduce it. Used to
// prove that a patch grown through exact joins recompiles unchanged, without
// authoring one hex-12 translation by hand anywhere in this file.
std::vector<BlueprintPlacement> records_from(const Arrangement &p_arrangement) {
    std::vector<BlueprintPlacement> records;
    for (const Entry &entry : p_arrangement.entries()) {
        records.push_back(BlueprintPlacement {
            entry.placement.prototile().id(),
            entry.placement.orientation(),
            entry.placement.translation(),
        });
    }
    return records;
}

bool same_coverage(const Arrangement &p_lhs, const Arrangement &p_rhs) {
    if (p_lhs.entries().size() != p_rhs.entries().size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_lhs.entries().size(); ++i) {
        const Placement &lhs = p_lhs.entries()[i].placement;
        const Placement &rhs = p_rhs.entries()[i].placement;
        if (lhs.prototile().id() != rhs.prototile().id()
            || lhs.orientation() != rhs.orientation()
            || lhs.translation() != rhs.translation()) {
            return false;
        }
        if (!same_boundary(lhs.footprint(), rhs.footprint())) {
            return false;
        }
    }
    return true;
}

// Grow one hexagon surrounded by triangles using only ordinary exact joins. The
// hexagon is placed at exact origin; each of its six edges then receives the
// first triangle variant and candidate edge the arrangement admits.
Arrangement hex12_patch(const Palette &p_palette) {
    Arrangement arrangement;

    const OrientedPrototile *hexagon = variant(p_palette, HEXAGON_ID, twelfth(0));
    if (hexagon == nullptr) {
        return arrangement;
    }
    auto anchor = Placement::make(*hexagon, units(0, 0));
    if (!anchor) {
        return arrangement;
    }
    auto inserted = arrangement.try_insert(std::move(anchor).value());
    if (!inserted) {
        return arrangement;
    }
    const PlacementId hexagon_id = inserted.value();

    const PaletteEntry *triangles = entry_for(p_palette, TRIANGLE_ID);
    if (triangles == nullptr) {
        return arrangement;
    }

    const std::size_t hexagon_edges =
        arrangement.entries().front().placement.footprint().vertices().size();
    for (std::size_t anchor_edge = 0; anchor_edge < hexagon_edges; ++anchor_edge) {
        bool joined = false;
        for (const OrientedPrototile &candidate : triangles->orientations()) {
            const std::size_t candidate_edges =
                candidate.canonical_polygon().vertices().size();
            for (std::size_t edge = 0; edge < candidate_edges && !joined; ++edge) {
                auto join = arrangement.try_join_full_edges(
                    hexagon_id, EdgeIndex(anchor_edge), candidate, EdgeIndex(edge));
                joined = bool(join);
            }
            if (joined) {
                break;
            }
        }
    }

    return arrangement;
}

} // namespace

// --- the empty blueprint ---

TEST_CASE("an empty blueprint compiles to an empty arrangement") {
    const Palette palette = square_palette();
    auto compiled = compile_blueprint(palette, {});
    CHECK(bool(compiled));
    if (compiled) {
        CHECK(compiled.value().entries().empty());
        CHECK(compiled.value().next_id() == std::optional<PlacementId>(PlacementId(0)));
    }
}

// --- representative blueprints ---

TEST_CASE("a representative lattice blueprint compiles in stored order") {
    const Palette palette = lattice_palette();
    const std::vector<BlueprintPlacement> records {
        record(DOMINO_ID, R, units(0, 0)),
        record(SQUARE_ID, R, units(0, 1)),
        record(SQUARE_ID, R, units(1, 1)),
    };

    auto compiled = compile_blueprint(palette, records);
    CHECK(bool(compiled));
    if (!compiled) {
        return;
    }

    const Arrangement &arrangement = compiled.value();
    CHECK(arrangement.entries().size() == 3);
    if (arrangement.entries().size() != 3) {
        return;
    }

    // Stored order is record order, and ids are the ordinary monotonic
    // allocation from zero.
    for (std::size_t i = 0; i < records.size(); ++i) {
        CHECK(arrangement.entries()[i].id == PlacementId(i));
        CHECK(arrangement.entries()[i].placement.prototile().id()
            == records[i].prototile_id);
        CHECK(arrangement.entries()[i].placement.orientation() == records[i].orientation);
        CHECK(arrangement.entries()[i].placement.translation() == records[i].translation);
    }
}

TEST_CASE("compiled footprints are exactly the palette geometry translated") {
    const Palette palette = lattice_palette();
    const std::vector<BlueprintPlacement> records {
        record(DOMINO_ID, Q, units(3, -2)),
    };

    auto compiled = compile_blueprint(palette, records);
    CHECK(bool(compiled));
    if (!compiled || compiled.value().entries().empty()) {
        return;
    }

    const OrientedPrototile *oriented = variant(palette, DOMINO_ID, Q);
    CHECK(oriented != nullptr);
    if (oriented == nullptr) {
        return;
    }
    auto expected = Placement::make(*oriented, units(3, -2));
    CHECK(bool(expected));
    if (!expected) {
        return;
    }

    const Placement &actual = compiled.value().entries().front().placement;
    CHECK(same_boundary(actual.footprint(), expected.value().footprint()));
    CHECK(same_boundary(
        actual.oriented_polygon(), oriented->canonical_polygon()));
}

TEST_CASE("a lattice partial-edge blueprint is admitted") {
    // Two unit squares sit on one half each of the domino's two-unit top edge,
    // so neither shares a complete edge with it. The core admits partial-edge
    // contact and this compiler adds no stricter rule.
    const Palette palette = lattice_palette();
    const std::vector<BlueprintPlacement> records {
        record(DOMINO_ID, R, units(0, 0)),
        record(SQUARE_ID, R, units(0, 1)),
        record(SQUARE_ID, R, units(1, 1)),
    };

    auto compiled = compile_blueprint(palette, records);
    CHECK(bool(compiled));
    if (compiled) {
        CHECK(compiled.value().entries().size() == 3);
    }
}

TEST_CASE("a multi-placement hex-12 patch compiles through the same path") {
    const Palette palette = hex12_palette();
    const Arrangement grown = hex12_patch(palette);

    // One hexagon and one triangle on each of its six edges.
    CHECK(grown.entries().size() == 7);
    if (grown.entries().size() != 7) {
        return;
    }

    const std::vector<BlueprintPlacement> records = records_from(grown);
    auto compiled = compile_blueprint(palette, records);
    CHECK(bool(compiled));
    if (!compiled) {
        return;
    }
    CHECK(same_coverage(grown, compiled.value()));
}

// --- record resolution failures ---

TEST_CASE("an unknown prototile id fails at its own record") {
    const Palette palette = square_palette();
    const std::vector<BlueprintPlacement> records {
        record(SQUARE_ID, R, units(0, 0)),
        record(DOMINO_ID, R, units(4, 0)),
        record(SQUARE_ID, R, units(2, 0)),
    };

    auto compiled = compile_blueprint(palette, records);
    CHECK(!compiled);
    if (compiled) {
        return;
    }
    CHECK(compiled.error().code
        == BlueprintCompilationErrorCode::prototile_not_in_palette);
    CHECK(compiled.error().placement == 1);
    CHECK(compiled.error().prototile_id == PrototileId(DOMINO_ID));
    CHECK(!compiled.error().orientation.has_value());
    CHECK(!compiled.error().placement_error.has_value());
    CHECK(!compiled.error().arrangement_error.has_value());
}

TEST_CASE("a nonrepresentative equivalent label is not an orientation in the palette") {
    // The unit square's four quarter turns collapse onto one group whose
    // representative is 0/1; 1/4 is one of its recorded equivalent labels and is
    // deliberately not accepted as a variant name.
    const Palette palette = square_palette();
    const OrientedPrototile *only = variant(palette, SQUARE_ID, R);
    CHECK(only != nullptr);
    if (only == nullptr) {
        return;
    }
    CHECK(only->equivalent_orientations().size() == 4);

    auto compiled = compile_blueprint(palette, { record(SQUARE_ID, Q, units(0, 0)) });
    CHECK(!compiled);
    if (compiled) {
        return;
    }
    CHECK(compiled.error().code
        == BlueprintCompilationErrorCode::orientation_not_in_palette);
    CHECK(compiled.error().placement == 0);
    CHECK(compiled.error().prototile_id == PrototileId(SQUARE_ID));
    CHECK(compiled.error().orientation == std::optional<Orientation>(Q));
    CHECK(!compiled.error().placement_error.has_value());
    CHECK(!compiled.error().arrangement_error.has_value());
}

TEST_CASE("an unsupported representative is not an orientation in the palette") {
    const Palette palette = lattice_palette();
    auto eighth = Orientation::make(1, 8);
    CHECK(bool(eighth));
    if (!eighth) {
        return;
    }

    auto compiled = compile_blueprint(
        palette, { record(DOMINO_ID, eighth.value(), units(0, 0)) });
    CHECK(!compiled);
    if (compiled) {
        return;
    }
    CHECK(compiled.error().code
        == BlueprintCompilationErrorCode::orientation_not_in_palette);
    CHECK(compiled.error().orientation == std::optional<Orientation>(eighth.value()));
}

// --- supply ---

TEST_CASE("a finite supply fails on the first excess record") {
    std::vector<PaletteEntry> entries;
    entries.push_back(lattice_entry(rectangle(SQUARE_ID, 1, 1), Supply::finite(2).value()));
    const Palette palette = palette_of(std::move(entries));

    const std::vector<BlueprintPlacement> records {
        record(SQUARE_ID, R, units(0, 0)),
        record(SQUARE_ID, R, units(1, 0)),
        record(SQUARE_ID, R, units(2, 0)),
    };

    auto compiled = compile_blueprint(palette, records);
    CHECK(!compiled);
    if (compiled) {
        return;
    }
    CHECK(compiled.error().code == BlueprintCompilationErrorCode::supply_exhausted);
    CHECK(compiled.error().placement == 2);
    CHECK(compiled.error().prototile_id == PrototileId(SQUARE_ID));
    CHECK(compiled.error().orientation == std::optional<Orientation>(R));
}

TEST_CASE("supply is counted per identity, not across the whole blueprint") {
    std::vector<PaletteEntry> entries;
    entries.push_back(lattice_entry(rectangle(DOMINO_ID, 2, 1), Supply::finite(1).value()));
    entries.push_back(lattice_entry(rectangle(SQUARE_ID, 1, 1), Supply::finite(1).value()));
    const Palette palette = palette_of(std::move(entries));

    auto compiled = compile_blueprint(
        palette,
        {
            record(DOMINO_ID, R, units(0, 0)),
            record(SQUARE_ID, R, units(0, 1)),
        });
    CHECK(bool(compiled));
    if (compiled) {
        CHECK(compiled.value().entries().size() == 2);
    }
}

TEST_CASE("an unlimited supply admits any number of records") {
    const Palette palette = square_palette();
    std::vector<BlueprintPlacement> records;
    for (std::int64_t i = 0; i < 8; ++i) {
        records.push_back(record(SQUARE_ID, R, units(i, 0)));
    }

    auto compiled = compile_blueprint(palette, records);
    CHECK(bool(compiled));
    if (compiled) {
        CHECK(compiled.value().entries().size() == 8);
    }
}

// --- exact construction failures ---

TEST_CASE("placement overflow preserves the exact PlacementError") {
    const Palette palette = square_palette();
    const Point far { Coordinate::from_raw(INT64_MAX), Coordinate::from_raw(0) };

    auto compiled = compile_blueprint(palette, { record(SQUARE_ID, R, far) });
    CHECK(!compiled);
    if (compiled) {
        return;
    }
    CHECK(compiled.error().code
        == BlueprintCompilationErrorCode::placement_construction_failed);
    CHECK(compiled.error().placement == 0);
    CHECK(compiled.error().placement_error
        == std::optional<PlacementError>(PlacementError::footprint_overflow));
    CHECK(!compiled.error().arrangement_error.has_value());
}

TEST_CASE("interior overlap preserves the exact ArrangementError and conflicting id") {
    const Palette palette = square_palette();
    const std::vector<BlueprintPlacement> records {
        record(SQUARE_ID, R, units(0, 0)),
        record(SQUARE_ID, R, units(4, 0)),
        record(SQUARE_ID, R, units(0, 0)),
    };

    auto compiled = compile_blueprint(palette, records);
    CHECK(!compiled);
    if (compiled) {
        return;
    }
    CHECK(compiled.error().code
        == BlueprintCompilationErrorCode::arrangement_insertion_failed);
    CHECK(compiled.error().placement == 2);
    CHECK(compiled.error().arrangement_error.has_value());
    if (compiled.error().arrangement_error.has_value()) {
        const ArrangementError &error = compiled.error().arrangement_error.value();
        CHECK(error.code == ArrangementErrorCode::interior_overlap);
        CHECK(error.conflicting_placement == std::optional<PlacementId>(PlacementId(0)));
    }
    CHECK(!compiled.error().placement_error.has_value());
}

// --- precedence ---

TEST_CASE("record precedence is exact when several defects coexist") {
    std::vector<PaletteEntry> entries;
    entries.push_back(lattice_entry(rectangle(SQUARE_ID, 1, 1), Supply::finite(1).value()));
    const Palette palette = palette_of(std::move(entries));

    const Point far { Coordinate::from_raw(INT64_MAX), Coordinate::from_raw(0) };

    // Unknown identity, unusable orientation label, and an overlapping
    // translation at once: identity answers first.
    auto unknown = compile_blueprint(
        palette,
        {
            record(SQUARE_ID, R, units(0, 0)),
            record(DOMINO_ID, Q, units(0, 0)),
        });
    CHECK(!unknown);
    if (!unknown) {
        CHECK(unknown.error().code
            == BlueprintCompilationErrorCode::prototile_not_in_palette);
        CHECK(!unknown.error().orientation.has_value());
    }

    // Known identity, unusable orientation, overlapping translation: orientation
    // answers before both supply and geometry.
    auto orientation = compile_blueprint(
        palette,
        {
            record(SQUARE_ID, R, units(0, 0)),
            record(SQUARE_ID, Q, units(0, 0)),
        });
    CHECK(!orientation);
    if (!orientation) {
        CHECK(orientation.error().code
            == BlueprintCompilationErrorCode::orientation_not_in_palette);
        CHECK(orientation.error().orientation == std::optional<Orientation>(Q));
    }

    // Resolvable record, exhausted supply, and an overlapping translation:
    // supply answers before the arrangement proof.
    auto supply = compile_blueprint(
        palette,
        {
            record(SQUARE_ID, R, units(0, 0)),
            record(SQUARE_ID, R, units(0, 0)),
        });
    CHECK(!supply);
    if (!supply) {
        CHECK(supply.error().code == BlueprintCompilationErrorCode::supply_exhausted);
        CHECK(!supply.error().arrangement_error.has_value());
    }

    // Available supply, unrepresentable translation, and no possible overlap
    // there: placement construction answers before insertion.
    auto placement = compile_blueprint(palette, { record(SQUARE_ID, R, far) });
    CHECK(!placement);
    if (!placement) {
        CHECK(placement.error().code
            == BlueprintCompilationErrorCode::placement_construction_failed);
        CHECK(!placement.error().arrangement_error.has_value());
    }
}

TEST_CASE("a failed compilation returns no arrangement at all") {
    const Palette palette = square_palette();
    auto compiled = compile_blueprint(
        palette,
        {
            record(SQUARE_ID, R, units(0, 0)),
            record(SQUARE_ID, R, units(0, 0)),
        });

    // The failure alternative carries the typed error and nothing else: there is
    // no partial arrangement to observe, by construction of the result type.
    CHECK(!compiled);
    CHECK(!compiled.has_value());
}

// --- removal and recompilation ---

TEST_CASE("removing a record and recompiling produces exactly the remaining coverage") {
    const Palette palette = lattice_palette();
    const std::vector<BlueprintPlacement> records {
        record(DOMINO_ID, R, units(0, 0)),
        record(SQUARE_ID, R, units(0, 1)),
        record(SQUARE_ID, R, units(1, 1)),
    };

    auto complete = compile_blueprint(palette, records);
    CHECK(bool(complete));

    std::vector<BlueprintPlacement> remaining;
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (i != 1) {
            remaining.push_back(records[i]);
        }
    }

    auto reduced = compile_blueprint(palette, remaining);
    CHECK(bool(reduced));
    if (!reduced) {
        return;
    }
    CHECK(reduced.value().entries().size() == 2);
    if (reduced.value().entries().size() != 2) {
        return;
    }

    // Recompilation reallocates identities from zero: the surviving records keep
    // their exact geometry, not their previous PlacementIds.
    CHECK(reduced.value().entries()[0].id == PlacementId(0));
    CHECK(reduced.value().entries()[1].id == PlacementId(1));
    CHECK(reduced.value().entries()[0].placement.translation() == units(0, 0));
    CHECK(reduced.value().entries()[1].placement.translation() == units(1, 1));
    CHECK(reduced.value().entries()[1].placement.prototile().id()
        == PrototileId(SQUARE_ID));

    // The removed square's own translation is covered by nothing now.
    for (const Entry &entry : reduced.value().entries()) {
        CHECK(entry.placement.translation() != units(0, 1)
            || entry.placement.prototile().id() != PrototileId(SQUARE_ID));
    }
}

TEST_CASE("removing a bridge placement is not an error") {
    // Two squares connected only through a domino. Dropping the domino leaves a
    // disconnected but perfectly valid blueprint.
    const Palette palette = lattice_palette();
    auto compiled = compile_blueprint(
        palette,
        {
            record(SQUARE_ID, R, units(0, 0)),
            record(SQUARE_ID, R, units(3, 0)),
        });
    CHECK(bool(compiled));
    if (compiled) {
        CHECK(compiled.value().entries().size() == 2);
    }
}
