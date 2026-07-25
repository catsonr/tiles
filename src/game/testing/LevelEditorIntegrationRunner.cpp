#include "game/testing/LevelEditorIntegrationRunner.h"

#include "content/PrototileCatalog.h"
#include "core/Prototile.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "game/PrototilePreview.h"
#include "game/resources/LevelPersistence.h"
#include "game/resources/ResourceCompiler.h"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/color_picker_button.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/spin_box.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

namespace tiles::game {

namespace {

// The one narrow generated directory this runner owns. Cleanup only ever names
// exact files beneath it, then removes the directory itself if it is empty.
const char *TEMPORARY_DIRECTORY = "res://.godot/tiles_level_editor_integration";

const char *MAIN_SCENE_PATH = "res://main.tscn";

// Catalog positions used by name below. The catalog ships ids 1..34 in order, so
// the zero-based row of id n is n - 1.
constexpr std::size_t ROW_TETROMINO_O = 0; // id 1
constexpr std::size_t ROW_TETROMINO_I = 1; // id 2
constexpr std::size_t ROW_TETROMINO_T = 2; // id 3
constexpr std::size_t ROW_PENTOMINO_F = 7; // id 8
constexpr std::size_t ROW_PENTOMINO_F_MIRRORED = 8; // id 9
constexpr std::size_t ROW_DOMINO = 25; // id 26
constexpr std::size_t ROW_SQUARE_1 = 26; // id 27

constexpr std::size_t EXPECTED_ROWS = 34;

// The editor's own default-color construction, restated here so the runner
// proves the shipped values rather than reading them back out of the object it
// is checking.
constexpr double DEFAULT_HUE_STEP = 0.6180339887498948;
constexpr float DEFAULT_SATURATION = 0.65f;
constexpr float DEFAULT_VALUE = 0.95f;

godot::Color expected_default_color(std::size_t p_index) {
    const double hue = std::fmod(
        static_cast<double>(p_index + 1) * DEFAULT_HUE_STEP, 1.0);
    return godot::Color::from_hsv(
        static_cast<float>(hue), DEFAULT_SATURATION, DEFAULT_VALUE, 1.0f);
}

godot::String temporary_path(const char *p_file) {
    return godot::String(TEMPORARY_DIRECTORY).path_join(godot::String(p_file));
}

godot::String number(std::int64_t p_value) {
    return godot::String::num_int64(p_value);
}

godot::Ref<PolygonResource> make_polygon(
    std::initializer_list<godot::Vector2> p_vertices) {
    godot::Ref<PolygonResource> polygon;
    polygon.instantiate();
    godot::PackedVector2Array vertices;
    for (const godot::Vector2 &vertex : p_vertices) {
        vertices.push_back(vertex);
    }
    polygon->set_vertices(vertices);
    return polygon;
}

godot::Ref<PaletteEntryResource> make_entry(
    std::int64_t p_id, std::int64_t p_supply, const godot::Color &p_color) {
    godot::Ref<PaletteEntryResource> entry;
    entry.instantiate();
    entry->set_prototile_id(p_id);
    entry->set_supply(p_supply);
    entry->set_color(p_color);
    return entry;
}

godot::Ref<PaletteResource> make_palette(
    const godot::TypedArray<PaletteEntryResource> &p_entries) {
    godot::Ref<PaletteResource> palette;
    palette.instantiate();
    palette->set_entries(p_entries);
    return palette;
}

godot::Ref<RegionResource> make_region(
    const godot::Ref<PolygonResource> &p_outer,
    const godot::TypedArray<PolygonResource> &p_holes) {
    godot::Ref<RegionResource> region;
    region.instantiate();
    region->set_outer_boundary(p_outer);
    region->set_inner_boundaries(p_holes);
    return region;
}

godot::Ref<LevelResource> make_level(
    const godot::Ref<PaletteResource> &p_palette,
    const godot::Ref<RegionResource> &p_region) {
    godot::Ref<LevelResource> level;
    level.instantiate();
    level->set_palette(p_palette);
    level->set_region(p_region);
    return level;
}

godot::Ref<RegionResource> make_unit_square_region() {
    return make_region(
        make_polygon({
            godot::Vector2(0.0f, 0.0f),
            godot::Vector2(6.0f, 0.0f),
            godot::Vector2(6.0f, 6.0f),
            godot::Vector2(0.0f, 6.0f),
        }),
        godot::TypedArray<PolygonResource>());
}

// A whole-game-unit coordinate, for comparing compiled vertices exactly.
Coordinate units(std::int64_t p_units) {
    return Coordinate::from_raw(p_units * Coordinate::SCALE);
}

using GridPoint = LevelEditor::GridPoint;

} // namespace

void LevelEditorIntegrationRunner::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_play_requested", "level"),
        &LevelEditorIntegrationRunner::on_play_requested);
}

void LevelEditorIntegrationRunner::on_play_requested(
    const godot::Ref<LevelResource> &p_level) {
    ++play_emissions_;
    last_played_ = p_level;
}

bool LevelEditorIntegrationRunner::expect(bool p_condition, const char *p_description) {
    ++checks_;
    if (!p_condition) {
        ++failures_;
        godot::UtilityFunctions::push_error(
            "[tiles] level editor integration failed: ", p_description);
    }
    return p_condition;
}

bool LevelEditorIntegrationRunner::draw_boundary(
    LevelEditor::BoundaryKind p_kind, const std::vector<GridPoint> &p_points) {
    if (p_kind == LevelEditor::BoundaryKind::new_hole) {
        editor_->begin_hole_loop();
    } else {
        editor_->begin_outer_loop();
    }
    if (editor_->open_loop() == nullptr) {
        return false;
    }
    for (const GridPoint &point : p_points) {
        editor_->append_point(point);
    }
    return editor_->close_loop();
}

// --- startup ---

void LevelEditorIntegrationRunner::check_startup() {
    if (!expect(editor_->has_document(), "startup owns one document")) {
        return;
    }
    const godot::Ref<LevelResource> level = editor_->level_resource();
    if (!expect(level.is_valid(), "startup owns one LevelResource")) {
        return;
    }
    expect(level->get_path().is_empty(), "the startup level is unsaved");
    expect(editor_->is_dirty(), "the startup level is dirty");
    expect(level->get_region().is_null(), "the startup level has no region");

    const godot::Ref<PaletteResource> palette = level->get_palette();
    if (expect(palette.is_valid(), "the startup level owns an embedded palette")) {
        expect(
            palette->get_entries().is_empty(),
            "the startup embedded palette is empty");
        expect(
            palette->get_path().is_empty(),
            "the startup palette has no independent resource path");
    }
    expect(
        editor_->compiled_palette() == nullptr,
        "an empty palette produces no compiled palette");
    expect(editor_->compiled_region() == nullptr, "startup has no compiled region");
    expect(editor_->open_loop() == nullptr, "startup leaves boundary drawing idle");

    godot::Button *new_level = editor_->action_button("NewLevelButton");
    godot::Button *open_draft = editor_->action_button("OpenDraftButton");
    godot::Button *restart = editor_->action_button("RestartRegionButton");
    godot::Button *add_hole = editor_->action_button("AddHoleButton");
    godot::Button *save = editor_->action_button("SaveButton");
    godot::Button *save_as = editor_->action_button("SaveAsButton");
    godot::Button *play = editor_->action_button("PlayButton");
    if (expect(
            new_level != nullptr && open_draft != nullptr && restart != nullptr
                && add_hole != nullptr && save != nullptr && save_as != nullptr
                && play != nullptr,
            "every toolbar action exists as an ordinary labelled Button")) {
        expect(
            !new_level->is_disabled() && !open_draft->is_disabled(),
            "new and open are available at startup");
        expect(!restart->is_disabled(), "restart region is available with a document");
        expect(add_hole->is_disabled(), "add hole needs a valid region");
        expect(
            save->is_disabled() && save_as->is_disabled() && play->is_disabled(),
            "save, save as, and play are disabled while the level is incomplete");
        expect(
            new_level->get_text() == godot::String("new level"),
            "the new-level action uses ordinary language");
    }
    expect(!editor_->can_save(), "an incomplete level cannot be saved");
}

