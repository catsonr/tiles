#include "game/LevelEditor.h"

#include "content/PrototileCatalog.h"
#include "core/Prototile.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "core/geometry/Triangle.h"
#include "game/PrototilePreview.h"
#include "game/resources/LevelPersistence.h"
#include "game/resources/ResourceCompiler.h"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace tiles::game {

namespace {

// --- camera and grid constants ---
//
// Recorded here rather than derived, so the implementation notes can state
// exactly what the authoring surface uses.

constexpr double INITIAL_PIXELS_PER_UNIT = 32.0;
constexpr double MIN_PIXELS_PER_UNIT = 4.0;
constexpr double MAX_PIXELS_PER_UNIT = 128.0;
constexpr double ZOOM_STEP = 1.25;

// Framing an opened region leaves this much empty canvas on every side.
constexpr double FRAME_MARGIN_PIXELS = 48.0;

// The whole-game-unit range representable on the q16.48 lattice: raw values fill
// a signed 64-bit integer read with 48 fractional bits, so the value range is
// [-32768, 32768) and the whole values in it are these.
constexpr std::int64_t MIN_GAME_UNIT = -32768;
constexpr std::int64_t MAX_GAME_UNIT = 32767;

// The editor's finite-supply control bounds. This is an authoring convenience,
// not a resource or engine invariant: a resource authored elsewhere may still
// encode any positive supply the compiler admits.
constexpr std::int64_t MIN_FINITE_SUPPLY = 1;
constexpr std::int64_t MAX_FINITE_SUPPLY = 1000000;

// Deterministic default row colors. The hue advances by the golden-ratio
// conjugate so consecutive catalog positions never land on neighbouring hues,
// and no authored table is needed. Canonical content owns no color; this is
// editor presentation which the author may replace per level.
constexpr double DEFAULT_HUE_STEP = 0.6180339887498948;
constexpr float DEFAULT_SATURATION = 0.65f;
constexpr float DEFAULT_VALUE = 0.95f;

// --- canvas palette ---

const godot::Color CANVAS_BACKGROUND(0.09f, 0.10f, 0.12f, 1.0f);
const godot::Color GRID_LINE(0.18f, 0.19f, 0.22f, 1.0f);
const godot::Color GRID_AXIS(0.34f, 0.36f, 0.42f, 1.0f);
const godot::Color REGION_FILL(0.30f, 0.46f, 0.62f, 0.38f);
const godot::Color REGION_OUTLINE(0.62f, 0.80f, 0.98f, 1.0f);
const godot::Color HOLE_OUTLINE(0.98f, 0.66f, 0.42f, 1.0f);
const godot::Color OUTER_LOOP(0.62f, 0.94f, 0.68f, 1.0f);
const godot::Color HOLE_LOOP(0.98f, 0.74f, 0.36f, 1.0f);
const godot::Color LOOP_INVALID(0.98f, 0.40f, 0.42f, 1.0f);
const godot::Color LOOP_PREVIEW(0.72f, 0.74f, 0.80f, 0.75f);
const godot::Color CLOSURE_MARKER(1.0f, 1.0f, 1.0f, 1.0f);
const godot::Color CURSOR_MARKER(0.92f, 0.94f, 1.0f, 1.0f);
const godot::Color CURSOR_REJECTED(0.98f, 0.30f, 0.32f, 1.0f);

constexpr float GRID_WIDTH = 1.0f;
constexpr float AXIS_WIDTH = 1.0f;
constexpr float BOUNDARY_WIDTH = 2.0f;
constexpr float LOOP_WIDTH = 2.0f;
constexpr float POINT_RADIUS = 3.5f;
constexpr float CLOSURE_RADIUS = 6.0f;
constexpr float CURSOR_RADIUS = 4.5f;
constexpr float REJECTED_ARM = 6.0f;

// --- scene paths ---

const char *TOOLBAR_PATH = "Toolbar";
const char *ACTIONS_PATH = "Toolbar/Margin/Actions";
const char *PATH_LABEL_PATH = "Toolbar/Margin/Actions/PathLabel";
const char *PALETTE_PANEL_PATH = "PalettePanel";
const char *PALETTE_ROWS_PATH = "PalettePanel/Margin/Body/PaletteScroll/PaletteRows";
const char *STATUS_BAR_PATH = "StatusBar";
const char *INSTRUCTION_PATH = "StatusBar/Margin/Body/InstructionLabel";
const char *STATUS_PATH = "StatusBar/Margin/Body/Line/StatusLabel";
const char *COORDINATE_PATH = "StatusBar/Margin/Body/Line/CoordinateLabel";
const char *OPEN_DIALOG_PATH = "OpenDialog";
const char *SAVE_DIALOG_PATH = "SaveDialog";
const char *DISCARD_DIALOG_PATH = "DiscardDialog";

const char *NEW_LEVEL_BUTTON = "NewLevelButton";
const char *OPEN_DRAFT_BUTTON = "OpenDraftButton";
const char *RESTART_REGION_BUTTON = "RestartRegionButton";
const char *ADD_HOLE_BUTTON = "AddHoleButton";
const char *SAVE_BUTTON = "SaveButton";
const char *SAVE_AS_BUTTON = "SaveAsButton";
const char *PLAY_BUTTON = "PlayButton";

// The two supply modes, in the order they are added to every row.
constexpr int SUPPLY_ITEM_UNLIMITED = 0;
constexpr int SUPPLY_ITEM_FINITE = 1;

double to_real(Coordinate p_coordinate) {
    return static_cast<double>(p_coordinate.raw()) / static_cast<double>(Coordinate::SCALE);
}

// Round to the nearest integer with exact ties away from zero: the same tie
// policy the q16.48 quantizer documents, so the editor never disagrees with the
// lattice about which way a half lands.
double round_half_away_from_zero(double p_value) {
    return p_value < 0.0 ? std::ceil(p_value - 0.5) : std::floor(p_value + 0.5);
}

godot::Color opaque(const godot::Color &p_color) {
    return godot::Color(p_color.r, p_color.g, p_color.b, 1.0f);
}

godot::Color default_row_color(std::size_t p_catalog_index) {
    const double hue = std::fmod(
        static_cast<double>(p_catalog_index + 1) * DEFAULT_HUE_STEP, 1.0);
    return godot::Color::from_hsv(
        static_cast<float>(hue), DEFAULT_SATURATION, DEFAULT_VALUE, 1.0f);
}

godot::String number(std::int64_t p_value) {
    return godot::String::num_int64(p_value);
}

godot::String number(std::size_t p_value) {
    return godot::String::num_int64(static_cast<std::int64_t>(p_value));
}

// --- typed error presentation ---
//
// Complete typed compiler errors are turned into text here, at the presentation
// boundary, and nowhere else. Nothing produced here flows back down, and no
// compiler flattens its own alternatives.

godot::String describe(QuantizationError p_error) {
    switch (p_error) {
        case QuantizationError::non_finite:
            return "the value is not finite";
        case QuantizationError::out_of_range:
            return "the value leaves the representable coordinate range";
    }
    return "unknown quantization failure";
}

godot::String describe(PolygonError p_error) {
    switch (p_error) {
        case PolygonError::too_few_vertices:
            return "fewer than three vertices";
        case PolygonError::repeated_closing_vertex:
            return "the first vertex is repeated at the end";
        case PolygonError::zero_length_edge:
            return "a zero-length edge";
        case PolygonError::repeated_vertex:
            return "a repeated vertex";
        case PolygonError::redundant_collinear_vertex:
            return "a redundant vertex inside a straight edge";
        case PolygonError::adjacent_edge_overlap:
            return "adjacent edges overlap";
        case PolygonError::self_intersection:
            return "the boundary crosses itself";
        case PolygonError::zero_area:
            return "the boundary encloses no area";
        case PolygonError::triangulation_failed:
            return "the interior could not be triangulated";
    }
    return "unknown polygon failure";
}

godot::String describe(CoordinateAxis p_axis) {
    return p_axis == CoordinateAxis::x ? "x" : "y";
}

godot::String describe(const PolygonResourceError &p_error) {
    switch (p_error.code) {
        case PolygonResourceErrorCode::missing_resource:
            return "the boundary is missing";
        case PolygonResourceErrorCode::coordinate_quantization_failed: {
            godot::String text = "vertex ";
            if (p_error.vertex.has_value()) {
                text += number(p_error.vertex.value());
            } else {
                text += "?";
            }
            if (p_error.axis.has_value()) {
                text += " " + describe(p_error.axis.value());
            }
            if (p_error.quantization_error.has_value()) {
                text += ": " + describe(p_error.quantization_error.value());
            }
            return text;
        }
        case PolygonResourceErrorCode::polygon_construction_failed:
            return p_error.polygon_error.has_value()
                ? describe(p_error.polygon_error.value())
                : godot::String("the boundary is not a simple polygon");
    }
    return "unknown boundary failure";
}

godot::String describe(RegionErrorCode p_code) {
    switch (p_code) {
        case RegionErrorCode::hole_not_strictly_inside_outer:
            return "it is not strictly inside the outer boundary";
        case RegionErrorCode::holes_not_disjoint:
            return "it touches or overlaps another hole";
    }
    return "unknown region relationship failure";
}

godot::String describe(const RegionResourceError &p_error) {
    switch (p_error.code) {
        case RegionResourceErrorCode::missing_resource:
            return "the region is missing";
        case RegionResourceErrorCode::outer_boundary_invalid:
            return "outer boundary is invalid: "
                + (p_error.polygon_error.has_value()
                        ? describe(p_error.polygon_error.value())
                        : godot::String("unknown boundary failure"));
        case RegionResourceErrorCode::inner_boundary_invalid: {
            godot::String text = "hole ";
            text += p_error.hole.has_value() ? number(p_error.hole.value())
                                             : godot::String("?");
            text += " is invalid: ";
            text += p_error.polygon_error.has_value()
                ? describe(p_error.polygon_error.value())
                : godot::String("unknown boundary failure");
            return text;
        }
        case RegionResourceErrorCode::region_construction_failed: {
            if (!p_error.region_error.has_value()) {
                return "the region relationships are invalid";
            }
            const RegionError &region_error = p_error.region_error.value();
            godot::String text = "hole " + number(region_error.first_hole);
            if (region_error.second_hole.has_value()) {
                text += " and hole " + number(region_error.second_hole.value());
            }
            text += " are invalid: " + describe(region_error.code);
            return text;
        }
    }
    return "unknown region failure";
}

godot::String describe(engine::PaletteError p_error) {
    switch (p_error) {
        case engine::PaletteError::empty:
            return "the palette has no entries";
        case engine::PaletteError::duplicate_prototile_id:
            return "two entries name the same prototile";
    }
    return "unknown palette failure";
}

godot::String describe(const LatticeOrientationError &p_error) {
    switch (p_error.code) {
        case LatticeOrientationErrorCode::empty_orientation_set:
            return "no orientations were requested";
        case LatticeOrientationErrorCode::unsupported_orientation:
            return "an unsupported orientation was requested";
        case LatticeOrientationErrorCode::coordinate_overflow:
            return "rotating it overflowed the coordinate range";
        case LatticeOrientationErrorCode::polygon_construction_failed:
            return p_error.polygon_error.has_value()
                ? describe(p_error.polygon_error.value())
                : godot::String("a rotated boundary was invalid");
        case LatticeOrientationErrorCode::normalization_overflow:
            return "normalizing a rotated boundary overflowed";
    }
    return "unknown orientation failure";
}

godot::String describe(const PaletteResourceError &p_error) {
    godot::String prefix;
    if (p_error.entry.has_value()) {
        prefix = "palette entry " + number(p_error.entry.value()) + " ";
    }
    switch (p_error.code) {
        case PaletteResourceErrorCode::missing_resource:
            return "the palette is missing";
        case PaletteResourceErrorCode::missing_entry:
            return prefix + "is missing";
        case PaletteResourceErrorCode::negative_prototile_id:
            return prefix + "has negative prototile id "
                + (p_error.encoded_prototile_id.has_value()
                        ? number(p_error.encoded_prototile_id.value())
                        : godot::String("?"));
        case PaletteResourceErrorCode::unknown_prototile_id:
            return prefix + "names prototile id "
                + (p_error.encoded_prototile_id.has_value()
                        ? number(p_error.encoded_prototile_id.value())
                        : godot::String("?"))
                + ", which is not in the catalog";
        case PaletteResourceErrorCode::invalid_supply:
            return prefix + "has supply "
                + (p_error.encoded_supply.has_value()
                        ? number(p_error.encoded_supply.value())
                        : godot::String("?"))
                + ", which is neither -1 nor positive";
        case PaletteResourceErrorCode::orientation_compilation_failed:
            return prefix + "failed orientation compilation: "
                + (p_error.orientation_error.has_value()
                        ? describe(p_error.orientation_error.value())
                        : godot::String("unknown orientation failure"));
        case PaletteResourceErrorCode::palette_construction_failed:
            return p_error.palette_error.has_value()
                ? describe(p_error.palette_error.value())
                : godot::String("the palette is invalid");
    }
    return "unknown palette failure";
}

godot::String describe(const LevelResourceError &p_error) {
    switch (p_error.code) {
        case LevelResourceErrorCode::missing_resource:
            return "there is no level to compile";
        case LevelResourceErrorCode::palette_invalid:
            return "palette is invalid: "
                + (p_error.palette_error.has_value()
                        ? describe(p_error.palette_error.value())
                        : godot::String("unknown palette failure"));
        case LevelResourceErrorCode::region_invalid:
            return "region is invalid: "
                + (p_error.region_error.has_value()
                        ? describe(p_error.region_error.value())
                        : godot::String("unknown region failure"));
    }
    return "unknown level failure";
}

godot::String describe(const SaveLevelError &p_error) {
    switch (p_error.code) {
        case SaveLevelErrorCode::missing_resource:
            return "there is no level to save";
        case SaveLevelErrorCode::path_required:
            return "no save path was given";
        case SaveLevelErrorCode::unsupported_extension:
            return "only .tres and .res paths can be saved";
        case SaveLevelErrorCode::saver_failed:
            return "the resource saver failed with engine error "
                + number(static_cast<std::int64_t>(p_error.godot_error));
    }
    return "unknown save failure";
}

} // namespace

