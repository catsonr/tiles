#include "game/testing/LevelEditorIntegrationRunner.h"

#include "content/PrototileCatalog.h"
#include "core/Arrangement.h"
#include "core/ArrangementRegion.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/Region.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/ExactInteger.h"
#include "core/geometry/Intersection.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "core/geometry/Predicates.h"
#include "engine/Palette.h"
#include "engine/Supply.h"
#include "game/PrototilePreview.h"
#include "game/resources/LevelPersistence.h"
#include "game/resources/LevelResources.h"
#include "game/resources/ResourceCompiler.h"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/file_dialog.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace tiles::game {

namespace {

const char *MAIN_SCENE_PATH = "res://main.tscn";

// Lattice catalog positions used by name below. The lattice view ships ids
// 1..34 in order, so the zero-based row of id n is n - 1.
constexpr std::size_t ROW_TETROMINO_O = 0; // id 1, the 2 x 2 square tetromino
constexpr std::size_t ROW_DOMINO = 25; // id 26, the 2 x 1 domino
constexpr std::size_t ROW_SQUARE_1 = 26; // id 27, the unit square

// The one temporary directory this run owns. It is globalized out of `user://`
// deliberately: an exported level is an ordinary host file, so the export path
// this runner exercises is an absolute filesystem path rather than a `res://`
// or `user://` engine path.
const char *TEMPORARY_DIRECTORY_SOURCE = "user://tiles_level_editor_export";

// Exportable names, and names an export must refuse.
const char *EXPORT_FILE = "level.tres";
const char *EXPORT_UPPERCASE_FILE = "shouted.TRES";
const char *EXPORT_EXTENSIONLESS_FILE = "extensionless";
const char *EXPORT_EXTENSIONLESS_RESULT = "extensionless.tres";
const char *EXPORT_SECOND_FILE = "second.tres";
const char *EXPORT_HEX12_FILE = "hex12.tres";
const char *EXPORT_BINARY_FILE = "refused.res";
const char *EXPORT_FOREIGN_FILE = "refused.json";
const char *EXPORT_OVERSIZED_FILE = "oversized.tres";

constexpr std::size_t EXPECTED_LATTICE_ROWS = 34;
constexpr std::size_t EXPECTED_HEX12_ROWS = 4;

// The hex-12 view, in its shipped presentation order.
constexpr std::size_t HEX_ROW_TRIANGLE = 0; // id 35
constexpr std::size_t HEX_ROW_SQUARE = 1; // id 27
constexpr std::size_t HEX_ROW_HEXAGON = 2; // id 36
constexpr std::size_t HEX_ROW_DODECAGON = 3; // id 37

constexpr std::int64_t UNLIMITED = -1;

godot::String number(std::int64_t p_value) {
    return godot::String::num_int64(p_value);
}

Point origin_point() {
    return Point { Coordinate::from_raw(0), Coordinate::from_raw(0) };
}

// One exact lattice point in whole game units. Every coordinate below is written
// this way, so nothing here is a projected or rounded value.
Point unit_point(std::int64_t p_x, std::int64_t p_y) {
    return Point {
        Coordinate::from_raw(p_x * Coordinate::SCALE),
        Coordinate::from_raw(p_y * Coordinate::SCALE),
    };
}

bool same_vertices(const Polygon &p_polygon, const std::vector<Point> &p_expected) {
    if (p_polygon.vertices().size() != p_expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_expected.size(); ++i) {
        if (p_polygon.vertices()[i] != p_expected[i]) {
            return false;
        }
    }
    return true;
}

// The exact covered area, summed from the footprints themselves rather than read
// back out of the derived region.
Int256 summed_footprint_area(const Arrangement &p_arrangement) {
    Int256 total = Int256::from_i64(0);
    for (const Entry &entry : p_arrangement.entries()) {
        total = total + signed_double_area(entry.placement.footprint().vertices());
    }
    return total;
}

// The exact doubled area of p_units whole game-unit squares of coverage.
Int256 doubled_unit_area(std::int64_t p_units) {
    return Int256::multiply(
        static_cast<__int128>(2) * static_cast<__int128>(p_units)
            * static_cast<__int128>(Coordinate::SCALE),
        static_cast<__int128>(Coordinate::SCALE));
}

godot::String temporary_directory() {
    return godot::ProjectSettings::get_singleton()->globalize_path(
        godot::String(TEMPORARY_DIRECTORY_SOURCE));
}

godot::String temporary_path(const char *p_file) {
    return temporary_directory() + "/" + p_file;
}

bool same_record(
    const engine::BlueprintPlacement &p_lhs, const engine::BlueprintPlacement &p_rhs) {
    return p_lhs.prototile_id == p_rhs.prototile_id
        && p_lhs.orientation == p_rhs.orientation
        && p_lhs.translation == p_rhs.translation;
}

// The arithmetic mean of a footprint's projected vertices: exactly the handle
// the editor ranks with, recomputed here through the editor's public projection
// rather than read out of the object under test.
godot::Vector2 projected_handle(const LevelEditor &p_editor, const Placement &p_placement) {
    const Polygon::Vertices &vertices = p_placement.footprint().vertices();
    double sum_x = 0.0;
    double sum_y = 0.0;
    for (const Point &vertex : vertices) {
        const godot::Vector2 projected = p_editor.project(
            static_cast<double>(vertex.x.raw()) / static_cast<double>(Coordinate::SCALE),
            static_cast<double>(vertex.y.raw()) / static_cast<double>(Coordinate::SCALE));
        sum_x += static_cast<double>(projected.x);
        sum_y += static_cast<double>(projected.y);
    }
    const double count = static_cast<double>(vertices.size());
    return godot::Vector2(
        static_cast<real_t>(sum_x / count), static_cast<real_t>(sum_y / count));
}

} // namespace

void LevelEditorIntegrationRunner::_bind_methods() {}

bool LevelEditorIntegrationRunner::expect(bool p_condition, const char *p_description) {
    ++checks_;
    if (!p_condition) {
        ++failures_;
        godot::UtilityFunctions::push_error(
            "[tiles] level editor integration failed: ", p_description);
    }
    return p_condition;
}

// --- independent exact predicates ---