// --- palette rows ---

void LevelEditorIntegrationRunner::check_palette_rows() {
    const content::PrototileCatalog *catalog = editor_->catalog();
    if (!expect(catalog != nullptr, "the editor owns a canonical catalog")) {
        return;
    }
    if (!expect(
            catalog->entries().size() == EXPECTED_ROWS,
            "the catalog ships exactly 34 entries")) {
        return;
    }
    if (!expect(
            editor_->palette_rows().size() == EXPECTED_ROWS,
            "the palette panel has exactly one row per catalog entry")) {
        return;
    }

    bool order_matches = true;
    bool names_match = true;
    bool shapes_match = true;
    bool defaults_match = true;
    bool controls_exist = true;
    for (std::size_t index = 0; index < EXPECTED_ROWS; ++index) {
        const LevelEditor::PaletteRow &row = editor_->palette_rows()[index];
        if (row.catalog_index != index) {
            order_matches = false;
        }
        if (row.included || !row.unlimited || row.finite_amount != 1) {
            defaults_match = false;
        }
        if (!(row.color == expected_default_color(index)) || row.color.a != 1.0f) {
            defaults_match = false;
        }

        godot::CheckBox *include = editor_->row_include_control(index);
        PrototilePreview *preview = editor_->row_preview_control(index);
        godot::Label *name = editor_->row_name_control(index);
        godot::OptionButton *supply = editor_->row_supply_control(index);
        godot::SpinBox *amount = editor_->row_amount_control(index);
        godot::ColorPickerButton *color = editor_->row_color_control(index);
        if (include == nullptr || preview == nullptr || name == nullptr
            || supply == nullptr || amount == nullptr || color == nullptr) {
            controls_exist = false;
            continue;
        }
        if (name->get_text()
            != godot::String(catalog->entries()[index].display_name().c_str())) {
            names_match = false;
        }
        if (preview->polygon() == nullptr
            || !same_boundary(
                *preview->polygon(), catalog->entries()[index].prototile().polygon())) {
            shapes_match = false;
        }
        if (include->is_pressed() || supply->get_selected() != 0
            || amount->get_value() != 1.0 || !(color->get_pick_color() == row.color)) {
            defaults_match = false;
        }
        if (!supply->is_disabled() || !color->is_disabled() || amount->is_editable()) {
            defaults_match = false;
        }
    }

    expect(order_matches, "palette rows follow canonical catalog order");
    expect(controls_exist, "every row owns its include, preview, name, supply, amount, and color controls");
    expect(names_match, "every row shows its catalog display name");
    expect(shapes_match, "every row previews its exact catalog polygon");
    expect(
        defaults_match,
        "every row begins excluded, unlimited, amount 1, opaque default color, with supply and color disabled");

    const PrototilePreview *f = editor_->row_preview_control(ROW_PENTOMINO_F);
    const PrototilePreview *f_mirrored =
        editor_->row_preview_control(ROW_PENTOMINO_F_MIRRORED);
    if (expect(
            f != nullptr && f_mirrored != nullptr && f->polygon() != nullptr
                && f_mirrored->polygon() != nullptr,
            "both chiral pentomino f previews carry geometry")) {
        expect(
            !same_boundary(*f->polygon(), *f_mirrored->polygon()),
            "a mirrored pentomino preview is geometrically distinct from its partner");
    }

    // Two adjacent rows do not share a default color, so the generated defaults
    // are actually distinguishable rather than merely deterministic.
    expect(
        !(expected_default_color(0) == expected_default_color(1)),
        "adjacent catalog positions receive different default colors");
}