// --- registration ---

void LevelEditor::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_new_level_pressed"), &LevelEditor::on_new_level_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_open_draft_pressed"), &LevelEditor::on_open_draft_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_restart_region_pressed"),
        &LevelEditor::on_restart_region_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_add_hole_pressed"), &LevelEditor::on_add_hole_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_save_pressed"), &LevelEditor::on_save_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_save_as_pressed"), &LevelEditor::on_save_as_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_play_pressed"), &LevelEditor::on_play_pressed);

    godot::ClassDB::bind_method(
        godot::D_METHOD("on_open_file_selected", "path"),
        &LevelEditor::on_open_file_selected);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_save_file_selected", "path"),
        &LevelEditor::on_save_file_selected);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_discard_confirmed"), &LevelEditor::on_discard_confirmed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_discard_canceled"), &LevelEditor::on_discard_canceled);

    godot::ClassDB::bind_method(
        godot::D_METHOD("on_row_included_toggled", "pressed", "row"),
        &LevelEditor::on_row_included_toggled);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_row_supply_selected", "index", "row"),
        &LevelEditor::on_row_supply_selected);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_row_amount_changed", "amount", "row"),
        &LevelEditor::on_row_amount_changed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_row_color_changed", "color", "row"),
        &LevelEditor::on_row_color_changed);

    // The typed play request. Its argument carries the exact current in-memory
    // resource, whether or not that resource has ever been saved.
    ADD_SIGNAL(godot::MethodInfo(
        "play_requested",
        godot::PropertyInfo(
            godot::Variant::OBJECT,
            "level",
            godot::PROPERTY_HINT_RESOURCE_TYPE,
            "LevelResource")));
}

// --- initialization ---

