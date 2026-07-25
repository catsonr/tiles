#include "game/testing/LevelEditorIntegrationRunner.h"

#include "content/PrototileCatalog.h"
#include "core/Arrangement.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Intersection.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "core/geometry/Predicates.h"
#include "engine/Palette.h"
#include "engine/Supply.h"
#include "game/PrototilePreview.h"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/node_path.hpp>
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
constexpr std::size_t ROW_DOMINO = 25; // id 26, the 2 x 1 domino
constexpr std::size_t ROW_SQUARE_1 = 26; // id 27, the unit square

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
        editor_->row_include_control(0) == nullptr,
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
        editor_->row_include_control(EXPECTED_LATTICE_ROWS) == nullptr,
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
        editor_->row_include_control(EXPECTED_HEX12_ROWS) == nullptr,
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
        editor_->row_include_control(0) == nullptr,
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
    check_absent_region_and_persistence_surface();

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