void LevelEditorIntegrationRunner::check_palette_publication() {
    const godot::Ref<LevelResource> level = editor_->level_resource();
    if (!expect(level.is_valid(), "the palette checks have a level")) {
        return;
    }

    editor_->set_row_included(ROW_TETROMINO_O, true);
    {
        const godot::Ref<PaletteResource> palette = level->get_palette();
        const godot::TypedArray<PaletteEntryResource> entries = palette->get_entries();
        if (expect(entries.size() == 1, "including one row publishes exactly one entry")) {
            const godot::Ref<PaletteEntryResource> entry = entries[0];
            expect(
                entry.is_valid() && entry->get_prototile_id() == 1,
                "the published entry names its row's catalog id");
            expect(
                entry.is_valid() && entry->get_supply() == -1,
                "inclusion defaults to unlimited supply");
            expect(
                entry.is_valid() && entry->get_color() == expected_default_color(ROW_TETROMINO_O),
                "the published entry carries the row's color");
            expect(
                entry.is_valid() && entry->get_path().is_empty(),
                "published palette entries are embedded, not separately pathed");
        }
        expect(
            editor_->compiled_palette() != nullptr
                && editor_->compiled_palette()->order() == 1,
            "a nonempty palette compiles");
    }

    // Finite supply.
    editor_->set_row_unlimited(ROW_TETROMINO_O, false);
    editor_->set_row_finite_amount(ROW_TETROMINO_O, 5);
    {
        const godot::TypedArray<PaletteEntryResource> entries =
            level->get_palette()->get_entries();
        const godot::Ref<PaletteEntryResource> entry = entries.is_empty()
            ? godot::Ref<PaletteEntryResource>()
            : godot::Ref<PaletteEntryResource>(entries[0]);
        expect(
            entry.is_valid() && entry->get_supply() == 5,
            "finite mode encodes the selected positive amount");
    }

    // The finite control cannot produce zero, a negative, a fraction, or an
    // amount above the editor maximum.
    editor_->set_row_finite_amount(ROW_TETROMINO_O, 0);
    expect(
        editor_->palette_rows()[ROW_TETROMINO_O].finite_amount == 1,
        "a zero finite amount is clamped to one");
    editor_->set_row_finite_amount(ROW_TETROMINO_O, -7);
    expect(
        editor_->palette_rows()[ROW_TETROMINO_O].finite_amount == 1,
        "a negative finite amount is clamped to one");
    editor_->set_row_finite_amount(ROW_TETROMINO_O, 5000000);
    expect(
        editor_->palette_rows()[ROW_TETROMINO_O].finite_amount == 1000000,
        "a finite amount above the editor maximum is clamped");
    editor_->on_row_amount_changed(2.7, static_cast<std::int64_t>(ROW_TETROMINO_O));
    expect(
        editor_->palette_rows()[ROW_TETROMINO_O].finite_amount == 3,
        "a fractional spinbox value becomes an integer amount");
    {
        const godot::TypedArray<PaletteEntryResource> entries =
            level->get_palette()->get_entries();
        const godot::Ref<PaletteEntryResource> entry = entries.is_empty()
            ? godot::Ref<PaletteEntryResource>()
            : godot::Ref<PaletteEntryResource>(entries[0]);
        expect(
            entry.is_valid() && entry->get_supply() == 3,
            "every clamped amount is published as a positive supply");
    }

    // Color, including alpha normalization.
    editor_->set_row_color(ROW_TETROMINO_O, godot::Color(0.25f, 0.5f, 0.75f, 0.25f));
    {
        const godot::TypedArray<PaletteEntryResource> entries =
            level->get_palette()->get_entries();
        const godot::Ref<PaletteEntryResource> entry = entries.is_empty()
            ? godot::Ref<PaletteEntryResource>()
            : godot::Ref<PaletteEntryResource>(entries[0]);
        expect(
            entry.is_valid()
                && entry->get_color() == godot::Color(0.25f, 0.5f, 0.75f, 1.0f),
            "a chosen color persists its complete rgb with alpha forced to one");
    }

    // Exclusion removes an entry rather than storing a zero supply, and
    // re-inclusion restores the row's local choices.
    editor_->set_row_included(ROW_TETROMINO_O, false);
    {
        const godot::TypedArray<PaletteEntryResource> entries =
            level->get_palette()->get_entries();
        expect(entries.is_empty(), "excluding a row removes its entry");
        expect(
            editor_->compiled_palette() == nullptr,
            "an emptied palette clears the compiled palette");
        expect(!editor_->can_save(), "an empty palette disables save and play");
    }
    editor_->set_row_included(ROW_TETROMINO_O, true);
    {
        const godot::TypedArray<PaletteEntryResource> entries =
            level->get_palette()->get_entries();
        const godot::Ref<PaletteEntryResource> entry = entries.is_empty()
            ? godot::Ref<PaletteEntryResource>()
            : godot::Ref<PaletteEntryResource>(entries[0]);
        expect(
            entry.is_valid() && entry->get_supply() == 3
                && entry->get_color() == godot::Color(0.25f, 0.5f, 0.75f, 1.0f),
            "re-including a row restores its local supply and color choices");
    }

    // Including entries out of catalog order still serializes catalog order.
    editor_->set_row_included(ROW_SQUARE_1, true);
    editor_->set_row_included(ROW_DOMINO, true);
    editor_->set_row_included(ROW_TETROMINO_I, true);
    {
        const godot::TypedArray<PaletteEntryResource> entries =
            level->get_palette()->get_entries();
        if (expect(entries.size() == 4, "four included rows publish four entries")) {
            const godot::Ref<PaletteEntryResource> a = entries[0];
            const godot::Ref<PaletteEntryResource> b = entries[1];
            const godot::Ref<PaletteEntryResource> c = entries[2];
            const godot::Ref<PaletteEntryResource> d = entries[3];
            expect(
                a.is_valid() && b.is_valid() && c.is_valid() && d.is_valid()
                    && a->get_prototile_id() == 1 && b->get_prototile_id() == 2
                    && c->get_prototile_id() == 26 && d->get_prototile_id() == 27,
                "entries serialize in catalog order regardless of inclusion order");
        }
        // Row identity makes duplicates and unknown ids unrepresentable: there is
        // exactly one row per catalog id and no control which names an id.
        bool unique_ids = true;
        for (std::int64_t i = 0; i < entries.size(); ++i) {
            for (std::int64_t j = i + 1; j < entries.size(); ++j) {
                const godot::Ref<PaletteEntryResource> lhs = entries[i];
                const godot::Ref<PaletteEntryResource> rhs = entries[j];
                if (lhs.is_valid() && rhs.is_valid()
                    && lhs->get_prototile_id() == rhs->get_prototile_id()) {
                    unique_ids = false;
                }
            }
        }
        expect(unique_ids, "row controls cannot produce a duplicate prototile id");
        bool known_ids = true;
        for (std::int64_t i = 0; i < entries.size(); ++i) {
            const godot::Ref<PaletteEntryResource> entry = entries[i];
            if (!entry.is_valid()
                || editor_->catalog()->find(PrototileId(
                       static_cast<PrototileId::Value>(entry->get_prototile_id())))
                    == nullptr) {
                known_ids = false;
            }
        }
        expect(known_ids, "row controls cannot produce an unknown prototile id");
        expect(
            editor_->compiled_palette() != nullptr
                && editor_->compiled_palette()->order() == 4,
            "the four-entry palette compiles");
    }

    expect(
        editor_->is_dirty(), "publishing a palette marks the document dirty");
    expect(
        !editor_->can_save(),
        "a valid palette alone is not enough to save: the region is still absent");
}

// --- region authoring ---