bool LevelEditor::bind_scene() {
    godot::String missing;
    const auto require = [&](const char *p_path) -> godot::Node * {
        godot::Node *node = get_node_or_null(godot::NodePath(p_path));
        if (node == nullptr) {
            missing += missing.is_empty() ? godot::String() : godot::String(", ");
            missing += p_path;
        }
        return node;
    };
    const auto require_action = [&](const char *p_name) -> godot::Button * {
        const godot::String path = godot::String(ACTIONS_PATH) + "/" + p_name;
        godot::Node *node = get_node_or_null(godot::NodePath(path));
        godot::Button *button = godot::Object::cast_to<godot::Button>(node);
        if (button == nullptr) {
            missing += missing.is_empty() ? godot::String() : godot::String(", ");
            missing += path;
        }
        return button;
    };

    toolbar_ = godot::Object::cast_to<godot::Control>(require(TOOLBAR_PATH));
    palette_panel_ = godot::Object::cast_to<godot::Control>(require(PALETTE_PANEL_PATH));
    status_bar_ = godot::Object::cast_to<godot::Control>(require(STATUS_BAR_PATH));
    rows_container_ =
        godot::Object::cast_to<godot::VBoxContainer>(require(PALETTE_ROWS_PATH));
    instruction_label_ = godot::Object::cast_to<godot::Label>(require(INSTRUCTION_PATH));
    status_label_ = godot::Object::cast_to<godot::Label>(require(STATUS_PATH));
    coordinate_label_ = godot::Object::cast_to<godot::Label>(require(COORDINATE_PATH));
    path_label_ = godot::Object::cast_to<godot::Label>(require(PATH_LABEL_PATH));
    open_dialog_ = godot::Object::cast_to<godot::FileDialog>(require(OPEN_DIALOG_PATH));
    save_dialog_ = godot::Object::cast_to<godot::FileDialog>(require(SAVE_DIALOG_PATH));
    discard_dialog_ =
        godot::Object::cast_to<godot::ConfirmationDialog>(require(DISCARD_DIALOG_PATH));

    new_level_button_ = require_action(NEW_LEVEL_BUTTON);
    open_draft_button_ = require_action(OPEN_DRAFT_BUTTON);
    restart_region_button_ = require_action(RESTART_REGION_BUTTON);
    add_hole_button_ = require_action(ADD_HOLE_BUTTON);
    save_button_ = require_action(SAVE_BUTTON);
    save_as_button_ = require_action(SAVE_AS_BUTTON);
    play_button_ = require_action(PLAY_BUTTON);

    // Every required child is reported once, together, rather than as a cascade
    // of null dereferences.
    if (!missing.is_empty()) {
        godot::UtilityFunctions::push_error(
            "[tiles] level editor initialization failed: missing or mistyped scene "
            "children: ",
            missing);
        return false;
    }

    const godot::Callable pressed_new(this, godot::StringName("on_new_level_pressed"));
    new_level_button_->connect(godot::StringName("pressed"), pressed_new);
    open_draft_button_->connect(
        godot::StringName("pressed"),
        godot::Callable(this, godot::StringName("on_open_draft_pressed")));
    restart_region_button_->connect(
        godot::StringName("pressed"),
        godot::Callable(this, godot::StringName("on_restart_region_pressed")));
    add_hole_button_->connect(
        godot::StringName("pressed"),
        godot::Callable(this, godot::StringName("on_add_hole_pressed")));
    save_button_->connect(
        godot::StringName("pressed"),
        godot::Callable(this, godot::StringName("on_save_pressed")));
    save_as_button_->connect(
        godot::StringName("pressed"),
        godot::Callable(this, godot::StringName("on_save_as_pressed")));
    play_button_->connect(
        godot::StringName("pressed"),
        godot::Callable(this, godot::StringName("on_play_pressed")));

    open_dialog_->connect(
        godot::StringName("file_selected"),
        godot::Callable(this, godot::StringName("on_open_file_selected")));
    save_dialog_->connect(
        godot::StringName("file_selected"),
        godot::Callable(this, godot::StringName("on_save_file_selected")));
    discard_dialog_->connect(
        godot::StringName("confirmed"),
        godot::Callable(this, godot::StringName("on_discard_confirmed")));
    discard_dialog_->connect(
        godot::StringName("canceled"),
        godot::Callable(this, godot::StringName("on_discard_canceled")));

    return true;
}

void LevelEditor::_ready() {
    if (!bind_scene()) {
        queue_redraw();
        return;
    }

    auto catalog = content::make_canonical_prototile_catalog();
    if (!catalog) {
        godot::UtilityFunctions::push_error(
            "[tiles] level editor initialization failed: the canonical catalog could "
            "not be constructed");
        if (instruction_label_ != nullptr) {
            instruction_label_->set_text("the canonical catalog could not be built.");
        }
        queue_redraw();
        return;
    }
    catalog_ = std::move(catalog).value();

    build_palette_rows();

    set_focus_mode(godot::Control::FOCUS_ALL);
    install_new_document();
    grab_focus();

    godot::UtilityFunctions::print(
        "[tiles] level editor ready: ", number(catalog_->entries().size()),
        " catalog rows");
}

void LevelEditor::build_palette_rows() {
    if (rows_container_ == nullptr || !catalog_.has_value()) {
        return;
    }

    row_controls_.clear();
    const std::vector<content::CanonicalPrototile> &entries = catalog_->entries();
    row_controls_.reserve(entries.size());

    for (std::size_t index = 0; index < entries.size(); ++index) {
        godot::HBoxContainer *row = memnew(godot::HBoxContainer);
        row->set_name(godot::String("Row") + number(index));
        rows_container_->add_child(row);

        RowControls controls;

        controls.include = memnew(godot::CheckBox);
        controls.include->set_name("Include");
        controls.include->set_tooltip_text("include this tile in the level palette");
        row->add_child(controls.include);

        controls.preview = memnew(PrototilePreview);
        controls.preview->set_name("Preview");
        controls.preview->set_custom_minimum_size(godot::Vector2(40.0f, 40.0f));
        controls.preview->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
        controls.preview->set_polygon(entries[index].prototile().polygon());
        row->add_child(controls.preview);

        godot::VBoxContainer *body = memnew(godot::VBoxContainer);
        body->set_name("Body");
        body->set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
        row->add_child(body);

        controls.name = memnew(godot::Label);
        controls.name->set_name("Name");
        controls.name->set_text(godot::String(entries[index].display_name().c_str()));
        body->add_child(controls.name);

        godot::HBoxContainer *line = memnew(godot::HBoxContainer);
        line->set_name("Line");
        body->add_child(line);

        controls.supply = memnew(godot::OptionButton);
        controls.supply->set_name("Supply");
        controls.supply->add_item("unlimited", SUPPLY_ITEM_UNLIMITED);
        controls.supply->add_item("finite", SUPPLY_ITEM_FINITE);
        controls.supply->select(SUPPLY_ITEM_UNLIMITED);
        controls.supply->set_tooltip_text("unlimited or a positive finite amount");
        line->add_child(controls.supply);

        controls.amount = memnew(godot::SpinBox);
        controls.amount->set_name("Amount");
        controls.amount->set_min(static_cast<double>(MIN_FINITE_SUPPLY));
        controls.amount->set_max(static_cast<double>(MAX_FINITE_SUPPLY));
        controls.amount->set_step(1.0);
        controls.amount->set_use_rounded_values(true);
        controls.amount->set_allow_greater(false);
        controls.amount->set_allow_lesser(false);
        controls.amount->set_value(1.0);
        controls.amount->set_tooltip_text("how many of this tile the level provides");
        line->add_child(controls.amount);

        controls.color = memnew(godot::ColorPickerButton);
        controls.color->set_name("Color");
        controls.color->set_custom_minimum_size(godot::Vector2(56.0f, 0.0f));
        controls.color->set_edit_alpha(false);
        controls.color->set_pick_color(default_row_color(index));
        controls.color->set_tooltip_text("this level's color for this tile");
        line->add_child(controls.color);

        const std::int64_t bound = static_cast<std::int64_t>(index);
        controls.include->connect(
            godot::StringName("toggled"),
            godot::Callable(this, godot::StringName("on_row_included_toggled"))
                .bind(bound));
        controls.supply->connect(
            godot::StringName("item_selected"),
            godot::Callable(this, godot::StringName("on_row_supply_selected")).bind(bound));
        controls.amount->connect(
            godot::StringName("value_changed"),
            godot::Callable(this, godot::StringName("on_row_amount_changed")).bind(bound));
        controls.color->connect(
            godot::StringName("color_changed"),
            godot::Callable(this, godot::StringName("on_row_color_changed")).bind(bound));

        row_controls_.push_back(controls);
    }
}

// --- document lifecycle ---

void LevelEditor::install_new_document() {
    if (!catalog_.has_value()) {
        return;
    }

    Document document;

    godot::Ref<PaletteResource> palette;
    palette.instantiate();
    palette->set_entries(godot::TypedArray<PaletteEntryResource>());

    document.resource.instantiate();
    document.resource->set_palette(palette);
    document.resource->set_region(godot::Ref<RegionResource>());

    document.rows.reserve(catalog_->entries().size());
    for (std::size_t index = 0; index < catalog_->entries().size(); ++index) {
        PaletteRow row;
        row.catalog_index = index;
        row.included = false;
        row.unlimited = true;
        row.finite_amount = MIN_FINITE_SUPPLY;
        row.color = default_row_color(index);
        document.rows.push_back(row);
    }

    document.dirty = true;
    document_ = std::move(document);

    cursor_pixels_.reset();
    snapped_.reset();
    snap_out_of_range_ = false;
    pan_anchor_.reset();

    update_canvas_rect();
    center_camera_on_origin();

    sync_all_row_controls();
    set_status("new unsaved level");
    refresh_controls();
    queue_redraw();
}