bool LevelEditorIntegrationRunner::shares_positive_length_contact(
    const Placement &p_placement) const {
    const Arrangement *arrangement = editor_->arrangement();
    if (arrangement == nullptr) {
        return false;
    }
    const Polygon::Vertices &candidate = p_placement.footprint().vertices();
    for (const Entry &entry : arrangement->entries()) {
        const Polygon::Vertices &placed = entry.placement.footprint().vertices();
        for (std::size_t i = 0; i < candidate.size(); ++i) {
            const Point &a = candidate[i];
            const Point &b = candidate[(i + 1) % candidate.size()];
            for (std::size_t j = 0; j < placed.size(); ++j) {
                const Point &c = placed[j];
                const Point &d = placed[(j + 1) % placed.size()];
                if (classify_segments(a, b, c, d) == SegmentRelation::collinear_overlap) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool LevelEditorIntegrationRunner::overlaps_blueprint(const Placement &p_placement) const {
    const Arrangement *arrangement = editor_->arrangement();
    if (arrangement == nullptr) {
        return false;
    }
    for (const Entry &entry : arrangement->entries()) {
        if (interiors_overlap(p_placement.footprint(), entry.placement.footprint())) {
            return true;
        }
    }
    return false;
}

bool LevelEditorIntegrationRunner::same_records(
    const std::vector<engine::BlueprintPlacement> &p_lhs,
    const std::vector<engine::BlueprintPlacement> &p_rhs) {
    if (p_lhs.size() != p_rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_lhs.size(); ++i) {
        if (!same_record(p_lhs[i], p_rhs[i])) {
            return false;
        }
    }
    return true;
}

bool LevelEditorIntegrationRunner::lock_palette(
    content::GeometryDomain p_domain,
    const std::vector<std::size_t> &p_rows,
    const std::vector<std::int64_t> &p_finite_amounts) {
    if (!editor_->choose_domain(p_domain)) {
        return false;
    }
    for (std::size_t i = 0; i < p_rows.size(); ++i) {
        editor_->set_row_included(p_rows[i], true);
        const std::int64_t amount =
            i < p_finite_amounts.size() ? p_finite_amounts[i] : UNLIMITED;
        if (amount > 0) {
            editor_->set_row_unlimited(p_rows[i], false);
            editor_->set_row_finite_amount(p_rows[i], amount);
        }
    }
    return editor_->build_palette();
}

// --- startup ---

void LevelEditorIntegrationRunner::check_startup() {
    expect(
        editor_->phase() == LevelEditor::EditorPhase::choose_domain,
        "the editor boots into domain phase");
    expect(!editor_->domain().has_value(), "no geometry domain is chosen at boot");
    expect(editor_->palette() == nullptr, "no palette exists at boot");
    expect(editor_->blueprint().empty(), "no blueprint records exist at boot");
    expect(editor_->arrangement() == nullptr, "no arrangement exists at boot");
    expect(editor_->proposals().empty(), "no proposals exist at boot");
    expect(!editor_->active_proposal().has_value(), "no active proposal exists at boot");
    expect(editor_->palette_rows().empty(), "no palette rows exist at boot");
    expect(
        editor_->row_preview_control(0) == nullptr,
        "no row controls are generated before a domain is chosen");
    expect(editor_->catalog() != nullptr, "the editor owns one canonical catalog");

    // A value outside the enumeration installs nothing at all.
    const bool installed =
        editor_->choose_domain(static_cast<content::GeometryDomain>(200));
    expect(!installed, "an invalid geometry domain is rejected");
    expect(
        editor_->phase() == LevelEditor::EditorPhase::choose_domain,
        "an invalid geometry domain leaves the editor in domain phase");
    expect(
        editor_->palette_rows().empty(),
        "an invalid geometry domain generates no rows");
}

// --- the two domain views ---

void LevelEditorIntegrationRunner::check_domain_views() {
    const content::PrototileCatalog *catalog = editor_->catalog();
    if (catalog == nullptr) {
        return;
    }

    expect(
        editor_->choose_domain(content::GeometryDomain::lattice),
        "the lattice domain can be chosen");
    expect(
        editor_->phase() == LevelEditor::EditorPhase::choose_palette,
        "choosing a domain enters palette phase");
    expect(
        editor_->domain() == std::optional<content::GeometryDomain>(
            content::GeometryDomain::lattice),
        "the chosen domain is reported back");
    expect(
        editor_->palette_rows().size() == EXPECTED_LATTICE_ROWS,
        "the lattice view exposes exactly 34 rows");

    godot::Node *rows = editor_->get_node_or_null(
        godot::NodePath("PalettePanel/Margin/Body/PaletteScroll/PaletteRows"));
    expect(rows != nullptr, "the palette panel owns its generated rows");
    if (rows != nullptr) {
        expect(
            static_cast<std::size_t>(rows->get_child_count()) == EXPECTED_LATTICE_ROWS,
            "the visible lattice row count matches the document");
    }

    const std::vector<const content::CanonicalPrototile *> lattice =
        catalog->entries_for(content::GeometryDomain::lattice);
    bool ordered = lattice.size() == EXPECTED_LATTICE_ROWS;
    bool named = true;
    bool previewed = true;
    for (std::size_t index = 0; index < lattice.size(); ++index) {
        if (lattice[index]->prototile().id()
            != PrototileId(static_cast<PrototileId::Value>(index + 1))) {
            ordered = false;
        }
        const godot::Label *name = editor_->row_name_control(index);
        if (name == nullptr
            || name->get_text()
                != godot::String(lattice[index]->display_name().c_str())) {
            named = false;
        }
        const PrototilePreview *preview = editor_->row_preview_control(index);
        if (preview == nullptr || preview->polygon() == nullptr
            || !same_boundary(*preview->polygon(), lattice[index]->prototile().polygon())) {
            previewed = false;
        }
    }
    expect(ordered, "the lattice view is ids 1..34 in order");
    expect(named, "every lattice row is named by its own catalog entry");
    expect(previewed, "every lattice row previews its own exact reference polygon");
    expect(
        editor_->row_preview_control(EXPECTED_LATTICE_ROWS) == nullptr,
        "there is no row past the last lattice identity");

    expect(
        editor_->choose_domain(content::GeometryDomain::hex12),
        "the hex-12 domain can be chosen");
    expect(
        editor_->palette_rows().size() == EXPECTED_HEX12_ROWS,
        "the hex-12 view exposes exactly four rows");
    if (rows != nullptr) {
        expect(
            static_cast<std::size_t>(rows->get_child_count()) == EXPECTED_HEX12_ROWS,
            "the visible hex-12 row count matches the document");
    }

    const std::vector<const content::CanonicalPrototile *> hex12 =
        catalog->entries_for(content::GeometryDomain::hex12);
    const PrototileId::Value expected_ids[] = { 35, 27, 36, 37 };
    bool hex_ordered = hex12.size() == EXPECTED_HEX12_ROWS;
    for (std::size_t index = 0; index < hex12.size() && index < EXPECTED_HEX12_ROWS;
         ++index) {
        if (hex12[index]->prototile().id() != PrototileId(expected_ids[index])) {
            hex_ordered = false;
        }
    }
    expect(hex_ordered, "the hex-12 view is triangle, square, hexagon, dodecagon");
    expect(
        editor_->row_preview_control(EXPECTED_HEX12_ROWS) == nullptr,
        "there is no row past the last hex-12 identity");
}

// --- discarding ---

void LevelEditorIntegrationRunner::check_domain_discards() {
    editor_->choose_domain(content::GeometryDomain::hex12);
    editor_->set_row_included(HEX_ROW_TRIANGLE, true);
    expect(
        editor_->palette_rows()[HEX_ROW_TRIANGLE].included,
        "a row can be included in palette phase");

    editor_->choose_domain(content::GeometryDomain::lattice);
    bool nothing_included = true;
    for (const LevelEditor::PaletteRow &row : editor_->palette_rows()) {
        if (row.included) {
            nothing_included = false;
        }
    }
    expect(nothing_included, "changing domains installs a fresh unincluded palette");

    // A locked palette and a complete blueprint are discarded just as readily.
    if (expect(
            lock_palette(
                content::GeometryDomain::lattice,
                { ROW_DOMINO, ROW_SQUARE_1 },
                { UNLIMITED, UNLIMITED }),
            "a lattice palette locks before the discard check")) {
        editor_->accept_active_proposal();
        expect(editor_->blueprint().size() == 1, "one placement exists before discarding");

        editor_->choose_domain(content::GeometryDomain::hex12);
        expect(
            editor_->phase() == LevelEditor::EditorPhase::choose_palette,
            "changing domains returns to palette phase");
        expect(editor_->palette() == nullptr, "changing domains discards the palette");
        expect(editor_->blueprint().empty(), "changing domains discards the blueprint");
        expect(
            editor_->arrangement() == nullptr || editor_->arrangement()->entries().empty(),
            "changing domains discards the arrangement");
        expect(editor_->proposals().empty(), "changing domains discards the proposals");
        expect(
            editor_->entry_select_control(0) == nullptr,
            "changing domains removes the blueprint entry rows");
    }

    editor_->return_to_domain_choice();
    expect(
        editor_->phase() == LevelEditor::EditorPhase::choose_domain,
        "returning to domain choice leaves no document");
    expect(editor_->palette_rows().empty(), "returning to domain choice discards the rows");
    expect(
        editor_->row_preview_control(0) == nullptr,
        "returning to domain choice removes the row controls");
    expect(
        editor_->arrangement() == nullptr,
        "returning to domain choice leaves no arrangement");
}

// --- locking one palette ---

void LevelEditorIntegrationRunner::check_palette_locking() {
    expect(
        editor_->choose_domain(content::GeometryDomain::lattice),
        "a lattice document can be started");
    expect(!editor_->build_palette(), "an empty palette cannot lock");
    expect(
        editor_->phase() == LevelEditor::EditorPhase::choose_palette,
        "a rejected palette stays in palette phase");
    expect(editor_->palette() == nullptr, "a rejected palette publishes nothing");

    editor_->set_row_included(ROW_DOMINO, true);
    editor_->set_row_included(ROW_SQUARE_1, true);
    editor_->set_row_unlimited(ROW_SQUARE_1, false);
    editor_->set_row_finite_amount(ROW_SQUARE_1, 3);
    expect(editor_->build_palette(), "a nonempty palette locks");

    const engine::Palette *palette = editor_->palette();
    if (!expect(palette != nullptr, "the locked palette is published")) {
        return;
    }
    expect(
        editor_->phase() == LevelEditor::EditorPhase::build_blueprint,
        "a locked palette enters blueprint phase");
    expect(palette->order() == 2, "the locked palette has both authored entries");
    if (palette->order() == 2) {
        expect(
            palette->entries()[0].prototile().id() == PrototileId(26),
            "authored row order is palette order");
        expect(
            palette->entries()[1].prototile().id() == PrototileId(27),
            "the second authored row is the second palette entry");
        expect(
            palette->entries()[0].supply().is_unlimited(),
            "an unlimited authored supply compiles as unlimited");
        expect(
            palette->entries()[1].supply()
                == engine::Supply::finite(3).value(),
            "a finite authored supply compiles as that exact amount");
        expect(
            editor_->remaining_supply(0) == std::optional<engine::Supply::Amount>(),
            "an unlimited entry reports no remaining count");
        expect(
            editor_->remaining_supply(1) == std::optional<engine::Supply::Amount>(3),
            "a finite entry reports its full remaining count");
    }

    expect(
        editor_->entry_select_control(0) != nullptr
            && editor_->entry_select_control(1) != nullptr,
        "blueprint phase exposes one row per palette entry");
    expect(
        editor_->entry_select_control(2) == nullptr,
        "blueprint phase exposes no row past the palette");

    // The palette is locked: there are no live edits.
    editor_->set_row_included(0, true);
    expect(
        !editor_->palette_rows()[0].included,
        "a locked palette ignores a row inclusion change");
    editor_->set_row_finite_amount(ROW_SQUARE_1, 9);
    expect(
        editor_->palette_rows()[ROW_SQUARE_1].finite_amount == 3,
        "a locked palette ignores a row supply change");
    expect(!editor_->build_palette(), "a locked palette cannot be rebuilt");
    expect(
        editor_->palette() == palette && editor_->palette()->order() == 2,
        "a rejected rebuild leaves the locked palette exactly as it was");

    const std::optional<LevelEditor::Selection> selection = editor_->selection();
    expect(selection.has_value(), "blueprint phase always carries one selection");
    if (selection.has_value()) {
        expect(
            selection->entry == 0 && selection->orientation == 0,
            "the initial selection is the first entry's first orientation");
    }
}

// --- the first placement ---

void LevelEditorIntegrationRunner::check_first_placement() {
    const engine::Palette *palette = editor_->palette();
    if (palette == nullptr || palette->entries().empty()) {
        return;
    }

    expect(
        editor_->proposals().size() == 1,
        "an empty blueprint offers exactly one placement");
    if (editor_->proposals().size() != 1) {
        return;
    }

    const LevelEditor::Proposal &proposal = editor_->proposals().front();
    const OrientedPrototile &variant = palette->entries()[0].orientations()[0];
    expect(
        proposal.record.translation == origin_point(),
        "the first placement is at exact origin");
    expect(
        proposal.record.prototile_id == palette->entries()[0].prototile().id(),
        "the first proposal names the selected identity");
    expect(
        proposal.record.orientation == variant.orientation(),
        "the first proposal names the selected representative orientation");

    auto expected = Placement::make(variant, origin_point());
    expect(bool(expected), "the expected origin placement constructs");
    if (expected) {
        expect(
            same_boundary(proposal.placement.footprint(), expected.value().footprint()),
            "the first proposal's footprint is exactly the palette geometry at origin");
    }

    expect(editor_->accept_active_proposal(), "the first proposal is accepted");
    expect(editor_->blueprint().size() == 1, "accepting appends exactly one record");
    const Arrangement *arrangement = editor_->arrangement();
    if (expect(arrangement != nullptr, "the blueprint publishes one arrangement")) {
        expect(
            arrangement->entries().size() == 1,
            "the published arrangement holds exactly one placement");
        if (!arrangement->entries().empty()) {
            expect(
                arrangement->entries()[0].placement.translation() == origin_point(),
                "the published placement is at exact origin");
            expect(
                arrangement->entries()[0].id == PlacementId(0),
                "the published placement carries the first identity");
        }
    }
}

// --- selection ---

void LevelEditorIntegrationRunner::check_selection_cycling() {
    const engine::Palette *palette = editor_->palette();
    if (palette == nullptr || palette->order() != 2) {
        return;
    }

    const std::size_t domino_variants = palette->entries()[0].orientations().size();
    const std::size_t square_variants = palette->entries()[1].orientations().size();
    expect(domino_variants == 2, "the domino compiles two distinct lattice orientations");
    expect(square_variants == 1, "the unit square compiles one distinct lattice orientation");

    editor_->select_entry(0);
    editor_->cycle_orientation(true);
    expect(
        editor_->selection()->orientation == 1,
        "cycling forward advances to the next distinct orientation");
    editor_->cycle_orientation(true);
    expect(
        editor_->selection()->orientation == 0,
        "cycling forward wraps at the last distinct orientation");
    editor_->cycle_orientation(false);
    expect(
        editor_->selection()->orientation == 1,
        "cycling backward wraps at the first distinct orientation");

    editor_->cycle_entry(true);
    expect(
        editor_->selection()->entry == 1 && editor_->selection()->orientation == 0,
        "changing entries resets the orientation to zero");
    editor_->cycle_orientation(true);
    expect(
        editor_->selection()->orientation == 0,
        "a one-orientation entry cannot cycle away from zero");
    editor_->cycle_entry(true);
    expect(editor_->selection()->entry == 0, "entry cycling wraps forward");
    editor_->cycle_entry(false);
    expect(editor_->selection()->entry == 1, "entry cycling wraps backward");

    editor_->select_entry(9);
    expect(
        editor_->selection()->entry == 1,
        "selecting a nonexistent entry changes nothing");

    const OrientedPrototile *variant = editor_->selected_variant();
    if (expect(variant != nullptr, "the selection resolves one exact oriented value")) {
        expect(
            variant->orientation() == palette->entries()[1].orientations()[0].orientation(),
            "the resolved variant is the selected entry's selected orientation");
    }
}

// --- proposal derivation ---

void LevelEditorIntegrationRunner::check_proposal_derivation() {
    editor_->select_entry(1);
    const OrientedPrototile *variant = editor_->selected_variant();
    const Arrangement *arrangement = editor_->arrangement();
    if (!expect(
            variant != nullptr && arrangement != nullptr
                && !arrangement->entries().empty(),
            "one placement and one selected variant exist for the proposal checks")) {
        return;
    }

    const std::vector<LevelEditor::Proposal> offered = editor_->proposals();
    expect(!offered.empty(), "a nonempty blueprint offers further placements");

    bool distinct = true;
    bool selected = true;
    bool contacting = true;
    bool disjoint = true;
    for (std::size_t i = 0; i < offered.size(); ++i) {
        if (offered[i].record.prototile_id != variant->prototile().id()
            || offered[i].record.orientation != variant->orientation()) {
            selected = false;
        }
        if (offered[i].placement.translation() != offered[i].record.translation) {
            selected = false;
        }
        if (!shares_positive_length_contact(offered[i].placement)) {
            contacting = false;
        }
        if (overlaps_blueprint(offered[i].placement)) {
            disjoint = false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (same_record(offered[i].record, offered[j].record)) {
                distinct = false;
            }
        }
    }
    expect(distinct, "no two offered proposals are the same physical placement");
    expect(selected, "every offered proposal carries the selected identity and orientation");
    expect(contacting, "every offered proposal shares positive-length boundary contact");
    expect(disjoint, "no offered proposal overlaps the blueprint");

    // The same selection rebuilt twice produces the same sequence.
    editor_->cycle_orientation(true);
    editor_->select_entry(1);
    expect(
        editor_->proposals().size() == offered.size(),
        "rebuilding one selection produces the same number of proposals");
    bool deterministic = editor_->proposals().size() == offered.size();
    for (std::size_t i = 0; i < offered.size() && i < editor_->proposals().size(); ++i) {
        if (!same_record(editor_->proposals()[i].record, offered[i].record)) {
            deterministic = false;
        }
    }
    expect(deterministic, "proposal derivation is deterministic and ordered");

    // Independently enumerate every exact preview the editor considers, then
    // prove that exactly the point-only contacts were dropped.
    std::vector<Placement> previewed;
    const std::size_t candidate_features = variant->canonical_polygon().vertices().size();
    for (const Entry &entry : arrangement->entries()) {
        const std::size_t anchor_features =
            entry.placement.footprint().vertices().size();
        for (std::size_t anchor_edge = 0; anchor_edge < anchor_features; ++anchor_edge) {
            for (std::size_t edge = 0; edge < candidate_features; ++edge) {
                auto join = arrangement->preview_join_full_edges(
                    entry.id, EdgeIndex(anchor_edge), *variant, EdgeIndex(edge));
                if (join) {
                    previewed.push_back(std::move(join).value());
                }
            }
        }
    }
    for (const Entry &entry : arrangement->entries()) {
        const std::size_t anchor_features =
            entry.placement.footprint().vertices().size();
        for (std::size_t anchor_vertex = 0; anchor_vertex < anchor_features;
             ++anchor_vertex) {
            for (std::size_t vertex = 0; vertex < candidate_features; ++vertex) {
                auto join = arrangement->preview_join_vertices(
                    entry.id, VertexIndex(anchor_vertex), *variant, VertexIndex(vertex));
                if (join) {
                    previewed.push_back(std::move(join).value());
                }
            }
        }
    }

    std::size_t dropped = 0;
    bool only_point_contact_dropped = true;
    for (const Placement &placement : previewed) {
        bool is_offered = false;
        for (const LevelEditor::Proposal &proposal : offered) {
            if (proposal.record.translation == placement.translation()) {
                is_offered = true;
                break;
            }
        }
        if (is_offered) {
            continue;
        }
        ++dropped;
        if (shares_positive_length_contact(placement)) {
            only_point_contact_dropped = false;
        }
    }
    expect(dropped > 0, "some exact preview really is point-only contact");
    expect(
        only_point_contact_dropped,
        "every dropped preview shares only isolated point contact");
}

// --- repeated additions ---

void LevelEditorIntegrationRunner::check_repeated_lattice_additions() {
    editor_->select_entry(1);
    const std::size_t before = editor_->blueprint().size();
    std::size_t accepted = 0;
    for (int i = 0; i < 3; ++i) {
        if (editor_->accept_active_proposal()) {
            ++accepted;
        }
    }
    expect(accepted == 3, "three further lattice placements are accepted");
    expect(
        editor_->blueprint().size() == before + 3,
        "every accepted placement appends exactly one record");
    const Arrangement *arrangement = editor_->arrangement();
    if (arrangement != nullptr) {
        expect(
            arrangement->entries().size() == editor_->blueprint().size(),
            "the arrangement holds exactly one placement per record");
        bool ordered = true;
        for (std::size_t i = 0; i < arrangement->entries().size(); ++i) {
            const Placement &placement = arrangement->entries()[i].placement;
            const engine::BlueprintPlacement &record = editor_->blueprint()[i];
            if (placement.prototile().id() != record.prototile_id
                || placement.orientation() != record.orientation
                || placement.translation() != record.translation) {
                ordered = false;
            }
        }
        expect(ordered, "the arrangement is the records in stored order");
    }
    expect(
        editor_->remaining_supply(1) == std::optional<engine::Supply::Amount>(0),
        "a finite supply is exhausted by its last placement");
}

// --- exhausted supply ---

void LevelEditorIntegrationRunner::check_supply_exhaustion() {
    const std::vector<engine::BlueprintPlacement> before = editor_->blueprint();
    const std::size_t entries_before =
        editor_->arrangement() != nullptr ? editor_->arrangement()->entries().size() : 0;

    editor_->select_entry(1);
    expect(
        !editor_->accept_active_proposal(),
        "an exhausted entry cannot add another record");
    expect(
        same_records(before, editor_->blueprint()),
        "a rejected addition preserves the blueprint records exactly");
    expect(
        editor_->arrangement() != nullptr
            && editor_->arrangement()->entries().size() == entries_before,
        "a rejected addition preserves the arrangement exactly");
    expect(
        editor_->entry_select_control(1) != nullptr,
        "an exhausted entry stays visible");
}

// --- removal and clearing ---

void LevelEditorIntegrationRunner::check_removal_and_clear() {
    const std::vector<engine::BlueprintPlacement> before = editor_->blueprint();
    if (!expect(before.size() == 4, "four placements exist for the removal checks")) {
        return;
    }

    expect(!editor_->remove_record(before.size()), "removing a nonexistent record fails");
    expect(
        same_records(before, editor_->blueprint()),
        "a failed removal preserves the blueprint");

    expect(editor_->remove_record(1), "one stored record can be removed");
    std::vector<engine::BlueprintPlacement> expected;
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (i != 1) {
            expected.push_back(before[i]);
        }
    }
    expect(
        same_records(expected, editor_->blueprint()),
        "removal drops exactly the named record and keeps stored order");

    const Arrangement *arrangement = editor_->arrangement();
    if (expect(arrangement != nullptr, "removal republishes an arrangement")) {
        expect(
            arrangement->entries().size() == expected.size(),
            "removal recompiles exactly the remaining coverage");
        bool renumbered = true;
        for (std::size_t i = 0; i < arrangement->entries().size(); ++i) {
            if (arrangement->entries()[i].id != PlacementId(i)) {
                renumbered = false;
            }
        }
        expect(renumbered, "recompilation reallocates placement identities from zero");
    }
    expect(
        editor_->remaining_supply(1) == std::optional<engine::Supply::Amount>(1),
        "removal returns the removed piece to its finite supply");

    editor_->clear_blueprint();
    expect(editor_->blueprint().empty(), "clearing empties the blueprint records");
    expect(
        editor_->arrangement() != nullptr && editor_->arrangement()->entries().empty(),
        "clearing publishes an empty arrangement");
    expect(
        editor_->proposals().size() == 1,
        "an emptied blueprint offers the exact origin placement again");
    expect(
        editor_->remaining_supply(1) == std::optional<engine::Supply::Amount>(3),
        "clearing returns the whole finite supply");
    expect(
        editor_->phase() == LevelEditor::EditorPhase::build_blueprint,
        "clearing keeps the locked palette and stays in blueprint phase");
}

// --- pointer input ---

void LevelEditorIntegrationRunner::check_pointer_selection() {
    editor_->select_entry(1);
    for (int i = 0; i < 3; ++i) {
        editor_->accept_active_proposal();
    }
    const std::vector<engine::BlueprintPlacement> placed = editor_->blueprint();
    if (!expect(placed.size() == 3, "three placements exist for the pointer checks")) {
        return;
    }

    const std::vector<LevelEditor::Proposal> offered = editor_->proposals();
    bool ranked = !offered.empty();
    for (std::size_t i = 0; i < offered.size(); ++i) {
        editor_->set_pointer(projected_handle(*editor_, offered[i].placement));
        if (editor_->active_proposal() != std::optional<std::size_t>(i)) {
            ranked = false;
        }
    }
    expect(ranked, "the pointer selects the proposal whose projected handle it is nearest");
    expect(
        same_records(placed, editor_->blueprint()),
        "pointer movement never changes the blueprint");

    const Arrangement *arrangement = editor_->arrangement();
    if (arrangement == nullptr || arrangement->entries().empty()) {
        return;
    }
    bool hit = true;
    for (std::size_t index = 0; index < arrangement->entries().size(); ++index) {
        const godot::Vector2 inside =
            projected_handle(*editor_, arrangement->entries()[index].placement);
        const std::optional<std::size_t> found = editor_->record_at_local(inside);
        if (found != std::optional<std::size_t>(index)) {
            hit = false;
        }
    }
    expect(hit, "screen input chooses exactly the record it lands on");
    expect(
        !editor_->record_at_local(godot::Vector2(100000.0f, 100000.0f)).has_value(),
        "screen input outside every footprint chooses no record");
    expect(
        same_records(placed, editor_->blueprint()),
        "hit testing never changes the blueprint");
}

// --- drawing inputs ---

void LevelEditorIntegrationRunner::check_drawing_inputs() {
    const Arrangement *arrangement = editor_->arrangement();
    if (!expect(
            arrangement != nullptr && !arrangement->entries().empty(),
            "an arrangement exists for the drawing checks")) {
        return;
    }

    // Everything the draw path consumes comes from the authoritative arrangement
    // and from each proposal's exact placement: one projected vertex per exact
    // vertex, and the polygon's own certified triangulation.
    bool projectable = true;
    for (const Entry &entry : arrangement->entries()) {
        const Polygon &footprint = entry.placement.footprint();
        if (footprint.vertices().size() < 3
            || footprint.triangulation().size() != footprint.vertices().size() - 2) {
            projectable = false;
            continue;
        }
        for (const Point &vertex : footprint.vertices()) {
            const godot::Vector2 projected = editor_->project(
                static_cast<double>(vertex.x.raw())
                    / static_cast<double>(Coordinate::SCALE),
                static_cast<double>(vertex.y.raw())
                    / static_cast<double>(Coordinate::SCALE));
            if (!std::isfinite(static_cast<double>(projected.x))
                || !std::isfinite(static_cast<double>(projected.y))) {
                projectable = false;
            }
        }
    }
    expect(projectable, "every drawn footprint projects from its own exact geometry");

    const std::optional<std::size_t> active = editor_->active_proposal();
    if (active.has_value() && active.value() < editor_->proposals().size()) {
        const OrientedPrototile *variant = editor_->selected_variant();
        const Placement &ghost = editor_->proposals()[active.value()].placement;
        expect(
            variant != nullptr
                && ghost.footprint().vertices().size()
                    == variant->canonical_polygon().vertices().size(),
            "the ghost is the selected variant's exact footprint");
    }

    expect(
        editor_->pixels_per_unit() > 0.0,
        "the presentation projection has a positive scale");
}

// --- the hex-12 domain ---

void LevelEditorIntegrationRunner::check_hex12_blueprint() {
    if (!expect(
            lock_palette(
                content::GeometryDomain::hex12,
                { HEX_ROW_TRIANGLE, HEX_ROW_HEXAGON },
                { UNLIMITED, UNLIMITED }),
            "a hex-12 palette locks")) {
        return;
    }

    const engine::Palette *palette = editor_->palette();
    if (palette == nullptr || palette->order() != 2) {
        return;
    }
    expect(
        palette->entries()[0].prototile().id() == PrototileId(35)
            && palette->entries()[0].orientations().size() == 4,
        "the hex-12 triangle compiles four distinct orientations");
    expect(
        palette->entries()[1].prototile().id() == PrototileId(36)
            && palette->entries()[1].orientations().size() == 2,
        "the hex-12 hexagon compiles two distinct orientations");

    // The hexagon first, at exact origin.
    editor_->select_entry(1);
    expect(
        editor_->proposals().size() == 1
            && editor_->proposals().front().record.translation == origin_point(),
        "the first hex-12 placement is at exact origin");
    expect(editor_->accept_active_proposal(), "the first hex-12 placement is accepted");

    // Then triangles, through the same domain-blind proposal path.
    editor_->select_entry(0);
    expect(!editor_->proposals().empty(), "the hexagon offers hex-12 triangle placements");

    std::size_t accepted = 0;
    bool contacting = true;
    bool disjoint = true;
    for (int i = 0; i < 3; ++i) {
        const std::optional<std::size_t> active = editor_->active_proposal();
        if (!active.has_value() || active.value() >= editor_->proposals().size()) {
            break;
        }
        const Placement candidate = editor_->proposals()[active.value()].placement;
        if (!shares_positive_length_contact(candidate)) {
            contacting = false;
        }
        if (overlaps_blueprint(candidate)) {
            disjoint = false;
        }
        if (editor_->accept_active_proposal()) {
            ++accepted;
        }
    }
    expect(accepted == 3, "repeated hex-12 additions succeed");
    expect(contacting, "every accepted hex-12 placement shares positive-length contact");
    expect(disjoint, "no accepted hex-12 placement overlaps the blueprint");
    expect(
        editor_->blueprint().size() == 4,
        "the hex-12 blueprint holds one hexagon and three triangles");

    // Four distinct triangle orientations, cycling in both directions.
    editor_->select_entry(0);
    bool cycled = true;
    for (std::size_t i = 0; i < 4; ++i) {
        if (editor_->selection()->orientation != i) {
            cycled = false;
        }
        editor_->cycle_orientation(true);
    }
    if (editor_->selection()->orientation != 0) {
        cycled = false;
    }
    expect(cycled, "hex-12 orientation cycling visits every distinct variant and wraps");

    const Arrangement *arrangement = editor_->arrangement();
    if (arrangement != nullptr) {
        bool exact = arrangement->entries().size() == editor_->blueprint().size();
        for (std::size_t i = 0; i < arrangement->entries().size(); ++i) {
            if (arrangement->entries()[i].placement.translation()
                != editor_->blueprint()[i].translation) {
                exact = false;
            }
        }
        expect(exact, "the hex-12 arrangement is exactly its records");
    }
}

// --- the coverage proof ---

bool LevelEditorIntegrationRunner::place_at(Point p_translation) {
    std::optional<std::size_t> target;
    for (std::size_t i = 0; i < editor_->proposals().size(); ++i) {
        if (editor_->proposals()[i].record.translation == p_translation) {
            target = i;
            break;
        }
    }
    if (!target.has_value()) {
        return false;
    }
    // Chosen the way an author chooses one: the pointer moves to that
    // proposal's projected handle and the ghost that was shown is the one
    // accepted.
    editor_->set_pointer(
        projected_handle(*editor_, editor_->proposals()[target.value()].placement));
    if (editor_->active_proposal() != target) {
        return false;
    }
    return editor_->accept_active_proposal();
}

bool LevelEditorIntegrationRunner::proof_is_exclusive() const {
    if (editor_->phase() != LevelEditor::EditorPhase::build_blueprint) {
        return editor_->region() == nullptr && editor_->region_error() == nullptr;
    }
    const bool exactly_one =
        (editor_->region() != nullptr) != (editor_->region_error() != nullptr);
    const Arrangement *arrangement = editor_->arrangement();
    return exactly_one && arrangement != nullptr
        && arrangement->entries().size() == editor_->blueprint().size();
}

std::optional<Placement> LevelEditorIntegrationRunner::placement_of_record(
    const engine::BlueprintPlacement &p_record) const {
    const engine::Palette *palette = editor_->palette();
    if (palette == nullptr) {
        return std::nullopt;
    }
    for (const engine::PaletteEntry &entry : palette->entries()) {
        if (entry.prototile().id() != p_record.prototile_id) {
            continue;
        }
        for (const OrientedPrototile &variant : entry.orientations()) {
            if (variant.orientation() != p_record.orientation) {
                continue;
            }
            auto placement = Placement::make(variant, p_record.translation);
            if (!placement) {
                return std::nullopt;
            }
            return std::move(placement).value();
        }
    }
    return std::nullopt;
}

void LevelEditorIntegrationRunner::check_region_proof_lifecycle() {
    if (!expect(
            lock_palette(
                content::GeometryDomain::lattice,
                { ROW_TETROMINO_O, ROW_SQUARE_1 },
                { UNLIMITED, UNLIMITED }),
            "a lattice palette locks for the coverage checks")) {
        return;
    }

    // A locked palette begins with an empty arrangement and the exact empty
    // coverage failure, not with an absent proof.
    expect(editor_->blueprint().empty(), "a locked palette begins with no records");
    expect(
        editor_->arrangement() != nullptr && editor_->arrangement()->entries().empty(),
        "a locked palette begins with an empty arrangement");
    expect(editor_->region() == nullptr, "an empty blueprint derives no region");
    if (expect(
            editor_->region_error() != nullptr,
            "an empty blueprint publishes a typed region failure")) {
        expect(
            editor_->region_error()->code
                == ArrangementRegionErrorCode::empty_arrangement,
            "the empty blueprint's failure is exactly empty_arrangement");
    }
    expect(proof_is_exclusive(), "a locked palette publishes exactly one proof result");
    expect(!editor_->can_export(), "an empty blueprint cannot be exported");

    // One valid placement is immediately one region.
    expect(editor_->accept_active_proposal(), "the first placement is accepted");
    expect(proof_is_exclusive(), "one placement publishes exactly one proof result");
    expect(editor_->region_error() == nullptr, "one placement publishes no failure");
    if (expect(editor_->region() != nullptr, "one placement immediately derives a region")) {
        expect(
            same_vertices(
                editor_->region()->outer_boundary(),
                { unit_point(0, 0), unit_point(2, 0), unit_point(2, 2), unit_point(0, 2) }),
            "the derived region is exactly the placed footprint");
        expect(
            editor_->region()->inner_boundaries().empty(),
            "one placement derives no hole");
        expect(
            editor_->region()->doubled_area() == doubled_unit_area(4),
            "the derived region has exactly the covered area");
    }
    expect(editor_->can_export(), "one valid placement makes the document exportable");

    // Partial-edge contact: one unit square against half of the tetromino's
    // right edge. Whole-edge cancellation alone could not derive this.
    editor_->select_entry(1);
    if (expect(place_at(unit_point(2, 0)), "a partial-edge lattice placement is accepted")) {
        expect(proof_is_exclusive(), "partial-edge coverage publishes one proof result");
        if (expect(
                editor_->region() != nullptr,
                "partial-edge coverage derives one region")) {
            expect(
                same_vertices(
                    editor_->region()->outer_boundary(),
                    {
                        unit_point(0, 0),
                        unit_point(3, 0),
                        unit_point(3, 1),
                        unit_point(2, 1),
                        unit_point(2, 2),
                        unit_point(0, 2),
                    }),
                "partial-edge coverage derives the exact canonical boundary");
            expect(
                editor_->region()->inner_boundaries().empty(),
                "partial-edge coverage derives no hole");
            expect(
                editor_->region()->doubled_area() == doubled_unit_area(5)
                    && editor_->region()->doubled_area()
                        == summed_footprint_area(*editor_->arrangement()),
                "partial-edge coverage derives exactly the covered area");
        }
    }

    // A rejected edit changes nothing at all, proof included.
    const DocumentSnapshot before = capture();
    expect(!editor_->remove_record(editor_->blueprint().size()), "a nonexistent record cannot be removed");
    expect(
        unchanged_since(before),
        "a rejected edit preserves the records, arrangement, and proof exactly");

    editor_->clear_blueprint();
    expect(editor_->region() == nullptr, "clearing discards the derived region");
    if (expect(
            editor_->region_error() != nullptr,
            "clearing republishes a typed region failure")) {
        expect(
            editor_->region_error()->code
                == ArrangementRegionErrorCode::empty_arrangement,
            "clearing returns to exactly empty_arrangement");
    }
    expect(proof_is_exclusive(), "clearing publishes exactly one proof result");
    expect(!editor_->can_export(), "a cleared blueprint cannot be exported");
    expect(
        editor_->status_text().ends_with("add at least one tile before export"),
        "an accepted edit reports the empty coverage concisely");
}

void LevelEditorIntegrationRunner::check_region_hole() {
    if (!expect(
            lock_palette(content::GeometryDomain::lattice, { ROW_SQUARE_1 }, { UNLIMITED }),
            "a unit-square palette locks for the hole checks")) {
        return;
    }

    // A ring of eight unit squares, each one placed against the last, enclosing
    // exactly one empty cell.
    expect(editor_->accept_active_proposal(), "the ring begins at exact origin");
    const Point ring[] = {
        unit_point(1, 0), unit_point(2, 0), unit_point(2, 1), unit_point(2, 2),
        unit_point(1, 2), unit_point(0, 2), unit_point(0, 1),
    };
    bool grew = true;
    bool exclusive = true;
    for (const Point &translation : ring) {
        if (!place_at(translation)) {
            grew = false;
            break;
        }
        if (!proof_is_exclusive()) {
            exclusive = false;
        }
    }
    expect(grew, "every ring placement is offered and accepted");
    expect(exclusive, "every ring publication carries exactly one proof result");
    if (!grew) {
        return;
    }

    expect(editor_->blueprint().size() == 8, "the ring holds eight placements");
    if (expect(editor_->region() != nullptr, "connected ring coverage derives one region")) {
        expect(
            same_vertices(
                editor_->region()->outer_boundary(),
                { unit_point(0, 0), unit_point(3, 0), unit_point(3, 3), unit_point(0, 3) }),
            "the ring's outer boundary is exactly the enclosing square");
        if (expect(
                editor_->region()->inner_boundaries().size() == 1,
                "the ring derives exactly one hole")) {
            expect(
                same_vertices(
                    editor_->region()->inner_boundaries()[0],
                    {
                        unit_point(1, 1),
                        unit_point(2, 1),
                        unit_point(2, 2),
                        unit_point(1, 2),
                    }),
                "the hole is exactly the uncovered cell");
        }
        expect(
            editor_->region()->doubled_area() == doubled_unit_area(8)
                && editor_->region()->doubled_area()
                    == summed_footprint_area(*editor_->arrangement()),
            "the ring's region area excludes exactly the hole");
    }
    expect(editor_->can_export(), "coverage with a hole is exportable");
}

void LevelEditorIntegrationRunner::check_disconnected_coverage() {
    editor_->clear_blueprint();
    expect(editor_->accept_active_proposal(), "the bridge fixture begins at exact origin");
    const bool built = place_at(unit_point(1, 0)) && place_at(unit_point(2, 0));
    if (!expect(built, "a three-tile bridge is built")) {
        return;
    }
    expect(editor_->region() != nullptr, "the intact bridge derives one region");

    const std::vector<engine::BlueprintPlacement> before = editor_->blueprint();
    expect(editor_->remove_record(1), "the bridge placement is removed");
    expect(
        editor_->blueprint().size() == 2,
        "removing a bridge is not rolled back by the region failure");
    expect(
        same_record(editor_->blueprint()[0], before[0])
            && same_record(editor_->blueprint()[1], before[2]),
        "removal keeps exactly the two remaining records in stored order");
    expect(
        editor_->arrangement() != nullptr && editor_->arrangement()->entries().size() == 2,
        "the arrangement is republished beside the failure");
    expect(editor_->region() == nullptr, "disconnected coverage derives no region");
    if (expect(
            editor_->region_error() != nullptr,
            "disconnected coverage publishes a typed failure")) {
        expect(
            editor_->region_error()->code
                == ArrangementRegionErrorCode::disconnected_coverage,
            "the failure is exactly disconnected_coverage");
        expect(
            editor_->region_error()->component_points.size() == 2
                && editor_->region_error()->component_points[0] == unit_point(0, 0)
                && editor_->region_error()->component_points[1] == unit_point(2, 0),
            "the failure names both components in lexicographic order");
    }
    expect(proof_is_exclusive(), "a disconnected publication carries exactly one result");
    expect(!editor_->can_export(), "disconnected coverage cannot be exported");

    expect(
        editor_->status_text().ends_with("coverage is disconnected"),
        "an accepted edit reports disconnected coverage concisely");

    godot::Button *button = editor_->action_button("ExportButton");
    expect(
        button != nullptr && button->is_disabled(),
        "the export button is disabled for disconnected coverage");
}

void LevelEditorIntegrationRunner::check_nonmanifold_coverage() {
    editor_->clear_blueprint();
    expect(editor_->accept_active_proposal(), "the pinch fixture begins at exact origin");
    // A 3 x 3 block less its centre and one corner: every cell shares a
    // positive-length edge with the coverage it joins, and the two ends meet at
    // exactly one point.
    const Point pinch[] = {
        unit_point(1, 0), unit_point(2, 0), unit_point(2, 1),
        unit_point(2, 2), unit_point(1, 2), unit_point(0, 1),
    };
    bool grew = true;
    for (const Point &translation : pinch) {
        if (!place_at(translation)) {
            grew = false;
            break;
        }
    }
    if (!expect(grew, "every pinch placement is offered and accepted")) {
        return;
    }

    expect(editor_->blueprint().size() == 7, "the pinch fixture holds seven placements");
    expect(editor_->region() == nullptr, "pinched coverage derives no region");
    if (expect(
            editor_->region_error() != nullptr,
            "pinched coverage publishes a typed failure")) {
        expect(
            editor_->region_error()->code
                == ArrangementRegionErrorCode::nonmanifold_boundary_vertex,
            "the failure is exactly nonmanifold_boundary_vertex");
        const std::optional<NonmanifoldVertex> &vertex =
            editor_->region_error()->nonmanifold_vertex;
        expect(
            vertex.has_value() && vertex->point == unit_point(1, 2),
            "the failure names the exact pinch point");
        expect(
            vertex.has_value() && vertex->indegree == 2 && vertex->outdegree == 2,
            "the pinch point carries its exact boundary degrees");
    }
    expect(proof_is_exclusive(), "a nonmanifold publication carries exactly one result");
    expect(!editor_->can_export(), "pinched coverage cannot be exported");
    expect(
        editor_->status_text().ends_with("coverage boundary is nonmanifold"),
        "an accepted edit reports a nonmanifold boundary concisely");

    // The pinch is repairable by ordinary authoring: the valid blueprint was
    // never rolled back, so filling the missing corner derives a region again.
    expect(place_at(unit_point(0, 2)), "the missing corner is still offered");
    expect(editor_->region() != nullptr, "filling the pinch derives one region again");
    expect(editor_->can_export(), "the repaired coverage is exportable");
}

void LevelEditorIntegrationRunner::check_hex12_region() {
    if (!expect(
            lock_palette(
                content::GeometryDomain::hex12,
                { HEX_ROW_TRIANGLE, HEX_ROW_HEXAGON },
                { UNLIMITED, UNLIMITED }),
            "a hex-12 palette locks for the coverage checks")) {
        return;
    }

    // One hexagon at exact origin, then triangles grown onto it through the
    // ordinary proposal path. No hex coordinate is authored here.
    editor_->select_entry(1);
    expect(editor_->accept_active_proposal(), "the hexagon is placed at exact origin");
    editor_->select_entry(0);
    std::size_t accepted = 0;
    bool exclusive = true;
    for (int i = 0; i < 3; ++i) {
        if (editor_->accept_active_proposal()) {
            ++accepted;
        }
        if (!proof_is_exclusive()) {
            exclusive = false;
        }
    }
    expect(accepted == 3, "three hex-12 triangles are accepted");
    expect(exclusive, "every hex-12 publication carries exactly one proof result");

    const Arrangement *arrangement = editor_->arrangement();
    if (!expect(
            editor_->region() != nullptr && arrangement != nullptr,
            "valid hex-12 coverage derives one region")) {
        return;
    }
    expect(
        editor_->region()->doubled_area() == summed_footprint_area(*arrangement),
        "the hex-12 region's exact area is exactly the covered area");
    expect(
        editor_->region()->inner_boundaries().empty(),
        "the hex-12 coverage derives no hole");

    // The same coverage, rebuilt from the published records in reverse order
    // through ordinary checked factories, derives exactly the same boundary: the
    // installed region depends on coverage alone, not on authoring history.
    Arrangement rebuilt;
    bool rebuildable = true;
    for (std::size_t offset = editor_->blueprint().size(); offset > 0; --offset) {
        std::optional<Placement> placement =
            placement_of_record(editor_->blueprint()[offset - 1]);
        if (!placement.has_value() || !rebuilt.try_insert(std::move(placement).value())) {
            rebuildable = false;
            break;
        }
    }
    if (!expect(rebuildable, "the hex-12 records rebuild one exact arrangement")) {
        return;
    }
    auto independent = region_from_arrangement(rebuilt);
    if (expect(bool(independent), "the rebuilt hex-12 coverage derives one region")) {
        expect(
            same_boundary(
                independent.value().outer_boundary(), editor_->region()->outer_boundary()),
            "the installed hex-12 region is the canonical region of its coverage");
        expect(
            independent.value().inner_boundaries().size()
                == editor_->region()->inner_boundaries().size()
                && independent.value().doubled_area() == editor_->region()->doubled_area(),
            "the rebuilt hex-12 region agrees exactly in holes and area");
    }
}

// --- the document, captured whole ---

LevelEditorIntegrationRunner::DocumentSnapshot
LevelEditorIntegrationRunner::capture() const {
    DocumentSnapshot snapshot;
    snapshot.phase = editor_->phase();
    snapshot.domain = editor_->domain();
    snapshot.has_document = snapshot.domain.has_value();
    snapshot.records = editor_->blueprint();

    const engine::Palette *palette = editor_->palette();
    if (palette != nullptr) {
        snapshot.palette_order = palette->order();
        for (const engine::PaletteEntry &entry : palette->entries()) {
            snapshot.palette_ids.push_back(entry.prototile().id());
        }
    }
    for (std::size_t index = 0; index < snapshot.palette_order; ++index) {
        const std::optional<godot::Color> color = editor_->entry_color(index);
        snapshot.colors.push_back(
            color.has_value() ? color.value() : godot::Color(0.0f, 0.0f, 0.0f, 0.0f));
    }
    if (editor_->arrangement() != nullptr) {
        for (const Entry &entry : editor_->arrangement()->entries()) {
            snapshot.arrangement_translations.push_back(entry.placement.translation());
        }
    }
    if (editor_->region() != nullptr) {
        snapshot.has_region = true;
        snapshot.region_vertices = editor_->region()->outer_boundary().vertices();
        snapshot.hole_count = editor_->region()->inner_boundaries().size();
    }
    if (editor_->region_error() != nullptr) {
        snapshot.region_error = editor_->region_error()->code;
    }
    snapshot.selection = editor_->selection();
    snapshot.active_proposal = editor_->active_proposal();
    snapshot.proposal_count = editor_->proposals().size();
    snapshot.pixels_per_unit = editor_->pixels_per_unit();
    snapshot.camera_origin = editor_->camera_origin();
    while (editor_->entry_select_control(snapshot.entry_control_count) != nullptr) {
        ++snapshot.entry_control_count;
    }
    return snapshot;
}

bool LevelEditorIntegrationRunner::unchanged_since(const DocumentSnapshot &p_before) const {
    const DocumentSnapshot now = capture();
    if (now.has_document != p_before.has_document || now.phase != p_before.phase
        || now.domain != p_before.domain || now.palette_order != p_before.palette_order
        || now.palette_ids != p_before.palette_ids || now.hole_count != p_before.hole_count
        || now.has_region != p_before.has_region
        || now.region_error != p_before.region_error
        || now.active_proposal != p_before.active_proposal
        || now.proposal_count != p_before.proposal_count
        || now.pixels_per_unit != p_before.pixels_per_unit
        || now.camera_origin != p_before.camera_origin
        || now.entry_control_count != p_before.entry_control_count) {
        return false;
    }
    if (now.colors.size() != p_before.colors.size()) {
        return false;
    }
    for (std::size_t i = 0; i < now.colors.size(); ++i) {
        if (now.colors[i] != p_before.colors[i]) {
            return false;
        }
    }
    if (!same_records(now.records, p_before.records)
        || now.arrangement_translations != p_before.arrangement_translations
        || now.region_vertices != p_before.region_vertices) {
        return false;
    }
    if (now.selection.has_value() != p_before.selection.has_value()) {
        return false;
    }
    return !now.selection.has_value()
        || (now.selection->entry == p_before.selection->entry
            && now.selection->orientation == p_before.selection->orientation);
}

// --- the export control and its dialog ---

void LevelEditorIntegrationRunner::check_export_controls() {
    godot::Button *button = editor_->action_button("ExportButton");
    if (expect(button != nullptr, "the toolbar offers one export action")) {
        expect(button->get_text() == godot::String("export .tres"), "the export action is labelled export .tres");
    }

    godot::FileDialog *dialog = godot::Object::cast_to<godot::FileDialog>(
        editor_->get_node_or_null(godot::NodePath("ExportDialog")));
    if (expect(dialog != nullptr, "the scene owns one export file dialog")) {
        expect(
            dialog->get_access() == godot::FileDialog::ACCESS_FILESYSTEM,
            "the export dialog browses the host filesystem");
        expect(
            dialog->get_file_mode() == godot::FileDialog::FILE_MODE_SAVE_FILE,
            "the export dialog saves one file");
        const godot::PackedStringArray filters = dialog->get_filters();
        expect(
            filters.size() == 1 && godot::String(filters[0]).begins_with("*.tres"),
            "the export dialog exposes only .tres");
        expect(
            dialog->is_customization_flag_enabled(
                godot::FileDialog::CUSTOMIZATION_OVERWRITE_WARNING),
            "the export dialog warns before overwriting");
        expect(dialog->get_use_native_dialog(), "the export dialog prefers a native dialog");
        expect(
            dialog->get_current_file() == godot::String("level.tres"),
            "the export dialog opens on level.tres");
    }

    // Domain phase: nothing to export, and the action says so without opening
    // anything.
    editor_->return_to_domain_choice();
    expect(!editor_->can_export(), "a document-less editor cannot export");
    expect(
        button != nullptr && button->is_disabled(),
        "the export button is disabled with no document");
    editor_->on_export_pressed();
    expect(
        dialog == nullptr || !dialog->is_visible(),
        "pressing export with no document does not open the dialog");

    // Palette phase: still nothing to export.
    expect(
        editor_->choose_domain(content::GeometryDomain::lattice),
        "a lattice document is started for the export control checks");
    expect(!editor_->can_export(), "an unlocked palette cannot export");
    expect(
        button != nullptr && button->is_disabled(),
        "the export button is disabled in palette phase");
    editor_->on_export_pressed();
    expect(
        dialog == nullptr || !dialog->is_visible(),
        "pressing export in palette phase does not open the dialog");

    if (!expect(
            lock_palette(content::GeometryDomain::lattice, { ROW_SQUARE_1 }, { UNLIMITED }),
            "a palette locks for the export control checks")) {
        return;
    }
    expect(
        button != nullptr && button->is_disabled(),
        "the export button is disabled while the blueprint is empty");
    const DocumentSnapshot empty_document = capture();
    editor_->on_export_pressed();
    expect(
        dialog == nullptr || !dialog->is_visible(),
        "pressing export with an empty blueprint does not open the dialog");
    expect(
        unchanged_since(empty_document),
        "a refused export press changes nothing about the document");

    expect(editor_->accept_active_proposal(), "one placement is made exportable");
    expect(editor_->can_export(), "one valid placement can be exported");
    expect(
        button != nullptr && !button->is_disabled(),
        "the export button is enabled exactly when the document can be exported");
}

// --- refused destinations ---

void LevelEditorIntegrationRunner::check_export_refusals() {
    if (!expect(editor_->can_export(), "an exportable document exists for the refusal checks")) {
        return;
    }

    const DocumentSnapshot before = capture();
    expect(!editor_->export_document(godot::String()), "an empty path is refused");
    expect(
        !editor_->export_document(temporary_path(EXPORT_BINARY_FILE)),
        "a .res destination is refused");
    expect(
        !editor_->export_document(temporary_path(EXPORT_FOREIGN_FILE)),
        "a .json destination is refused");
    expect(
        !godot::FileAccess::file_exists(temporary_path(EXPORT_BINARY_FILE))
            && !godot::FileAccess::file_exists(temporary_path(EXPORT_FOREIGN_FILE)),
        "a refused destination is never written");
    expect(
        unchanged_since(before), "a refused export preserves the complete document");
}

// --- one written artifact, and the consumer's view of it ---

void LevelEditorIntegrationRunner::check_export_round_trip() {
    const content::PrototileCatalog *catalog = editor_->catalog();
    if (catalog == nullptr || !expect(editor_->can_export(), "an exportable lattice document exists")) {
        return;
    }

    const godot::String path = temporary_path(EXPORT_FILE);
    const DocumentSnapshot before = capture();
    if (!expect(editor_->export_document(path), "an exportable document exports")) {
        return;
    }
    expect(godot::FileAccess::file_exists(path), "the export writes exactly one named file");
    expect(unchanged_since(before), "a successful export preserves the complete document");
    expect(
        editor_->can_export(),
        "a successful export leaves the blueprint editable and exportable");

    // A path with no extension receives .tres, and nothing is written beside it.
    const godot::String bare = temporary_path(EXPORT_EXTENSIONLESS_FILE);
    expect(editor_->export_document(bare), "a path without an extension is accepted");
    expect(
        godot::FileAccess::file_exists(temporary_path(EXPORT_EXTENSIONLESS_RESULT)),
        "a path without an extension receives .tres");
    expect(!godot::FileAccess::file_exists(bare), "no extensionless file is written");

    // The extension check is case-insensitive and preserves the name it was
    // given.
    expect(
        editor_->export_document(temporary_path(EXPORT_UPPERCASE_FILE)),
        "an uppercase .TRES destination is accepted");
    expect(
        godot::FileAccess::file_exists(temporary_path(EXPORT_UPPERCASE_FILE)),
        "an uppercase destination is written under exactly its own name");

    // Every export names its own destination: nothing is remembered between
    // them, and the earlier artifact is untouched.
    expect(
        editor_->export_document(temporary_path(EXPORT_SECOND_FILE)),
        "a second export to another destination succeeds");
    expect(
        godot::FileAccess::file_exists(temporary_path(EXPORT_SECOND_FILE))
            && godot::FileAccess::file_exists(path),
        "a later export leaves the earlier artifact in place");
    expect(
        !editor_->export_document(godot::String()),
        "an empty path is still refused after a successful export: no destination is remembered");

    // The consumer's view of the artifact, through beta's public loader.
    auto loaded = load_level_resource(path, *catalog);
    if (!expect(bool(loaded), "the exported lattice artifact loads through the consumer path")) {
        return;
    }
    const CompiledLevelResource &compiled = loaded.value().compiled;
    expect(
        compiled.domain == content::GeometryDomain::lattice,
        "the artifact carries the authored geometry domain");
    expect(
        same_records(compiled.blueprint, editor_->blueprint()),
        "the artifact carries exactly the authored records, in order");
    if (expect(
            editor_->region() != nullptr, "the exported document still owns its region")) {
        expect(
            same_boundary(
                compiled.level.region().outer_boundary(),
                editor_->region()->outer_boundary()),
            "the reloaded region is exactly the derived region");
        expect(
            compiled.level.region().inner_boundaries().size()
                == editor_->region()->inner_boundaries().size()
                && compiled.level.region().doubled_area()
                    == editor_->region()->doubled_area(),
            "the reloaded region agrees exactly in holes and area");
    }
    const engine::Palette *palette = editor_->palette();
    if (palette != nullptr) {
        bool same_palette = compiled.level.palette().order() == palette->order();
        for (std::size_t i = 0;
             i < palette->entries().size() && i < compiled.level.palette().entries().size();
             ++i) {
            if (compiled.level.palette().entries()[i].prototile().id()
                    != palette->entries()[i].prototile().id()
                || compiled.level.palette().entries()[i].supply()
                    != palette->entries()[i].supply()) {
                same_palette = false;
            }
        }
        expect(same_palette, "the artifact carries exactly the authored palette");
    }
    const godot::Ref<PaletteResource> palette_resource = loaded.value().resource->get_palette();
    if (expect(palette_resource.is_valid(), "the artifact carries its authored palette resource")) {
        const godot::TypedArray<PaletteEntryResource> entries = palette_resource->get_entries();
        bool same_colors = static_cast<std::size_t>(entries.size())
            == (palette != nullptr ? palette->order() : 0);
        for (std::int64_t i = 0; i < entries.size(); ++i) {
            const godot::Ref<PaletteEntryResource> entry = entries[i];
            const std::optional<godot::Color> authored =
                editor_->entry_color(static_cast<std::size_t>(i));
            if (entry.is_null() || !authored.has_value()
                || entry->get_color() != authored.value()) {
                same_colors = false;
            }
        }
        expect(same_colors, "the artifact carries exactly the authored colors");
    }

    // The same proof for hex-12, whose exact coverage no lattice fixture
    // reaches.
    if (!expect(
            lock_palette(
                content::GeometryDomain::hex12,
                { HEX_ROW_TRIANGLE, HEX_ROW_HEXAGON },
                { UNLIMITED, UNLIMITED }),
            "a hex-12 palette locks for the export round trip")) {
        return;
    }
    editor_->select_entry(1);
    expect(editor_->accept_active_proposal(), "the hex-12 hexagon is placed");
    editor_->select_entry(0);
    editor_->accept_active_proposal();
    editor_->accept_active_proposal();
    if (!expect(editor_->can_export(), "the hex-12 document is exportable")) {
        return;
    }

    const godot::String hex_path = temporary_path(EXPORT_HEX12_FILE);
    const DocumentSnapshot hex_before = capture();
    if (!expect(editor_->export_document(hex_path), "the hex-12 document exports")) {
        return;
    }
    expect(unchanged_since(hex_before), "the hex-12 export preserves the complete document");
    auto hex_loaded = load_level_resource(hex_path, *catalog);
    if (expect(bool(hex_loaded), "the exported hex-12 artifact loads through the consumer path")) {
        expect(
            hex_loaded.value().compiled.domain == content::GeometryDomain::hex12,
            "the hex-12 artifact carries its own geometry domain");
        expect(
            same_records(hex_loaded.value().compiled.blueprint, editor_->blueprint()),
            "the hex-12 artifact carries exactly the authored records");
        expect(
            editor_->region() != nullptr
                && same_boundary(
                    hex_loaded.value().compiled.level.region().outer_boundary(),
                    editor_->region()->outer_boundary()),
            "the reloaded hex-12 region is exactly the derived region");
    }
}

// --- a candidate which cannot be written at all ---

void LevelEditorIntegrationRunner::check_export_encoding_refusal() {
    if (!expect(
            lock_palette(content::GeometryDomain::lattice, { ROW_SQUARE_1 }, { UNLIMITED }),
            "a unit-square palette locks for the oversized blueprint")) {
        return;
    }

    // One more placement than a level artifact may carry. The blueprint itself
    // is perfectly valid and covers one exact region; only writing it is
    // refused.
    expect(editor_->accept_active_proposal(), "the oversized strip begins at exact origin");
    bool grew = true;
    for (std::int64_t x = 1; x <= 64; ++x) {
        if (!place_at(unit_point(x, 0))) {
            grew = false;
            break;
        }
    }
    if (!expect(grew, "a sixty-five placement strip is built")) {
        return;
    }
    expect(editor_->blueprint().size() == 65, "the strip holds sixty-five placements");
    expect(
        editor_->region() != nullptr && editor_->can_export(),
        "the oversized strip is one valid region and looks exportable");

    const godot::String path = temporary_path(EXPORT_OVERSIZED_FILE);
    const DocumentSnapshot before = capture();
    expect(
        !editor_->export_document(path),
        "a blueprint too large to encode is refused");
    expect(
        !godot::FileAccess::file_exists(path),
        "an encoding failure writes nothing at all");
    expect(
        unchanged_since(before),
        "an encoding failure preserves the complete document");
}

// --- the surfaces this act removed ---

void LevelEditorIntegrationRunner::check_absent_region_and_persistence_surface() {
    expect(
        !editor_->has_signal(godot::StringName("play_requested")),
        "the editor publishes no play request");
    expect(
        !editor_->has_method(godot::StringName("on_save_pressed"))
            && !editor_->has_method(godot::StringName("on_save_as_pressed"))
            && !editor_->has_method(godot::StringName("on_play_pressed"))
            && !editor_->has_method(godot::StringName("on_open_draft_pressed")),
        "the editor binds no save, play, or open callback");
    expect(
        !editor_->has_method(godot::StringName("on_restart_region_pressed"))
            && !editor_->has_method(godot::StringName("on_add_hole_pressed")),
        "the editor binds no region-drawing callback");

    expect(
        editor_->action_button("SaveButton") == nullptr
            && editor_->action_button("SaveAsButton") == nullptr
            && editor_->action_button("PlayButton") == nullptr
            && editor_->action_button("RestartRegionButton") == nullptr
            && editor_->action_button("AddHoleButton") == nullptr,
        "the toolbar offers no save, play, or region action");
    expect(
        editor_->action_button("LatticeButton") != nullptr
            && editor_->action_button("Hex12Button") != nullptr
            && editor_->action_button("BuildPaletteButton") != nullptr
            && editor_->action_button("ClearBlueprintButton") != nullptr,
        "the toolbar offers exactly this act's actions");

    expect(
        editor_->get_node_or_null(godot::NodePath("OpenDialog")) == nullptr
            && editor_->get_node_or_null(godot::NodePath("SaveDialog")) == nullptr
            && editor_->get_node_or_null(godot::NodePath("DiscardDialog")) == nullptr,
        "the scene owns no file or discard dialog");

    // The editor exports artifacts and does not persist documents: there is no
    // way in, and no remembered way back out.
    expect(
        !editor_->has_method(godot::StringName("on_open_pressed"))
            && !editor_->has_method(godot::StringName("on_load_pressed"))
            && !editor_->has_method(godot::StringName("on_export_as_pressed"))
            && !editor_->has_method(godot::StringName("load_document"))
            && !editor_->has_method(godot::StringName("save_document")),
        "the editor binds no open, load, or save-as operation");
    expect(
        editor_->action_button("OpenButton") == nullptr
            && editor_->action_button("LoadButton") == nullptr
            && editor_->action_button("ExportAsButton") == nullptr,
        "the toolbar offers no open, load, or export-as action");
    expect(
        editor_->get_node_or_null(godot::NodePath("LoadDialog")) == nullptr
            && editor_->get_node_or_null(godot::NodePath("PathLabel")) == nullptr
            && editor_->get_node_or_null(
                   godot::NodePath("StatusBar/Margin/Body/Line/PathLabel"))
                == nullptr,
        "the scene owns no load dialog and shows no document path");
    expect(
        !editor_->has_method(godot::StringName("level_resource"))
            && !editor_->has_method(godot::StringName("export_path"))
            && !editor_->has_method(godot::StringName("is_dirty"))
            && !editor_->has_method(godot::StringName("last_export_result")),
        "the editor publishes no resource, path, dirty state, or export result");

    // The derived region is authoring proof, not presentation: nothing draws it.
    expect(
        !editor_->has_method(godot::StringName("draw_region"))
            && !editor_->has_method(godot::StringName("draw_target"))
            && !editor_->has_method(godot::StringName("draw_region_boundary"))
            && !editor_->has_method(godot::StringName("draw_holes")),
        "the editor binds no region-drawing operation");
    expect(
        editor_->get_node_or_null(godot::NodePath("RegionOverlay")) == nullptr
            && editor_->get_node_or_null(godot::NodePath("TargetOverlay")) == nullptr
            && editor_->get_node_or_null(godot::NodePath("RegionPreview")) == nullptr,
        "the scene owns no region overlay");
}

// --- temporary files ---

void LevelEditorIntegrationRunner::remove_temporary(const godot::String &p_path) {
    if (godot::FileAccess::file_exists(p_path)) {
        godot::DirAccess::remove_absolute(p_path);
    }
}

void LevelEditorIntegrationRunner::check_temporary_files_removed() {
    bool all_removed = true;
    for (const godot::String &path : temporary_paths_) {
        if (godot::FileAccess::file_exists(path)) {
            all_removed = false;
        }
    }
    expect(all_removed, "every temporary file this run created was removed");

    // The directory itself goes only when it is empty, and only by its exact
    // name. Nothing recursive is ever removed.
    const godot::String directory = temporary_directory();
    if (godot::DirAccess::dir_exists_absolute(directory)
        && godot::DirAccess::get_files_at(directory).is_empty()
        && godot::DirAccess::get_directories_at(directory).is_empty()) {
        godot::DirAccess::remove_absolute(directory);
    }
}

// --- entry point ---

void LevelEditorIntegrationRunner::_ready() {
    // The real main scene, so the checks below observe exactly what the
    // application boots into rather than a parallel arrangement of controls.
    const godot::Ref<godot::PackedScene> scene =
        godot::ResourceLoader::get_singleton()->load(godot::String(MAIN_SCENE_PATH));
    if (!expect(scene.is_valid(), "the main scene loads")) {
        if (get_tree() != nullptr) {
            get_tree()->quit(1);
        }
        return;
    }

    godot::Node *instance = scene->instantiate();
    editor_ = godot::Object::cast_to<LevelEditor>(instance);
    if (!expect(editor_ != nullptr, "the main scene root is a LevelEditor")) {
        if (instance != nullptr) {
            memdelete(instance);
        }
        if (get_tree() != nullptr) {
            get_tree()->quit(1);
        }
        return;
    }
    add_child(editor_);

    // One temporary directory, and exactly the files named here. Every one is
    // removed before this run begins and again when it ends.
    temporary_paths_ = {
        temporary_path(EXPORT_FILE),
        temporary_path(EXPORT_UPPERCASE_FILE),
        temporary_path(EXPORT_EXTENSIONLESS_FILE),
        temporary_path(EXPORT_EXTENSIONLESS_RESULT),
        temporary_path(EXPORT_SECOND_FILE),
        temporary_path(EXPORT_HEX12_FILE),
        temporary_path(EXPORT_BINARY_FILE),
        temporary_path(EXPORT_FOREIGN_FILE),
        temporary_path(EXPORT_OVERSIZED_FILE),
    };
    for (const godot::String &path : temporary_paths_) {
        remove_temporary(path);
    }
    const godot::Error made =
        godot::DirAccess::make_dir_recursive_absolute(temporary_directory());
    expect(
        made == godot::OK || godot::DirAccess::dir_exists_absolute(temporary_directory()),
        "the narrow temporary directory is available");

    check_startup();
    check_domain_views();
    check_domain_discards();
    check_palette_locking();
    check_first_placement();
    check_selection_cycling();
    check_proposal_derivation();
    check_repeated_lattice_additions();
    check_supply_exhaustion();
    check_removal_and_clear();
    check_pointer_selection();
    check_drawing_inputs();
    check_hex12_blueprint();
    check_region_proof_lifecycle();
    check_region_hole();
    check_disconnected_coverage();
    check_nonmanifold_coverage();
    check_hex12_region();
    check_export_controls();
    check_export_refusals();
    check_export_round_trip();
    check_export_encoding_refusal();
    check_absent_region_and_persistence_surface();

    for (const godot::String &path : temporary_paths_) {
        remove_temporary(path);
    }
    check_temporary_files_removed();

    if (failures_ == 0) {
        godot::UtilityFunctions::print(
            "[tiles] level editor integration: ",
            number(static_cast<std::int64_t>(checks_)), " checks passed");
    } else {
        godot::UtilityFunctions::push_error(
            "[tiles] level editor integration: ",
            number(static_cast<std::int64_t>(failures_)), " of ",
            number(static_cast<std::int64_t>(checks_)), " checks failed");
    }

    if (get_tree() != nullptr) {
        get_tree()->quit(failures_ == 0 ? 0 : 1);
    }
}

} // namespace tiles::game