void LevelEditorIntegrationRunner::check_region_authoring() {
    // Snapping happens through the same projection the canvas draws with, and
    // negative coordinates are included deliberately.
    {
        bool snapped_exactly = true;
        const std::vector<GridPoint> probes = {
            GridPoint { 0, 0 },
            GridPoint { 3, 5 },
            GridPoint { -4, -7 },
            GridPoint { -1, 2 },
        };
        for (const GridPoint &probe : probes) {
            const godot::Vector2 pixel = editor_->project(
                static_cast<double>(probe.x), static_cast<double>(probe.y));
            const auto snapped = editor_->snap_local(pixel);
            if (!snapped.has_value() || snapped.value() != probe) {
                snapped_exactly = false;
            }
        }
        expect(snapped_exactly, "integer grid points snap back to themselves, negatives included");

        // A pointer nudged a fraction of a unit still resolves to the same
        // integer, because the snap is a round, not a truncation.
        const godot::Vector2 nudged =
            editor_->project(-3.0, 4.0)
            + godot::Vector2(
                static_cast<real_t>(0.3 * editor_->pixels_per_unit()),
                static_cast<real_t>(-0.3 * editor_->pixels_per_unit()));
        const auto snapped = editor_->snap_local(nudged);
        expect(
            snapped.has_value() && snapped.value() == GridPoint { -3, 4 },
            "a pointer inside a cell snaps to the nearest integer point");

        // Deliberately out of the representable whole-unit range.
        const godot::Vector2 far_away = editor_->project(100000.0, 0.0);
        expect(
            !editor_->snap_local(far_away).has_value(),
            "a cursor outside the representable coordinate range does not snap");
    }

    // A non-finite pointer position cannot produce a point.
    {
        const double infinity = std::numeric_limits<double>::infinity();
        expect(
            !editor_->snap_local(
                     godot::Vector2(static_cast<real_t>(infinity), 0.0f))
                 .has_value(),
            "a non-finite cursor position does not snap");
    }

    editor_->begin_outer_loop();
    if (!expect(editor_->open_loop() != nullptr, "restart region opens an outer loop")) {
        return;
    }
    expect(
        editor_->open_loop()->kind == LevelEditor::BoundaryKind::outer_replacement,
        "the opened loop is an outer replacement");

    // Out-of-range points cannot append.
    expect(
        !editor_->append_point(GridPoint { 100000, 0 }),
        "an out-of-range point cannot be appended");
    expect(
        editor_->open_loop()->vertices.empty(),
        "a rejected point leaves the loop untouched");

    // Backspace on an empty loop is safe.
    editor_->remove_last_point();
    expect(editor_->open_loop() != nullptr, "backspace on an empty loop does nothing");

    editor_->append_point(GridPoint { 0, 0 });
    editor_->append_point(GridPoint { 0, 0 });
    expect(
        editor_->open_loop()->vertices.size() == 1,
        "appending the newest point again is ignored");

    editor_->append_point(GridPoint { 6, 0 });
    expect(!editor_->close_loop(), "a two-point loop cannot close");
    expect(
        editor_->open_loop() != nullptr && editor_->open_loop()->vertices.size() == 2,
        "a rejected closure stays in drawing state with its points");

    editor_->append_point(GridPoint { 6, 6 });
    editor_->remove_last_point();
    expect(
        editor_->open_loop()->vertices.size() == 2,
        "backspace removes the newest point");

    // Escape cancels without touching the resource or dirty state.
    const bool dirty_before_cancel = editor_->is_dirty();
    editor_->cancel_loop();
    expect(editor_->open_loop() == nullptr, "escape cancels the loop");
    expect(
        editor_->level_resource()->get_region().is_null(),
        "cancelling never commits a region");
    expect(
        editor_->is_dirty() == dirty_before_cancel,
        "cancelling does not alter dirty state");

    // A self-intersecting proposal remains editable and commits nothing.
    editor_->begin_outer_loop();
    editor_->append_point(GridPoint { 0, 0 });
    editor_->append_point(GridPoint { 6, 6 });
    editor_->append_point(GridPoint { 6, 0 });
    editor_->append_point(GridPoint { 0, 6 });
    expect(!editor_->close_loop(), "a self-intersecting outer boundary is rejected");
    if (expect(
            editor_->open_loop() != nullptr,
            "a failed closure keeps the complete open loop")) {
        expect(
            editor_->open_loop()->vertices.size() == 4,
            "a failed closure keeps every stored point");
        expect(
            editor_->open_loop()->failed_closure,
            "a failed closure is recorded against that proposal");
    }
    expect(
        editor_->level_resource()->get_region().is_null(),
        "a failed closure leaves the resource region unchanged");
    expect(editor_->compiled_region() == nullptr, "a failed closure compiles no region");
    expect(
        editor_->status_text().contains("outer boundary is invalid"),
        "the failed closure names the boundary kind and its typed reason");

    editor_->remove_last_point();
    expect(
        editor_->open_loop() != nullptr && !editor_->open_loop()->failed_closure,
        "changing a point clears the stale closure diagnostic");
    editor_->cancel_loop();

    // One valid concave outer loop.
    const std::vector<GridPoint> concave = {
        GridPoint { 0, 0 },
        GridPoint { 8, 0 },
        GridPoint { 8, 8 },
        GridPoint { 4, 8 },
        GridPoint { 4, 4 },
        GridPoint { 0, 4 },
    };
    if (expect(
            draw_boundary(LevelEditor::BoundaryKind::outer_replacement, concave),
            "a valid concave outer boundary commits")) {
        expect(editor_->open_loop() == nullptr, "a successful closure returns to idle");
        expect(editor_->compiled_region() != nullptr, "the committed region compiles");
        expect(
            editor_->compiled_region()->inner_boundaries().empty(),
            "an outer replacement produces a no-hole region");
        expect(editor_->is_dirty(), "a committed region marks the document dirty");

        const godot::Ref<RegionResource> region = editor_->level_resource()->get_region();
        if (expect(region.is_valid(), "the resource owns a RegionResource")) {
            const godot::Ref<PolygonResource> outer = region->get_outer_boundary();
            if (expect(outer.is_valid(), "the resource owns an outer PolygonResource")) {
                const godot::PackedVector2Array vertices = outer->get_vertices();
                expect(
                    vertices.size() == 6,
                    "the stored ring has one vertex per authored point");
                expect(
                    !vertices.is_empty()
                        && vertices[vertices.size() - 1] != vertices[0],
                    "the stored ring never repeats its first vertex");
                bool exact = vertices.size() == 6;
                for (std::int64_t i = 0; exact && i < vertices.size(); ++i) {
                    exact = vertices[i]
                        == godot::Vector2(
                            static_cast<real_t>(concave[static_cast<std::size_t>(i)].x),
                            static_cast<real_t>(concave[static_cast<std::size_t>(i)].y));
                }
                expect(exact, "every authored integer is stored exactly");
            }
        }
    }

    // Negative coordinates round-trip through the compiler exactly.
    const std::vector<GridPoint> negative = {
        GridPoint { -6, -4 },
        GridPoint { 2, -4 },
        GridPoint { 2, 3 },
        GridPoint { -6, 3 },
    };
    if (expect(
            draw_boundary(LevelEditor::BoundaryKind::outer_replacement, negative),
            "a negative-coordinate outer boundary commits")) {
        const Region *region = editor_->compiled_region();
        if (expect(region != nullptr, "the negative region compiles")) {
            bool found_min = false;
            for (const Point &vertex : region->outer_boundary().vertices()) {
                if (vertex == Point { units(-6), units(-4) }) {
                    found_min = true;
                }
            }
            expect(found_min, "a negative authored integer compiles to its exact lattice value");
        }
    }
}

void LevelEditorIntegrationRunner::check_holes() {
    // Start from a known 10 x 10 outer boundary.
    const std::vector<GridPoint> outer = {
        GridPoint { 0, 0 },
        GridPoint { 10, 0 },
        GridPoint { 10, 10 },
        GridPoint { 0, 10 },
    };
    if (!expect(
            draw_boundary(LevelEditor::BoundaryKind::outer_replacement, outer),
            "the hole checks begin from a valid outer boundary")) {
        return;
    }

    const godot::Ref<RegionResource> before = editor_->level_resource()->get_region();
    const godot::Ref<PolygonResource> outer_before = before->get_outer_boundary();

    // A hole outside the outer boundary.
    expect(
        !draw_boundary(
            LevelEditor::BoundaryKind::new_hole,
            { GridPoint { 20, 20 }, GridPoint { 22, 20 }, GridPoint { 22, 22 } }),
        "a hole outside the outer boundary is rejected");
    expect(
        editor_->open_loop() != nullptr && editor_->open_loop()->vertices.size() == 3,
        "a rejected hole keeps its open loop");
    expect(
        editor_->level_resource()->get_region() == before,
        "a rejected hole leaves the accepted RegionResource reference unchanged");
    expect(
        editor_->status_text().contains("hole is invalid"),
        "a rejected hole reports its complete typed reason");
    editor_->cancel_loop();

    // A hole touching the outer boundary.
    expect(
        !draw_boundary(
            LevelEditor::BoundaryKind::new_hole,
            { GridPoint { 0, 2 }, GridPoint { 2, 2 }, GridPoint { 2, 4 },
                GridPoint { 0, 4 } }),
        "a hole touching the outer boundary is rejected");
    editor_->cancel_loop();

    // Two separated valid holes.
    expect(
        draw_boundary(
            LevelEditor::BoundaryKind::new_hole,
            { GridPoint { 2, 2 }, GridPoint { 4, 2 }, GridPoint { 4, 4 },
                GridPoint { 2, 4 } }),
        "a valid hole commits");
    expect(
        draw_boundary(
            LevelEditor::BoundaryKind::new_hole,
            { GridPoint { 6, 6 }, GridPoint { 8, 6 }, GridPoint { 8, 8 },
                GridPoint { 6, 8 } }),
        "a second separated hole commits");

    const Region *region = editor_->compiled_region();
    if (expect(region != nullptr, "the two-hole region compiles")) {
        expect(region->inner_boundaries().size() == 2, "both holes are retained");
        expect(
            region->inner_boundaries()[0].vertices().front() == Point { units(2), units(2) }
                && region->inner_boundaries()[1].vertices().front()
                    == Point { units(6), units(6) },
            "holes are appended after existing holes in authored order");
    }
    const godot::Ref<RegionResource> two_holes = editor_->level_resource()->get_region();
    if (expect(two_holes.is_valid(), "the two-hole region resource exists")) {
        expect(
            two_holes->get_outer_boundary() == outer_before,
            "committing a hole reuses the accepted outer-boundary resource");
        expect(
            two_holes->get_inner_boundaries().size() == 2,
            "the resource stores both holes");
    }

    // A hole overlapping an accepted hole.
    expect(
        !draw_boundary(
            LevelEditor::BoundaryKind::new_hole,
            { GridPoint { 3, 3 }, GridPoint { 5, 3 }, GridPoint { 5, 5 },
                GridPoint { 3, 5 } }),
        "a hole overlapping another hole is rejected");
    expect(
        editor_->level_resource()->get_region() == two_holes,
        "a rejected hole leaves the accepted region reference unchanged");
    expect(
        editor_->compiled_region() != nullptr
            && editor_->compiled_region()->inner_boundaries().size() == 2,
        "a rejected hole leaves the compiled region unchanged");
    editor_->cancel_loop();

    // A hole touching an accepted hole along an edge.
    expect(
        !draw_boundary(
            LevelEditor::BoundaryKind::new_hole,
            { GridPoint { 4, 2 }, GridPoint { 6, 2 }, GridPoint { 6, 4 },
                GridPoint { 4, 4 } }),
        "a hole touching another hole is rejected");
    editor_->cancel_loop();
}