bool LevelEditor::open_draft(const godot::String &p_path) {
    if (!catalog_.has_value()) {
        set_status("cannot open: the canonical catalog is unavailable");
        return false;
    }
    if (p_path.is_empty()) {
        set_status("cannot open: no path was chosen");
        return false;
    }

    // Uncached and deep, so the path is observed as persisted rather than as an
    // older cached object.
    const godot::Ref<godot::Resource> loaded =
        godot::ResourceLoader::get_singleton()->load(
            p_path,
            "LevelResource",
            godot::ResourceLoader::CACHE_MODE_IGNORE_DEEP);
    if (loaded.is_null()) {
        set_status("cannot open " + p_path + ": the loader failed");
        return false;
    }

    godot::Ref<LevelResource> level = godot::Object::cast_to<LevelResource>(loaded.ptr());
    if (level.is_null()) {
        set_status("cannot open " + p_path + ": it is not a LevelResource");
        return false;
    }

    // Nothing is installed until every check below succeeds; the candidate
    // values are assembled first and the active document is untouched.
    Document candidate;
    candidate.resource = level;
    candidate.rows.reserve(catalog_->entries().size());
    for (std::size_t index = 0; index < catalog_->entries().size(); ++index) {
        PaletteRow row;
        row.catalog_index = index;
        row.included = false;
        row.unlimited = true;
        row.finite_amount = MIN_FINITE_SUPPLY;
        row.color = default_row_color(index);
        candidate.rows.push_back(row);
    }

    bool repaired = false;
    godot::Ref<PaletteResource> palette = level->get_palette();
    if (palette.is_null()) {
        // A null palette is repaired into an empty editable one. That is a graph
        // change, so the opened document is dirty.
        palette.instantiate();
        palette->set_entries(godot::TypedArray<PaletteEntryResource>());
        level->set_palette(palette);
        repaired = true;
    } else if (!palette->get_entries().is_empty()) {
        auto compiled = compile_palette_resource(palette, catalog_.value());
        if (!compiled) {
            set_status("cannot open " + p_path + ": " + describe(compiled.error()));
            return false;
        }

        // Every compiled entry resolves to exactly one catalog row, because
        // compilation already proved every id is known and unique.
        const godot::TypedArray<PaletteEntryResource> entries = palette->get_entries();
        for (std::int64_t i = 0; i < entries.size(); ++i) {
            const godot::Ref<PaletteEntryResource> entry = entries[i];
            if (entry.is_null()) {
                continue;
            }
            const PrototileId id(static_cast<PrototileId::Value>(entry->get_prototile_id()));
            std::optional<std::size_t> row_index;
            for (std::size_t index = 0; index < catalog_->entries().size(); ++index) {
                if (catalog_->entries()[index].prototile().id() == id) {
                    row_index = index;
                    break;
                }
            }
            if (!row_index.has_value()) {
                // Unreachable after successful compilation; reported rather than
                // asserted away, and the current document is preserved.
                godot::UtilityFunctions::push_error(
                    "[tiles] level editor: a compiled palette entry has no catalog row");
                set_status("cannot open " + p_path + ": internal catalog mismatch");
                return false;
            }

            PaletteRow &row = candidate.rows[row_index.value()];
            row.included = true;
            const std::int64_t supply = entry->get_supply();
            row.unlimited = supply < 0;
            if (!row.unlimited) {
                row.finite_amount =
                    std::min<std::int64_t>(std::max<std::int64_t>(supply, MIN_FINITE_SUPPLY),
                        MAX_FINITE_SUPPLY);
            }
            // Authored color is displayed with alpha forced to one. The resource
            // itself is not rewritten until the author edits the palette.
            row.color = opaque(entry->get_color());
        }

        candidate.compiled_palette = std::move(compiled).value();
    }

    const godot::Ref<RegionResource> region = level->get_region();
    if (region.is_valid()) {
        auto compiled = compile_region_resource(region);
        if (!compiled) {
            set_status("cannot open " + p_path + ": " + describe(compiled.error()));
            return false;
        }
        candidate.compiled_region = std::move(compiled).value();
    }

    candidate.dirty = repaired;
    document_ = std::move(candidate);

    cursor_pixels_.reset();
    snapped_.reset();
    snap_out_of_range_ = false;
    pan_anchor_.reset();

    update_canvas_rect();
    if (document_->compiled_region.has_value()) {
        frame_region(document_->compiled_region.value());
    } else {
        center_camera_on_origin();
        // The base contract: an opened draft with no region begins its outer
        // loop immediately, because there is nothing else to do with it.
        document_->open_loop = OpenLoop { BoundaryKind::outer_replacement, {}, false };
    }

    sync_all_row_controls();
    set_status(
        repaired ? godot::String("opened ") + p_path + " (its missing palette was repaired)"
                 : godot::String("opened ") + p_path);
    refresh_controls();
    grab_focus();
    queue_redraw();
    return true;
}

// --- palette rows ---

void LevelEditor::set_row_included(std::size_t p_row, bool p_included) {
    if (!document_.has_value() || p_row >= document_->rows.size()) {
        return;
    }
    if (document_->rows[p_row].included == p_included) {
        return;
    }
    const std::vector<PaletteRow> previous = document_->rows;
    document_->rows[p_row].included = p_included;
    publish_palette(previous);
}

void LevelEditor::set_row_unlimited(std::size_t p_row, bool p_unlimited) {
    if (!document_.has_value() || p_row >= document_->rows.size()) {
        return;
    }
    if (document_->rows[p_row].unlimited == p_unlimited) {
        return;
    }
    const std::vector<PaletteRow> previous = document_->rows;
    document_->rows[p_row].unlimited = p_unlimited;
    // An excluded row keeps its local choice without republishing: it is absent
    // from the resource either way.
    if (!document_->rows[p_row].included) {
        sync_row_controls(p_row);
        return;
    }
    publish_palette(previous);
}

void LevelEditor::set_row_finite_amount(std::size_t p_row, std::int64_t p_amount) {
    if (!document_.has_value() || p_row >= document_->rows.size()) {
        return;
    }
    const std::int64_t clamped = std::min<std::int64_t>(
        std::max<std::int64_t>(p_amount, MIN_FINITE_SUPPLY), MAX_FINITE_SUPPLY);
    if (document_->rows[p_row].finite_amount == clamped) {
        sync_row_controls(p_row);
        return;
    }
    const std::vector<PaletteRow> previous = document_->rows;
    document_->rows[p_row].finite_amount = clamped;
    if (!document_->rows[p_row].included || document_->rows[p_row].unlimited) {
        sync_row_controls(p_row);
        return;
    }
    publish_palette(previous);
}

void LevelEditor::set_row_color(std::size_t p_row, const godot::Color &p_color) {
    if (!document_.has_value() || p_row >= document_->rows.size()) {
        return;
    }
    const godot::Color color = opaque(p_color);
    if (document_->rows[p_row].color == color) {
        return;
    }
    const std::vector<PaletteRow> previous = document_->rows;
    document_->rows[p_row].color = color;
    if (!document_->rows[p_row].included) {
        sync_row_controls(p_row);
        return;
    }
    publish_palette(previous);
}

void LevelEditor::publish_palette(const std::vector<PaletteRow> &p_previous_rows) {
    if (!document_.has_value() || !catalog_.has_value()) {
        return;
    }

    // Construct and replace: the live palette is never mutated while a candidate
    // is being proven, so an externally pathed palette in an opened resource is
    // left exactly as it was found.
    godot::Ref<PaletteResource> candidate;
    candidate.instantiate();
    godot::TypedArray<PaletteEntryResource> entries;
    for (const PaletteRow &row : document_->rows) {
        if (!row.included) {
            continue;
        }
        if (row.catalog_index >= catalog_->entries().size()) {
            continue;
        }
        godot::Ref<PaletteEntryResource> entry;
        entry.instantiate();
        entry->set_prototile_id(static_cast<std::int64_t>(
            catalog_->entries()[row.catalog_index].prototile().id().value()));
        entry->set_supply(row.unlimited ? -1 : row.finite_amount);
        entry->set_color(opaque(row.color));
        entries.push_back(entry);
    }
    candidate->set_entries(entries);

    if (entries.is_empty()) {
        // An empty palette is an ordinary incomplete draft, not an error.
        document_->resource->set_palette(candidate);
        document_->compiled_palette.reset();
        document_->dirty = true;
        sync_all_row_controls();
        set_status("choose at least one tile to complete the palette");
        refresh_controls();
        return;
    }

    auto compiled = compile_palette_resource(candidate, catalog_.value());
    if (!compiled) {
        // Unreachable through these controls: rows come only from the catalog,
        // ids are unique by row identity, and supplies are constrained. Reported
        // in full rather than asserted away, with the visible controls restored.
        godot::UtilityFunctions::push_error(
            "[tiles] level editor: a row-built palette failed to compile");
        document_->rows = p_previous_rows;
        sync_all_row_controls();
        set_status("palette could not be published: " + describe(compiled.error()));
        refresh_controls();
        return;
    }

    document_->resource->set_palette(candidate);
    document_->compiled_palette = std::move(compiled).value();
    document_->dirty = true;
    sync_all_row_controls();
    set_status(
        "palette: " + number(document_->compiled_palette->order()) + " tile types");
    refresh_controls();
}