void LevelEditorIntegrationRunner::check_restart() {
    const godot::Ref<RegionResource> before = editor_->level_resource()->get_region();
    if (!expect(
            before.is_valid()
                && before->get_inner_boundaries().size() == 2,
            "restart begins from the accepted two-hole region")) {
        return;
    }

    editor_->begin_outer_loop();
    expect(
        editor_->level_resource()->get_region() == before,
        "starting a replacement does not clear the accepted region");
    expect(
        editor_->compiled_region() != nullptr
            && editor_->compiled_region()->inner_boundaries().size() == 2,
        "the previous valid region stays visible beneath the replacement loop");

    editor_->append_point(GridPoint { 0, 0 });
    editor_->append_point(GridPoint { 3, 0 });
    editor_->cancel_loop();
    expect(
        editor_->level_resource()->get_region() == before
            && editor_->compiled_region() != nullptr
            && editor_->compiled_region()->inner_boundaries().size() == 2,
        "cancelling a replacement restores the old region unchanged");

    expect(
        draw_boundary(
            LevelEditor::BoundaryKind::outer_replacement,
            { GridPoint { 0, 0 }, GridPoint { 12, 0 }, GridPoint { 12, 9 },
                GridPoint { 0, 9 } }),
        "a replacement outer boundary commits");
    expect(
        editor_->compiled_region() != nullptr
            && editor_->compiled_region()->inner_boundaries().empty(),
        "a successful replacement removes the old holes atomically");
    expect(
        editor_->level_resource()->get_region() != before,
        "a successful replacement publishes a new RegionResource");
}

void LevelEditorIntegrationRunner::check_cross_preservation() {
    const godot::Ref<RegionResource> region_before = editor_->level_resource()->get_region();
    const double zoom_before = editor_->pixels_per_unit();
    const godot::Vector2 origin_before = editor_->camera_origin();

    editor_->set_row_included(ROW_TETROMINO_T, true);
    editor_->set_row_color(ROW_TETROMINO_T, godot::Color(0.5f, 0.25f, 0.75f, 1.0f));
    expect(
        editor_->level_resource()->get_region() == region_before,
        "palette edits preserve the accepted region");
    expect(
        editor_->compiled_region() != nullptr,
        "palette edits preserve the compiled region");
    expect(
        editor_->pixels_per_unit() == zoom_before
            && editor_->camera_origin() == origin_before,
        "palette edits preserve the camera");

    // A palette edit while a loop is open leaves that loop alone.
    editor_->begin_hole_loop();
    editor_->append_point(GridPoint { 2, 2 });
    editor_->append_point(GridPoint { 4, 2 });
    editor_->set_row_included(ROW_PENTOMINO_F, true);
    expect(
        editor_->open_loop() != nullptr && editor_->open_loop()->vertices.size() == 2,
        "a palette edit preserves the active open loop");
    editor_->cancel_loop();
    editor_->set_row_included(ROW_PENTOMINO_F, false);

    // Region edits leave every palette row alone.
    const std::vector<LevelEditor::PaletteRow> rows_before = editor_->palette_rows();
    expect(
        draw_boundary(
            LevelEditor::BoundaryKind::new_hole,
            { GridPoint { 3, 3 }, GridPoint { 5, 3 }, GridPoint { 5, 5 },
                GridPoint { 3, 5 } }),
        "a hole commits during the preservation check");
    bool rows_unchanged = editor_->palette_rows().size() == rows_before.size();
    for (std::size_t i = 0; rows_unchanged && i < rows_before.size(); ++i) {
        const LevelEditor::PaletteRow &lhs = rows_before[i];
        const LevelEditor::PaletteRow &rhs = editor_->palette_rows()[i];
        rows_unchanged = lhs.included == rhs.included && lhs.unlimited == rhs.unlimited
            && lhs.finite_amount == rhs.finite_amount && lhs.color == rhs.color;
    }
    expect(rows_unchanged, "region edits preserve every palette row's state");
    expect(
        editor_->compiled_palette() != nullptr
            && editor_->compiled_palette()->order() == 5,
        "region edits preserve the compiled palette");
}

void LevelEditorIntegrationRunner::check_new_level() {
    // The dirty draft is guarded by exactly one ordinary confirmation.
    expect(editor_->is_dirty(), "the draft under test is dirty");
    const godot::Ref<LevelResource> before = editor_->level_resource();
    editor_->on_new_level_pressed();
    expect(
        editor_->pending_action() == LevelEditor::PendingAction::new_level,
        "a dirty draft defers the new-level action to a confirmation");
    expect(
        editor_->level_resource() == before,
        "the deferred action changes nothing yet");

    editor_->on_discard_canceled();
    expect(
        editor_->pending_action() == LevelEditor::PendingAction::none,
        "cancelling clears the pending action");
    expect(
        editor_->level_resource() == before
            && editor_->compiled_region() != nullptr
            && editor_->compiled_palette() != nullptr,
        "cancelling preserves the complete current document");

    editor_->on_new_level_pressed();
    editor_->on_discard_confirmed();
    expect(
        editor_->level_resource() != before,
        "confirming continues the already selected new-level action");
    expect(
        editor_->level_resource().is_valid()
            && editor_->level_resource()->get_region().is_null(),
        "a new level has no region");
    expect(editor_->compiled_palette() == nullptr, "a new level has an empty palette");

    bool defaults_restored = editor_->palette_rows().size() == EXPECTED_ROWS;
    for (std::size_t i = 0; defaults_restored && i < editor_->palette_rows().size(); ++i) {
        const LevelEditor::PaletteRow &row = editor_->palette_rows()[i];
        defaults_restored = !row.included && row.unlimited && row.finite_amount == 1
            && row.color == expected_default_color(i);
    }
    expect(defaults_restored, "a new level restores every row default");
}

// --- persistence ---

void LevelEditorIntegrationRunner::check_save_and_play(
    const content::PrototileCatalog &p_catalog) {
    const godot::String level_path = temporary_path("editor_level.tres");
    const godot::String rejected_path = temporary_path("editor_level.json");

    // Author a complete, colored, two-supply level.
    editor_->set_row_included(ROW_TETROMINO_O, true);
    editor_->set_row_unlimited(ROW_TETROMINO_O, false);
    editor_->set_row_finite_amount(ROW_TETROMINO_O, 4);
    editor_->set_row_color(ROW_TETROMINO_O, godot::Color(0.75f, 0.5f, 0.25f, 1.0f));
    editor_->set_row_included(ROW_TETROMINO_I, true);
    editor_->set_row_color(ROW_TETROMINO_I, godot::Color(0.25f, 0.75f, 0.5f, 1.0f));
    expect(
        draw_boundary(
            LevelEditor::BoundaryKind::outer_replacement,
            { GridPoint { 0, 0 }, GridPoint { 10, 0 }, GridPoint { 10, 8 },
                GridPoint { 0, 8 } }),
        "the persistence check authors a valid outer boundary");
    expect(
        draw_boundary(
            LevelEditor::BoundaryKind::new_hole,
            { GridPoint { 3, 3 }, GridPoint { 5, 3 }, GridPoint { 5, 5 },
                GridPoint { 3, 5 } }),
        "the persistence check authors a valid hole");
    if (!expect(editor_->can_save(), "the authored level is complete")) {
        return;
    }

    // Save and play refuse while a boundary is being drawn.
    editor_->begin_hole_loop();
    expect(
        !editor_->save_document(level_path),
        "save refuses while a boundary is being drawn");
    expect(!editor_->request_play(), "play refuses while a boundary is being drawn");
    expect(
        !godot::FileAccess::file_exists(level_path),
        "a refused save writes nothing");
    editor_->cancel_loop();

    // A deliberately corrupted in-memory palette must be caught by the immediate
    // pre-save gate, not by the saver.
    {
        const godot::Ref<PaletteResource> palette =
            editor_->level_resource()->get_palette();
        const godot::TypedArray<PaletteEntryResource> entries = palette->get_entries();
        const godot::Ref<PaletteEntryResource> first = entries[0];
        const std::int64_t restore = first->get_supply();
        first->set_supply(0);

        expect(
            !editor_->save_document(level_path),
            "an invalid palette is rejected by the pre-save compilation gate");
        expect(
            !godot::FileAccess::file_exists(level_path),
            "a compilation failure prevents the resource saver from being called");
        expect(
            editor_->status_text().contains("cannot save"),
            "the refused save reports its complete typed reason");
        expect(!editor_->request_play(), "an invalid level cannot be played");
        expect(play_emissions_ == 0, "a failed play request emits no signal");

        first->set_supply(restore);
    }

    // Save-as.
    expect(editor_->is_dirty(), "the authored level is dirty before saving");
    if (!expect(editor_->save_document(level_path), "a complete valid level saves")) {
        return;
    }
    expect(
        editor_->level_resource()->get_path() == level_path,
        "a successful save gives the resource its owned path");
    expect(!editor_->is_dirty(), "a successful save marks the document clean");
    expect(
        godot::FileAccess::file_exists(level_path), "the level file was written");

    // Uncached reload through the production loader.
    {
        auto loaded = load_level_resource(level_path, p_catalog);
        if (expect(bool(loaded), "the saved level reloads and compiles uncached")) {
            auto in_memory = compile_level_resource(editor_->level_resource(), p_catalog);
            if (expect(bool(in_memory), "the in-memory level still compiles")) {
                expect(
                    same_palette(in_memory.value().palette(), loaded.value().level.palette()),
                    "the persisted palette compiles equivalently");
                expect(
                    same_region(in_memory.value().region(), loaded.value().level.region()),
                    "the persisted region compiles equivalently");
            }
            const godot::TypedArray<PaletteEntryResource> entries =
                loaded.value().resource->get_palette()->get_entries();
            if (expect(entries.size() == 2, "the persisted palette keeps both entries")) {
                const godot::Ref<PaletteEntryResource> first = entries[0];
                const godot::Ref<PaletteEntryResource> second = entries[1];
                expect(
                    first.is_valid() && first->get_prototile_id() == 1
                        && first->get_supply() == 4
                        && first->get_color() == godot::Color(0.75f, 0.5f, 0.25f, 1.0f),
                    "the first persisted entry keeps its id, finite supply, and color");
                expect(
                    second.is_valid() && second->get_prototile_id() == 2
                        && second->get_supply() == -1
                        && second->get_color() == godot::Color(0.25f, 0.75f, 0.5f, 1.0f),
                    "the second persisted entry keeps its id, unlimited supply, and color");
            }
            expect(
                loaded.value().resource->get_region()->get_inner_boundaries().size() == 1,
                "the persisted region keeps its hole");
        }
    }

    // A failing save preserves the draft and its dirty state.
    editor_->set_row_color(ROW_TETROMINO_I, godot::Color(0.1f, 0.2f, 0.3f, 1.0f));
    expect(editor_->is_dirty(), "a palette edit after saving marks the draft dirty");
    expect(
        !editor_->save_document(rejected_path),
        "saving to an unsupported extension fails");
    expect(editor_->is_dirty(), "a failed save preserves dirty state");
    expect(
        !godot::FileAccess::file_exists(rejected_path),
        "a rejected extension writes nothing");

    // A pathless save overwrites the owned path.
    if (expect(editor_->save_document(godot::String()), "a pathless save succeeds")) {
        expect(
            editor_->level_resource()->get_path() == level_path,
            "a pathless save keeps the owned path");
        expect(!editor_->is_dirty(), "the re-saved document is clean");
    }

    // Play emits exactly once, with the pointer-identical current resource, and
    // saves nothing.
    const godot::Ref<LevelResource> current = editor_->level_resource();
    const bool dirty_before_play = editor_->is_dirty();
    if (expect(editor_->request_play(), "a complete valid level can be played")) {
        expect(play_emissions_ == 1, "a successful play request emits exactly once");
        expect(
            last_played_ == current,
            "the play request carries the pointer-identical current resource");
        expect(
            editor_->is_dirty() == dirty_before_play,
            "a play request does not change dirty state");
    }
}