void LevelEditor::sync_row_controls(std::size_t p_row) {
    if (!document_.has_value() || p_row >= document_->rows.size()
        || p_row >= row_controls_.size()) {
        return;
    }
    const PaletteRow &row = document_->rows[p_row];
    const RowControls &controls = row_controls_[p_row];

    suppress_row_signals_ = true;
    if (controls.include != nullptr) {
        controls.include->set_pressed_no_signal(row.included);
    }
    if (controls.supply != nullptr) {
        controls.supply->select(row.unlimited ? SUPPLY_ITEM_UNLIMITED : SUPPLY_ITEM_FINITE);
        controls.supply->set_disabled(!row.included);
    }
    if (controls.amount != nullptr) {
        controls.amount->set_value_no_signal(static_cast<double>(row.finite_amount));
        controls.amount->set_editable(row.included && !row.unlimited);
    }
    if (controls.color != nullptr) {
        controls.color->set_pick_color(row.color);
        controls.color->set_disabled(!row.included);
    }
    suppress_row_signals_ = false;
}

void LevelEditor::sync_all_row_controls() {
    if (!document_.has_value()) {
        return;
    }
    for (std::size_t index = 0; index < document_->rows.size(); ++index) {
        sync_row_controls(index);
    }
}

// --- region authoring ---

void LevelEditor::begin_outer_loop() {
    if (!document_.has_value() || document_->open_loop.has_value()) {
        return;
    }
    document_->open_loop = OpenLoop { BoundaryKind::outer_replacement, {}, false };
    set_status("drawing a replacement outer boundary");
    refresh_controls();
    grab_focus();
    queue_redraw();
}

void LevelEditor::begin_hole_loop() {
    if (!document_.has_value() || document_->open_loop.has_value()) {
        return;
    }
    if (!document_->compiled_region.has_value()) {
        set_status("a hole needs a valid region first");
        return;
    }
    document_->open_loop = OpenLoop { BoundaryKind::new_hole, {}, false };
    set_status("drawing a new hole");
    refresh_controls();
    grab_focus();
    queue_redraw();
}

bool LevelEditor::append_point(GridPoint p_point) {
    if (!document_.has_value() || !document_->open_loop.has_value()) {
        return false;
    }
    if (p_point.x < MIN_GAME_UNIT || p_point.x > MAX_GAME_UNIT
        || p_point.y < MIN_GAME_UNIT || p_point.y > MAX_GAME_UNIT) {
        set_status("that point leaves the representable coordinate range");
        return false;
    }

    OpenLoop &loop = document_->open_loop.value();
    if (!loop.vertices.empty() && loop.vertices.back() == p_point) {
        set_status("that is already the newest point");
        return false;
    }

    loop.vertices.push_back(p_point);
    // Any change to the points invalidates a diagnostic which described an
    // older proposal.
    loop.failed_closure = false;
    set_status(
        "points: " + number(loop.vertices.size())
        + (loop.vertices.size() >= 3 ? godot::String(" — the loop can be closed")
                                     : godot::String()));
    refresh_controls();
    queue_redraw();
    return true;
}

void LevelEditor::remove_last_point() {
    if (!document_.has_value() || !document_->open_loop.has_value()) {
        return;
    }
    OpenLoop &loop = document_->open_loop.value();
    if (loop.vertices.empty()) {
        return;
    }
    loop.vertices.pop_back();
    loop.failed_closure = false;
    set_status("points: " + number(loop.vertices.size()));
    refresh_controls();
    queue_redraw();
}

void LevelEditor::cancel_loop() {
    if (!document_.has_value() || !document_->open_loop.has_value()) {
        return;
    }
    // Cancellation touches neither the resource nor dirty state: nothing was
    // ever committed.
    document_->open_loop.reset();
    set_status("drawing cancelled");
    refresh_controls();
    queue_redraw();
}

bool LevelEditor::close_loop() {
    if (!document_.has_value() || !document_->open_loop.has_value()) {
        return false;
    }
    if (document_->open_loop->vertices.size() < 3) {
        set_status("a boundary needs at least three points");
        return false;
    }
    return commit_open_loop();
}

bool LevelEditor::commit_open_loop() {
    Document &document = document_.value();
    OpenLoop &loop = document.open_loop.value();

    // The proposed boundary, built from the stored integers. Each integer is
    // exactly representable as a Vector2 component; the resource compiler
    // remains the one quantization boundary.
    godot::Ref<PolygonResource> proposed;
    proposed.instantiate();
    godot::PackedVector2Array vertices;
    for (const GridPoint &point : loop.vertices) {
        vertices.push_back(godot::Vector2(
            static_cast<real_t>(point.x), static_cast<real_t>(point.y)));
    }
    proposed->set_vertices(vertices);

    // A candidate region graph beside the live one. Nothing below mutates the
    // accepted RegionResource, so a failure leaves the document exactly as it
    // was.
    godot::Ref<RegionResource> candidate;
    candidate.instantiate();
    godot::TypedArray<PolygonResource> holes;

    const bool is_hole = loop.kind == BoundaryKind::new_hole;
    if (is_hole) {
        const godot::Ref<RegionResource> accepted = document.resource->get_region();
        if (accepted.is_null() || !document.compiled_region.has_value()) {
            set_status("a hole needs a valid region first");
            return false;
        }
        candidate->set_outer_boundary(accepted->get_outer_boundary());
        const godot::TypedArray<PolygonResource> accepted_holes =
            accepted->get_inner_boundaries();
        for (std::int64_t i = 0; i < accepted_holes.size(); ++i) {
            holes.push_back(accepted_holes[i]);
        }
        holes.push_back(proposed);
    } else {
        // A successful replacement discards the old holes as part of the same
        // atomic commit.
        candidate->set_outer_boundary(proposed);
    }
    candidate->set_inner_boundaries(holes);

    auto compiled = compile_region_resource(candidate);
    if (!compiled) {
        loop.failed_closure = true;
        set_status(
            (is_hole ? godot::String("this hole is invalid: ")
                     : godot::String("this outer boundary is invalid: "))
            + describe(compiled.error()));
        refresh_controls();
        queue_redraw();
        return false;
    }

    document.resource->set_region(candidate);
    document.compiled_region = std::move(compiled).value();
    document.open_loop.reset();
    document.dirty = true;
    set_status(
        is_hole ? godot::String("hole accepted")
                : godot::String("outer boundary accepted"));
    refresh_controls();
    queue_redraw();
    return true;
}

// --- save and play ---

bool LevelEditor::save_document(const godot::String &p_explicit_path) {
    // Button state is a hint; the handler rechecks every condition itself.
    if (!document_.has_value() || document_->resource.is_null()) {
        set_status("cannot save: there is no level");
        return false;
    }
    if (document_->open_loop.has_value()) {
        set_status("cannot save while a boundary is being drawn");
        return false;
    }
    if (!catalog_.has_value()) {
        set_status("cannot save: the canonical catalog is unavailable");
        return false;
    }

    // The load-bearing gate: the complete resource is compiled again, and the
    // saver is not reached at all if it fails.
    auto compiled = compile_level_resource(document_->resource, catalog_.value());
    if (!compiled) {
        set_status("cannot save: " + describe(compiled.error()));
        refresh_controls();
        return false;
    }

    auto saved = save_level_resource(document_->resource, p_explicit_path);
    if (!saved) {
        set_status("cannot save: " + describe(saved.error()));
        refresh_controls();
        return false;
    }

    document_->dirty = false;
    set_status("saved to " + saved.value());
    refresh_controls();
    return true;
}

bool LevelEditor::request_play() {
    if (!document_.has_value() || document_->resource.is_null()) {
        set_status("cannot play: there is no level");
        return false;
    }
    if (document_->open_loop.has_value()) {
        set_status("cannot play while a boundary is being drawn");
        return false;
    }
    if (!catalog_.has_value()) {
        set_status("cannot play: the canonical catalog is unavailable");
        return false;
    }

    auto compiled = compile_level_resource(document_->resource, catalog_.value());
    if (!compiled) {
        set_status("cannot play: " + describe(compiled.error()));
        refresh_controls();
        return false;
    }

    // Pointer identity is intentional: the host receives the same current
    // in-memory draft, saved or not. Nothing here saves, clones, or clears
    // dirty state.
    emit_signal(godot::StringName("play_requested"), document_->resource);
    set_status("play requested for the current level");
    return true;
}

// --- observation ---

godot::Ref<LevelResource> LevelEditor::level_resource() const {
    return document_.has_value() ? document_->resource : godot::Ref<LevelResource>();
}