void LevelEditorIntegrationRunner::check_open_drafts(
    const content::PrototileCatalog &p_catalog) {
    const godot::String level_path = temporary_path("editor_level.tres");
    const godot::String palette_only_path = temporary_path("palette_only.tres");
    const godot::String null_palette_path = temporary_path("null_palette.tres");
    const godot::String empty_palette_path = temporary_path("empty_palette.tres");
    const godot::String bad_palette_path = temporary_path("bad_palette.tres");
    const godot::String bad_region_path = temporary_path("bad_region.tres");
    const godot::String wrong_type_path = temporary_path("wrong_type.tres");
    const godot::String external_palette_path = temporary_path("external_palette.tres");
    const godot::String external_level_path = temporary_path("external_level.tres");

    const godot::Color authored(0.5f, 0.25f, 0.125f, 1.0f);

    // Fixtures, built through the ordinary resource graph and the production
    // saver. None of this is a second editor save path.
    {
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(3, 7, authored));
        save_level_resource(
            make_level(make_palette(entries), godot::Ref<RegionResource>()),
            palette_only_path);
    }
    save_level_resource(
        make_level(godot::Ref<PaletteResource>(), make_unit_square_region()),
        null_palette_path);
    save_level_resource(
        make_level(
            make_palette(godot::TypedArray<PaletteEntryResource>()),
            make_unit_square_region()),
        empty_palette_path);
    {
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(999, -1, authored));
        save_level_resource(
            make_level(make_palette(entries), make_unit_square_region()),
            bad_palette_path);
    }
    {
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(1, -1, authored));
        godot::TypedArray<PolygonResource> holes;
        holes.push_back(make_polygon({
            godot::Vector2(20.0f, 20.0f),
            godot::Vector2(22.0f, 20.0f),
            godot::Vector2(22.0f, 22.0f),
        }));
        save_level_resource(
            make_level(
                make_palette(entries),
                make_region(
                    make_polygon({
                        godot::Vector2(0.0f, 0.0f),
                        godot::Vector2(6.0f, 0.0f),
                        godot::Vector2(6.0f, 6.0f),
                        godot::Vector2(0.0f, 6.0f),
                    }),
                    holes)),
            bad_region_path);
    }
    {
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(1, -1, authored));
        godot::ResourceSaver::get_singleton()->save(
            make_palette(entries),
            wrong_type_path,
            godot::BitField<godot::ResourceSaver::SaverFlags>(
                godot::ResourceSaver::FLAG_CHANGE_PATH));
    }
    {
        // An externally pathed palette, referenced by a level, so the first
        // palette edit after opening can be observed not to touch it.
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(1, 2, authored));
        godot::Ref<PaletteResource> palette = make_palette(entries);
        godot::ResourceSaver::get_singleton()->save(
            palette,
            external_palette_path,
            godot::BitField<godot::ResourceSaver::SaverFlags>(
                godot::ResourceSaver::FLAG_CHANGE_PATH));
        palette->take_over_path(external_palette_path);
        save_level_resource(
            make_level(palette, make_unit_square_region()), external_level_path);
    }

    // A complete valid level opens, is clean, and frames its region.
    {
        const double zoom_before = editor_->pixels_per_unit();
        if (expect(editor_->open_draft(level_path), "a complete valid level opens")) {
            expect(!editor_->is_dirty(), "an opened valid draft is not dirty");
            expect(
                editor_->level_resource().is_valid()
                    && editor_->level_resource()->get_path() == level_path,
                "the opened resource retains its owned path");
            expect(
                editor_->compiled_region() != nullptr
                    && editor_->compiled_region()->inner_boundaries().size() == 1,
                "the opened region compiles with its hole");
            expect(editor_->open_loop() == nullptr, "a valid region opens in idle state");
            expect(editor_->can_save(), "an opened complete level can be saved and played");

            bool rows_restored = true;
            const std::vector<LevelEditor::PaletteRow> &rows = editor_->palette_rows();
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const bool should_include =
                    i == ROW_TETROMINO_O || i == ROW_TETROMINO_I;
                if (rows[i].included != should_include) {
                    rows_restored = false;
                }
            }
            expect(rows_restored, "opening maps the palette onto exactly its catalog rows");
            expect(
                !rows[ROW_TETROMINO_O].unlimited
                    && rows[ROW_TETROMINO_O].finite_amount == 4
                    && rows[ROW_TETROMINO_O].color
                        == godot::Color(0.75f, 0.5f, 0.25f, 1.0f),
                "an opened finite entry restores its supply and color");
            expect(
                rows[ROW_TETROMINO_I].unlimited
                    && rows[ROW_TETROMINO_I].color == godot::Color(0.1f, 0.2f, 0.3f, 1.0f),
                "an opened unlimited entry restores its supply and color");
            expect(
                rows[ROW_TETROMINO_T].included == false
                    && rows[ROW_TETROMINO_T].finite_amount == 1
                    && rows[ROW_TETROMINO_T].color == expected_default_color(ROW_TETROMINO_T),
                "rows the opened palette does not list keep their defaults");
            expect(
                editor_->pixels_per_unit() != zoom_before
                    || editor_->pixels_per_unit() > 0.0,
                "opening a valid region sets a usable camera scale");
            expect(
                editor_->pixels_per_unit() >= 4.0 && editor_->pixels_per_unit() <= 128.0,
                "the framing scale stays inside the ordinary zoom bounds");

            godot::CheckBox *include = editor_->row_include_control(ROW_TETROMINO_O);
            godot::SpinBox *amount = editor_->row_amount_control(ROW_TETROMINO_O);
            godot::ColorPickerButton *color = editor_->row_color_control(ROW_TETROMINO_O);
            expect(
                include != nullptr && include->is_pressed() && amount != nullptr
                    && amount->get_value() == 4.0 && color != nullptr
                    && color->get_pick_color() == godot::Color(0.75f, 0.5f, 0.25f, 1.0f),
                "the visible row controls show the opened values");
        }
    }

    // A valid palette with no region.
    if (expect(
            editor_->open_draft(palette_only_path),
            "a level with a valid palette and no region opens")) {
        expect(
            editor_->compiled_palette() != nullptr
                && editor_->compiled_palette()->order() == 1,
            "the palette-only draft compiles its palette");
        expect(editor_->compiled_region() == nullptr, "the palette-only draft has no region");
        expect(
            editor_->open_loop() != nullptr
                && editor_->open_loop()->kind
                    == LevelEditor::BoundaryKind::outer_replacement,
            "a missing region begins an outer loop immediately");
        expect(
            editor_->palette_rows()[ROW_TETROMINO_T].included
                && !editor_->palette_rows()[ROW_TETROMINO_T].unlimited
                && editor_->palette_rows()[ROW_TETROMINO_T].finite_amount == 7,
            "the palette-only draft maps its entry onto the right row");
        expect(!editor_->is_dirty(), "an intact opened draft is not dirty");
        editor_->cancel_loop();
    }

    // An empty palette is an editable incomplete draft.
    if (expect(
            editor_->open_draft(empty_palette_path),
            "a level with an empty palette opens as an editable draft")) {
        expect(
            editor_->compiled_palette() == nullptr,
            "an empty opened palette produces no compiled palette");
        expect(
            editor_->compiled_region() != nullptr,
            "an empty-palette draft still compiles its region");
        expect(!editor_->can_save(), "an empty-palette draft cannot be saved");
        expect(!editor_->is_dirty(), "an intact empty-palette draft is not dirty");
    }

    // A null palette is repaired into an empty one, which is a graph change.
    if (expect(
            editor_->open_draft(null_palette_path),
            "a level with a null palette opens")) {
        expect(
            editor_->level_resource()->get_palette().is_valid()
                && editor_->level_resource()->get_palette()->get_entries().is_empty(),
            "a null palette is repaired into an empty embedded palette");
        expect(editor_->is_dirty(), "a repaired draft is marked dirty");
    }

    // Rejections preserve the complete current document.
    {
        const godot::Ref<LevelResource> before = editor_->level_resource();
        const bool dirty_before = editor_->is_dirty();

        expect(!editor_->open_draft(bad_palette_path), "an unknown palette id is rejected");
        expect(
            editor_->status_text().contains("not in the catalog"),
            "the rejected palette reports its complete typed reason");
        expect(!editor_->open_draft(bad_region_path), "an invalid non-null region is rejected");
        expect(
            editor_->status_text().contains("is not strictly inside"),
            "the rejected region reports its complete typed reason");
        expect(!editor_->open_draft(wrong_type_path), "a non-level resource is rejected");
        expect(
            !editor_->open_draft(temporary_path("never_written.tres")),
            "a path that was never written is rejected");
        expect(!editor_->open_draft(godot::String()), "an empty path is rejected");

        expect(
            editor_->level_resource() == before && editor_->is_dirty() == dirty_before,
            "every rejected open preserves the current document exactly");
    }

    // The first palette edit after opening an externally pathed palette publishes
    // a new embedded palette and leaves the external resource alone.
    if (expect(
            editor_->open_draft(external_level_path),
            "a level referencing an externally pathed palette opens")) {
        const godot::Ref<PaletteResource> external =
            editor_->level_resource()->get_palette();
        expect(
            external.is_valid() && !external->get_path().is_empty(),
            "the opened palette is the externally pathed resource");

        editor_->set_row_included(ROW_DOMINO, true);
        const godot::Ref<PaletteResource> published =
            editor_->level_resource()->get_palette();
        expect(
            published != external,
            "the first palette edit publishes a new embedded palette");
        expect(
            published.is_valid() && published->get_path().is_empty(),
            "the published palette has no independent resource path");
        expect(
            external.is_valid() && external->get_entries().size() == 1,
            "the external palette resource is not mutated");

        auto reloaded = godot::ResourceLoader::get_singleton()->load(
            external_palette_path,
            "PaletteResource",
            godot::ResourceLoader::CACHE_MODE_IGNORE_DEEP);
        const godot::Ref<PaletteResource> on_disk =
            godot::Object::cast_to<PaletteResource>(reloaded.ptr());
        expect(
            on_disk.is_valid() && on_disk->get_entries().size() == 1,
            "the external palette file is unchanged on disk");
    }

    // Compiling the reopened level once more, so the open path is proven to
    // produce a resource the production compiler still accepts.
    {
        auto compiled = compile_level_resource(editor_->level_resource(), p_catalog);
        expect(bool(compiled), "the edited opened level still compiles");
    }

    temporary_paths_ = {
        level_path,
        palette_only_path,
        null_palette_path,
        empty_palette_path,
        bad_palette_path,
        bad_region_path,
        wrong_type_path,
        external_palette_path,
        external_level_path,
    };
}