const engine::Palette *LevelEditor::compiled_palette() const {
    if (!document_.has_value() || !document_->compiled_palette.has_value()) {
        return nullptr;
    }
    return &document_->compiled_palette.value();
}

const Region *LevelEditor::compiled_region() const {
    if (!document_.has_value() || !document_->compiled_region.has_value()) {
        return nullptr;
    }
    return &document_->compiled_region.value();
}

const std::vector<LevelEditor::PaletteRow> &LevelEditor::palette_rows() const {
    static const std::vector<PaletteRow> empty;
    return document_.has_value() ? document_->rows : empty;
}

const LevelEditor::OpenLoop *LevelEditor::open_loop() const {
    if (!document_.has_value() || !document_->open_loop.has_value()) {
        return nullptr;
    }
    return &document_->open_loop.value();
}

bool LevelEditor::is_dirty() const {
    return document_.has_value() && document_->dirty;
}

bool LevelEditor::can_save() const {
    return document_.has_value() && document_->resource.is_valid()
        && !document_->open_loop.has_value() && document_->compiled_palette.has_value()
        && document_->compiled_region.has_value();
}

godot::Vector2 LevelEditor::project(double p_x, double p_y) const {
    return to_screen(p_x, p_y);
}

double LevelEditor::pixels_per_unit() const {
    return camera_.pixels_per_unit;
}

godot::Vector2 LevelEditor::camera_origin() const {
    return camera_.origin_pixels;
}

godot::Rect2 LevelEditor::canvas_rect() const {
    return canvas_rect_;
}

const content::PrototileCatalog *LevelEditor::catalog() const {
    return catalog_.has_value() ? &catalog_.value() : nullptr;
}

godot::CheckBox *LevelEditor::row_include_control(std::size_t p_row) const {
    return p_row < row_controls_.size() ? row_controls_[p_row].include : nullptr;
}

PrototilePreview *LevelEditor::row_preview_control(std::size_t p_row) const {
    return p_row < row_controls_.size() ? row_controls_[p_row].preview : nullptr;
}

godot::Label *LevelEditor::row_name_control(std::size_t p_row) const {
    return p_row < row_controls_.size() ? row_controls_[p_row].name : nullptr;
}

godot::OptionButton *LevelEditor::row_supply_control(std::size_t p_row) const {
    return p_row < row_controls_.size() ? row_controls_[p_row].supply : nullptr;
}

godot::SpinBox *LevelEditor::row_amount_control(std::size_t p_row) const {
    return p_row < row_controls_.size() ? row_controls_[p_row].amount : nullptr;
}

godot::ColorPickerButton *LevelEditor::row_color_control(std::size_t p_row) const {
    return p_row < row_controls_.size() ? row_controls_[p_row].color : nullptr;
}

godot::Button *LevelEditor::action_button(const char *p_name) const {
    const godot::String path = godot::String(ACTIONS_PATH) + "/" + p_name;
    return godot::Object::cast_to<godot::Button>(
        const_cast<LevelEditor *>(this)->get_node_or_null(godot::NodePath(path)));
}

// --- visible state ---

void LevelEditor::set_status(const godot::String &p_text) {
    status_ = p_text;
    if (status_label_ != nullptr) {
        status_label_->set_text(p_text);
    }
}

void LevelEditor::refresh_path_label() {
    if (path_label_ == nullptr) {
        return;
    }
    if (!document_.has_value() || document_->resource.is_null()) {
        path_label_->set_text("no level");
        return;
    }
    const godot::String path = document_->resource->get_path();
    godot::String text = path.is_empty() ? godot::String("unsaved level") : path;
    if (document_->dirty) {
        text += " •";
    }
    path_label_->set_text(text);
}

void LevelEditor::refresh_instructions() {
    if (instruction_label_ == nullptr) {
        return;
    }
    const godot::String navigation = "\nmiddle-drag pans · wheel zooms";

    if (!document_.has_value()) {
        instruction_label_->set_text("the editor could not initialize.");
        return;
    }
    if (document_->open_loop.has_value()) {
        const bool hole = document_->open_loop->kind == BoundaryKind::new_hole;
        instruction_label_->set_text(
            (hole ? godot::String("new hole") : godot::String("outer boundary"))
            + " — click grid points; click the first point or press enter to close. "
              "backspace removes a point; esc cancels."
            + navigation);
        return;
    }

    const bool palette_ready = document_->compiled_palette.has_value();
    const bool region_ready = document_->compiled_region.has_value();
    if (!palette_ready && !region_ready) {
        instruction_label_->set_text(
            godot::String("choose at least one tile, then draw the outer boundary.")
            + navigation);
    } else if (palette_ready && !region_ready) {
        instruction_label_->set_text(
            godot::String(
                "palette ready — choose “restart region” to draw the outer "
                "boundary.")
            + navigation);
    } else if (!palette_ready && region_ready) {
        instruction_label_->set_text(
            godot::String(
                "region ready — choose at least one tile before saving or playing.")
            + navigation);
    } else {
        instruction_label_->set_text(
            godot::String("level ready — add a hole, restart the region, save, or play.")
            + navigation);
    }
}

void LevelEditor::refresh_coordinate_label() {
    if (coordinate_label_ == nullptr) {
        return;
    }
    if (snapped_.has_value()) {
        coordinate_label_->set_text(
            "(" + number(snapped_->x) + ", " + number(snapped_->y) + ")");
        return;
    }
    coordinate_label_->set_text(snap_out_of_range_ ? "(out of range)" : "(—, —)");
}

void LevelEditor::refresh_controls() {
    const bool has_document = document_.has_value();
    const bool drawing = has_document && document_->open_loop.has_value();
    const bool has_region = has_document && document_->compiled_region.has_value();
    const bool complete = can_save();

    if (new_level_button_ != nullptr) {
        new_level_button_->set_disabled(!catalog_.has_value());
    }
    if (open_draft_button_ != nullptr) {
        open_draft_button_->set_disabled(!catalog_.has_value());
    }
    if (restart_region_button_ != nullptr) {
        restart_region_button_->set_disabled(!has_document || drawing);
    }
    if (add_hole_button_ != nullptr) {
        add_hole_button_->set_disabled(!has_region || drawing);
    }
    if (save_button_ != nullptr) {
        save_button_->set_disabled(!complete);
    }
    if (save_as_button_ != nullptr) {
        save_as_button_->set_disabled(!complete);
    }
    if (play_button_ != nullptr) {
        play_button_->set_disabled(!complete);
    }

    refresh_instructions();
    refresh_path_label();
    refresh_coordinate_label();
}

// --- camera ---

void LevelEditor::update_canvas_rect() {
    const godot::Vector2 size = get_size();
    double left = 0.0;
    double top = 0.0;
    double bottom = static_cast<double>(size.y);
    if (palette_panel_ != nullptr) {
        left = static_cast<double>(palette_panel_->get_position().x)
            + static_cast<double>(palette_panel_->get_size().x);
    }
    if (toolbar_ != nullptr) {
        top = static_cast<double>(toolbar_->get_position().y)
            + static_cast<double>(toolbar_->get_size().y);
    }
    if (status_bar_ != nullptr) {
        bottom = static_cast<double>(status_bar_->get_position().y);
    }
    const double right = static_cast<double>(size.x);
    canvas_rect_ = godot::Rect2(
        godot::Vector2(static_cast<real_t>(left), static_cast<real_t>(top)),
        godot::Vector2(
            static_cast<real_t>(std::max(0.0, right - left)),
            static_cast<real_t>(std::max(0.0, bottom - top))));
}

void LevelEditor::center_camera_on_origin() {
    camera_.pixels_per_unit = INITIAL_PIXELS_PER_UNIT;
    camera_.origin_pixels = canvas_rect_.position + canvas_rect_.size * 0.5f;
}

void LevelEditor::frame_region(const Region &p_region) {
    const Polygon::Vertices &vertices = p_region.outer_boundary().vertices();
    if (vertices.empty() || canvas_rect_.size.x <= 0.0f || canvas_rect_.size.y <= 0.0f) {
        center_camera_on_origin();
        return;
    }

    double min_x = to_real(vertices.front().x);
    double max_x = min_x;
    double min_y = to_real(vertices.front().y);
    double max_y = min_y;
    for (const Point &vertex : vertices) {
        min_x = std::min(min_x, to_real(vertex.x));
        max_x = std::max(max_x, to_real(vertex.x));
        min_y = std::min(min_y, to_real(vertex.y));
        max_y = std::max(max_y, to_real(vertex.y));
    }

    const double width = std::max(max_x - min_x, 1e-9);
    const double height = std::max(max_y - min_y, 1e-9);
    const double available_x =
        std::max(1.0, static_cast<double>(canvas_rect_.size.x) - 2.0 * FRAME_MARGIN_PIXELS);
    const double available_y =
        std::max(1.0, static_cast<double>(canvas_rect_.size.y) - 2.0 * FRAME_MARGIN_PIXELS);

    double scale = std::min(available_x / width, available_y / height);
    scale = std::min(std::max(scale, MIN_PIXELS_PER_UNIT), MAX_PIXELS_PER_UNIT);
    camera_.pixels_per_unit = scale;

    const double center_x = 0.5 * (min_x + max_x);
    const double center_y = 0.5 * (min_y + max_y);
    const godot::Vector2 canvas_center = canvas_rect_.position + canvas_rect_.size * 0.5f;
    camera_.origin_pixels = godot::Vector2(
        static_cast<real_t>(static_cast<double>(canvas_center.x) - scale * center_x),
        static_cast<real_t>(static_cast<double>(canvas_center.y) + scale * center_y));
}

godot::Vector2 LevelEditor::to_screen(double p_x, double p_y) const {
    return godot::Vector2(
        static_cast<real_t>(
            static_cast<double>(camera_.origin_pixels.x) + camera_.pixels_per_unit * p_x),
        static_cast<real_t>(
            static_cast<double>(camera_.origin_pixels.y) - camera_.pixels_per_unit * p_y));
}

godot::Vector2 LevelEditor::to_screen(GridPoint p_point) const {
    return to_screen(static_cast<double>(p_point.x), static_cast<double>(p_point.y));
}

std::optional<LevelEditor::GridPoint> LevelEditor::snap_local(
    godot::Vector2 p_local) const {
    if (camera_.pixels_per_unit <= 0.0) {
        return std::nullopt;
    }
    const double world_x = (static_cast<double>(p_local.x)
                               - static_cast<double>(camera_.origin_pixels.x))
        / camera_.pixels_per_unit;
    const double world_y = -(static_cast<double>(p_local.y)
                               - static_cast<double>(camera_.origin_pixels.y))
        / camera_.pixels_per_unit;
    if (!std::isfinite(world_x) || !std::isfinite(world_y)) {
        return std::nullopt;
    }

    const double x = round_half_away_from_zero(world_x);
    const double y = round_half_away_from_zero(world_y);
    if (!(x >= static_cast<double>(MIN_GAME_UNIT))
        || !(x <= static_cast<double>(MAX_GAME_UNIT))
        || !(y >= static_cast<double>(MIN_GAME_UNIT))
        || !(y <= static_cast<double>(MAX_GAME_UNIT))) {
        return std::nullopt;
    }
    return GridPoint { static_cast<std::int64_t>(x), static_cast<std::int64_t>(y) };
}

void LevelEditor::update_snapped_cursor(godot::Vector2 p_local) {
    const std::optional<GridPoint> previous = snapped_;
    const bool previously_out_of_range = snap_out_of_range_;

    cursor_pixels_ = p_local;
    snapped_ = snap_local(p_local);
    snap_out_of_range_ = !snapped_.has_value();

    if (snapped_ != previous || snap_out_of_range_ != previously_out_of_range) {
        refresh_coordinate_label();
        queue_redraw();
    } else if (document_.has_value() && document_->open_loop.has_value()) {
        // The open-edge preview follows the pointer even between snap changes.
        queue_redraw();
    }
}

void LevelEditor::zoom_at(godot::Vector2 p_local, double p_factor) {
    const double before = camera_.pixels_per_unit;
    const double after =
        std::min(std::max(before * p_factor, MIN_PIXELS_PER_UNIT), MAX_PIXELS_PER_UNIT);
    if (after == before) {
        return;
    }

    // Cursor-centered: the world point under the pointer stays under it.
    const double world_x =
        (static_cast<double>(p_local.x) - static_cast<double>(camera_.origin_pixels.x))
        / before;
    const double world_y =
        (static_cast<double>(p_local.y) - static_cast<double>(camera_.origin_pixels.y))
        / before;
    camera_.pixels_per_unit = after;
    camera_.origin_pixels = godot::Vector2(
        static_cast<real_t>(static_cast<double>(p_local.x) - world_x * after),
        static_cast<real_t>(static_cast<double>(p_local.y) - world_y * after));

    update_snapped_cursor(p_local);
    queue_redraw();
}

// --- input ---

void LevelEditor::_gui_input(const godot::Ref<godot::InputEvent> &p_event) {
    if (p_event.is_null()) {
        return;
    }

    const godot::Ref<godot::InputEventKey> key = p_event;
    if (key.is_valid()) {
        if (!key->is_pressed() || key->is_echo()) {
            return;
        }
        if (!document_.has_value() || !document_->open_loop.has_value()) {
            return;
        }
        const godot::Key code = key->get_keycode();
        if (code == godot::KEY_ENTER || code == godot::KEY_KP_ENTER) {
            close_loop();
            accept_event();
            return;
        }
        if (code == godot::KEY_BACKSPACE) {
            remove_last_point();
            accept_event();
            return;
        }
        if (code == godot::KEY_ESCAPE) {
            cancel_loop();
            accept_event();
            return;
        }
        return;
    }

    const godot::Ref<godot::InputEventMouseMotion> motion = p_event;
    if (motion.is_valid()) {
        const godot::Vector2 position = motion->get_position();
        if (pan_anchor_.has_value()) {
            camera_.origin_pixels += position - pan_anchor_.value();
            pan_anchor_ = position;
            update_snapped_cursor(position);
            accept_event();
            return;
        }
        if (canvas_rect_.has_point(position)) {
            update_snapped_cursor(position);
        } else if (cursor_pixels_.has_value()) {
            cursor_pixels_.reset();
            snapped_.reset();
            snap_out_of_range_ = false;
            refresh_coordinate_label();
            queue_redraw();
        }
        return;
    }

    const godot::Ref<godot::InputEventMouseButton> button = p_event;
    if (button.is_valid()) {
        const godot::Vector2 position = button->get_position();
        const godot::MouseButton index = button->get_button_index();

        if (index == godot::MOUSE_BUTTON_MIDDLE) {
            if (button->is_pressed()) {
                if (!canvas_rect_.has_point(position)) {
                    return;
                }
                pan_anchor_ = position;
            } else {
                pan_anchor_.reset();
            }
            accept_event();
            return;
        }

        if (!button->is_pressed() || !canvas_rect_.has_point(position)) {
            return;
        }

        if (index == godot::MOUSE_BUTTON_WHEEL_UP) {
            zoom_at(position, ZOOM_STEP);
            accept_event();
            return;
        }
        if (index == godot::MOUSE_BUTTON_WHEEL_DOWN) {
            zoom_at(position, 1.0 / ZOOM_STEP);
            accept_event();
            return;
        }

        if (index != godot::MOUSE_BUTTON_LEFT) {
            return;
        }

        grab_focus();
        update_snapped_cursor(position);

        // A click outside an open loop never creates a hidden loop: the toolbar
        // starts outer and hole drawing explicitly.
        if (!document_.has_value() || !document_->open_loop.has_value()) {
            accept_event();
            return;
        }
        if (!snapped_.has_value()) {
            set_status("that point leaves the representable coordinate range");
            accept_event();
            return;
        }

        const OpenLoop &loop = document_->open_loop.value();
        if (!loop.vertices.empty() && loop.vertices.front() == snapped_.value()) {
            if (loop.vertices.size() >= 3) {
                close_loop();
            } else {
                set_status("a boundary needs at least three points before it can close");
            }
            accept_event();
            return;
        }

        append_point(snapped_.value());
        accept_event();
        return;
    }
}

// --- toolbar and dialogs ---

void LevelEditor::popup_open_dialog() {
    if (open_dialog_ == nullptr) {
        return;
    }
    open_dialog_->popup_centered_ratio(0.7f);
}

void LevelEditor::popup_save_dialog() {
    if (save_dialog_ == nullptr) {
        return;
    }
    save_dialog_->popup_centered_ratio(0.7f);
}

void LevelEditor::on_new_level_pressed() {
    if (document_.has_value() && document_->dirty) {
        pending_action_ = PendingAction::new_level;
        if (discard_dialog_ != nullptr) {
            discard_dialog_->popup_centered();
        }
        return;
    }
    install_new_document();
}

void LevelEditor::on_open_draft_pressed() {
    if (document_.has_value() && document_->dirty) {
        pending_action_ = PendingAction::open_draft;
        if (discard_dialog_ != nullptr) {
            discard_dialog_->popup_centered();
        }
        return;
    }
    popup_open_dialog();
}

void LevelEditor::on_discard_confirmed() {
    const PendingAction action = pending_action_;
    pending_action_ = PendingAction::none;
    if (action == PendingAction::new_level) {
        install_new_document();
    } else if (action == PendingAction::open_draft) {
        popup_open_dialog();
    }
}

void LevelEditor::on_discard_canceled() {
    // The complete current document, open loop, camera, and status survive.
    pending_action_ = PendingAction::none;
}