void LevelEditorIntegrationRunner::check_absent_external_palette_surface() {
    expect(
        editor_->get_node_or_null(godot::NodePath("PaletteDialog")) == nullptr,
        "no external palette file dialog exists");
    expect(
        editor_->action_button("NewFromPaletteButton") == nullptr,
        "no “new from palette” action exists");
    expect(
        editor_->get_node_or_null(
            godot::NodePath("StatusBar/Margin/Body/PaletteSummaryLabel"))
            == nullptr,
        "no read-only palette summary label exists");
    expect(
        editor_->get_node_or_null(
            godot::NodePath("PalettePanel/Margin/Body/PaletteScroll/PaletteRows/Row0"))
            != nullptr,
        "the palette panel really owns its generated rows");
}

// --- cleanup ---

void LevelEditorIntegrationRunner::remove_temporary(const godot::String &p_path) {
    if (godot::FileAccess::file_exists(p_path)) {
        godot::DirAccess::remove_absolute(p_path);
    }
}

void LevelEditorIntegrationRunner::check_temporary_files_removed() {
    for (const godot::String &path : temporary_paths_) {
        remove_temporary(path);
    }

    bool all_removed = true;
    for (const godot::String &path : temporary_paths_) {
        if (godot::FileAccess::file_exists(path)) {
            all_removed = false;
        }
    }
    expect(all_removed, "every temporary file this run created was removed");

    const godot::String directory(TEMPORARY_DIRECTORY);
    if (godot::DirAccess::dir_exists_absolute(directory)
        && godot::DirAccess::get_files_at(directory).is_empty()
        && godot::DirAccess::get_directories_at(directory).is_empty()) {
        godot::DirAccess::remove_absolute(directory);
    }
}

// --- exact comparison ---

bool LevelEditorIntegrationRunner::same_palette(
    const engine::Palette &p_lhs, const engine::Palette &p_rhs) {
    if (p_lhs.entries().size() != p_rhs.entries().size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_lhs.entries().size(); ++i) {
        const engine::PaletteEntry &lhs = p_lhs.entries()[i];
        const engine::PaletteEntry &rhs = p_rhs.entries()[i];
        if (lhs.prototile().id() != rhs.prototile().id() || lhs.supply() != rhs.supply()) {
            return false;
        }
        if (!same_boundary(lhs.prototile().polygon(), rhs.prototile().polygon())) {
            return false;
        }
        if (lhs.orientations().size() != rhs.orientations().size()) {
            return false;
        }
        for (std::size_t j = 0; j < lhs.orientations().size(); ++j) {
            if (!same_boundary(
                    lhs.orientations()[j].canonical_polygon(),
                    rhs.orientations()[j].canonical_polygon())) {
                return false;
            }
        }
    }
    return true;
}

bool LevelEditorIntegrationRunner::same_region(
    const Region &p_lhs, const Region &p_rhs) {
    if (!same_boundary(p_lhs.outer_boundary(), p_rhs.outer_boundary())) {
        return false;
    }
    if (p_lhs.inner_boundaries().size() != p_rhs.inner_boundaries().size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_lhs.inner_boundaries().size(); ++i) {
        if (!same_boundary(p_lhs.inner_boundaries()[i], p_rhs.inner_boundaries()[i])) {
            return false;
        }
    }
    return p_lhs.doubled_area() == p_rhs.doubled_area();
}

// --- entry point ---

void LevelEditorIntegrationRunner::_ready() {
    auto catalog = content::make_canonical_prototile_catalog();
    if (!expect(bool(catalog), "the canonical catalog constructs")) {
        if (get_tree() != nullptr) {
            get_tree()->quit(1);
        }
        return;
    }

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
    editor_->connect(
        godot::StringName("play_requested"),
        godot::Callable(this, godot::StringName("on_play_requested")));

    const godot::Error made = godot::DirAccess::make_dir_recursive_absolute(
        godot::String(TEMPORARY_DIRECTORY));
    expect(
        made == godot::OK || made == godot::ERR_ALREADY_EXISTS,
        "the narrow temporary directory is available");

    check_startup();
    check_palette_rows();
    check_palette_publication();
    check_region_authoring();
    check_holes();
    check_restart();
    check_cross_preservation();
    check_new_level();
    check_save_and_play(catalog.value());
    check_open_drafts(catalog.value());
    check_absent_external_palette_surface();
    check_temporary_files_removed();

    if (failures_ == 0) {
        godot::UtilityFunctions::print(
            "[tiles] level editor integration: ", number(static_cast<std::int64_t>(checks_)),
            " checks passed");
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