void LevelEditor::on_restart_region_pressed() {
    begin_outer_loop();
}

void LevelEditor::on_add_hole_pressed() {
    begin_hole_loop();
}

void LevelEditor::on_save_pressed() {
    if (!document_.has_value() || document_->resource.is_null()) {
        set_status("cannot save: there is no level");
        return;
    }
    if (document_->resource->get_path().is_empty()) {
        popup_save_dialog();
        return;
    }
    save_document(godot::String());
}

void LevelEditor::on_save_as_pressed() {
    popup_save_dialog();
}

void LevelEditor::on_play_pressed() {
    request_play();
}

void LevelEditor::on_open_file_selected(const godot::String &p_path) {
    open_draft(p_path);
    grab_focus();
}

void LevelEditor::on_save_file_selected(const godot::String &p_path) {
    save_document(p_path);
    grab_focus();
}

void LevelEditor::on_row_included_toggled(bool p_pressed, std::int64_t p_row) {
    if (suppress_row_signals_ || p_row < 0) {
        return;
    }
    set_row_included(static_cast<std::size_t>(p_row), p_pressed);
}

void LevelEditor::on_row_supply_selected(std::int64_t p_index, std::int64_t p_row) {
    if (suppress_row_signals_ || p_row < 0) {
        return;
    }
    set_row_unlimited(
        static_cast<std::size_t>(p_row), p_index == SUPPLY_ITEM_UNLIMITED);
}

void LevelEditor::on_row_amount_changed(double p_amount, std::int64_t p_row) {
    if (suppress_row_signals_ || p_row < 0) {
        return;
    }
    if (!std::isfinite(p_amount)) {
        return;
    }
    set_row_finite_amount(
        static_cast<std::size_t>(p_row),
        static_cast<std::int64_t>(round_half_away_from_zero(p_amount)));
}

void LevelEditor::on_row_color_changed(const godot::Color &p_color, std::int64_t p_row) {
    if (suppress_row_signals_ || p_row < 0) {
        return;
    }
    set_row_color(static_cast<std::size_t>(p_row), p_color);
}

// --- drawing ---

void LevelEditor::draw_grid() {
    if (camera_.pixels_per_unit <= 0.0) {
        return;
    }

    const double left = static_cast<double>(canvas_rect_.position.x);
    const double top = static_cast<double>(canvas_rect_.position.y);
    const double right = left + static_cast<double>(canvas_rect_.size.x);
    const double bottom = top + static_cast<double>(canvas_rect_.size.y);

    const double origin_x = static_cast<double>(camera_.origin_pixels.x);
    const double origin_y = static_cast<double>(camera_.origin_pixels.y);

    // The visible world extent, then the whole units inside it. Density is
    // bounded by the minimum zoom, so this is at most canvas width / 4 lines.
    const double world_left = (left - origin_x) / camera_.pixels_per_unit;
    const double world_right = (right - origin_x) / camera_.pixels_per_unit;
    const double world_bottom = -(bottom - origin_y) / camera_.pixels_per_unit;
    const double world_top = -(top - origin_y) / camera_.pixels_per_unit;

    const std::int64_t first_x = static_cast<std::int64_t>(std::ceil(world_left));
    const std::int64_t last_x = static_cast<std::int64_t>(std::floor(world_right));
    const std::int64_t first_y = static_cast<std::int64_t>(std::ceil(world_bottom));
    const std::int64_t last_y = static_cast<std::int64_t>(std::floor(world_top));

    for (std::int64_t x = first_x; x <= last_x; ++x) {
        const real_t screen_x =
            static_cast<real_t>(origin_x + camera_.pixels_per_unit * static_cast<double>(x));
        draw_line(
            godot::Vector2(screen_x, static_cast<real_t>(top)),
            godot::Vector2(screen_x, static_cast<real_t>(bottom)),
            x == 0 ? GRID_AXIS : GRID_LINE,
            x == 0 ? AXIS_WIDTH : GRID_WIDTH);
    }
    for (std::int64_t y = first_y; y <= last_y; ++y) {
        const real_t screen_y =
            static_cast<real_t>(origin_y - camera_.pixels_per_unit * static_cast<double>(y));
        draw_line(
            godot::Vector2(static_cast<real_t>(left), screen_y),
            godot::Vector2(static_cast<real_t>(right), screen_y),
            y == 0 ? GRID_AXIS : GRID_LINE,
            y == 0 ? AXIS_WIDTH : GRID_WIDTH);
    }
}

void LevelEditor::draw_region_area() {
    if (!document_.has_value() || !document_->compiled_region.has_value()) {
        return;
    }
    const Region &region = document_->compiled_region.value();

    // The exact certified triangulations are drawn directly, so concave
    // boundaries fill correctly without a second triangulator. Holes are cut out
    // with the canvas background rather than by a boolean operation.
    const auto fill = [&](const Polygon &p_polygon, const godot::Color &p_color) {
        for (const Triangle &triangle : p_polygon.triangulation()) {
            godot::PackedVector2Array points;
            for (const Point &vertex : triangle.vertices) {
                points.push_back(to_screen(to_real(vertex.x), to_real(vertex.y)));
            }
            draw_colored_polygon(points, p_color);
        }
    };
    const auto outline = [&](const Polygon &p_polygon, const godot::Color &p_color) {
        godot::PackedVector2Array points;
        for (const Point &vertex : p_polygon.vertices()) {
            points.push_back(to_screen(to_real(vertex.x), to_real(vertex.y)));
        }
        if (points.is_empty()) {
            return;
        }
        points.push_back(points[0]);
        draw_polyline(points, p_color, BOUNDARY_WIDTH);
    };

    fill(region.outer_boundary(), REGION_FILL);
    for (const Polygon &hole : region.inner_boundaries()) {
        fill(hole, CANVAS_BACKGROUND);
    }
    outline(region.outer_boundary(), REGION_OUTLINE);
    for (const Polygon &hole : region.inner_boundaries()) {
        outline(hole, HOLE_OUTLINE);
    }
}

void LevelEditor::draw_open_loop() {
    if (!document_.has_value() || !document_->open_loop.has_value()) {
        return;
    }
    const OpenLoop &loop = document_->open_loop.value();
    const godot::Color base =
        loop.failed_closure ? LOOP_INVALID
                            : (loop.kind == BoundaryKind::new_hole ? HOLE_LOOP : OUTER_LOOP);

    godot::PackedVector2Array points;
    for (const GridPoint &vertex : loop.vertices) {
        points.push_back(to_screen(vertex));
    }
    if (points.size() >= 2) {
        draw_polyline(points, base, LOOP_WIDTH);
    }
    // The closing edge is shown once the loop could actually close, so the
    // proposed boundary reads as the polygon it would become.
    if (points.size() >= 3) {
        draw_line(points[points.size() - 1], points[0], base, LOOP_WIDTH);
    }
    if (!points.is_empty() && snapped_.has_value()) {
        draw_line(
            points[points.size() - 1], to_screen(snapped_.value()), LOOP_PREVIEW, LOOP_WIDTH);
    }
    for (std::int64_t i = 0; i < points.size(); ++i) {
        draw_circle(points[i], POINT_RADIUS, base);
    }
    if (points.size() >= 3) {
        draw_circle(points[0], CLOSURE_RADIUS, CLOSURE_MARKER, false, 2.0f);
    }
}

void LevelEditor::draw_cursor() {
    if (!cursor_pixels_.has_value()) {
        return;
    }
    if (snapped_.has_value()) {
        const godot::Vector2 at = to_screen(snapped_.value());
        draw_circle(at, CURSOR_RADIUS, CURSOR_MARKER, false, 1.5f);
        return;
    }
    if (!snap_out_of_range_) {
        return;
    }
    // Conspicuous, and unable to append: an out-of-range cursor is marked where
    // the pointer actually is, not at an invented lattice position.
    const godot::Vector2 at = cursor_pixels_.value();
    draw_line(
        at + godot::Vector2(-REJECTED_ARM, -REJECTED_ARM),
        at + godot::Vector2(REJECTED_ARM, REJECTED_ARM),
        CURSOR_REJECTED,
        2.0f);
    draw_line(
        at + godot::Vector2(-REJECTED_ARM, REJECTED_ARM),
        at + godot::Vector2(REJECTED_ARM, -REJECTED_ARM),
        CURSOR_REJECTED,
        2.0f);
}

void LevelEditor::_draw() {
    update_canvas_rect();
    if (canvas_rect_.size.x <= 0.0f || canvas_rect_.size.y <= 0.0f) {
        return;
    }

    draw_rect(canvas_rect_, CANVAS_BACKGROUND);
    draw_grid();
    draw_region_area();
    draw_open_loop();
    draw_cursor();
}

} // namespace tiles::game
