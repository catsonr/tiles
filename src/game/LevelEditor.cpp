#include "game/LevelEditor.h"

#include "content/CanonicalOrientationCompiler.h"
#include "content/PrototileCatalog.h"
#include "core/Hex12.h"
#include "core/Prototile.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "core/geometry/Predicates.h"
#include "core/geometry/Triangle.h"
#include "game/PrototilePreview.h"
#include "game/resources/LevelResources.h"
#include "game/resources/ResourceCompiler.h"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
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

// --- camera constants ---

constexpr double INITIAL_PIXELS_PER_UNIT = 48.0;
constexpr double MIN_PIXELS_PER_UNIT = 6.0;
constexpr double MAX_PIXELS_PER_UNIT = 192.0;
constexpr double ZOOM_STEP = 1.25;

// The editor's finite-supply control bounds. This is an authoring convenience,
// not a resource or engine invariant.
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
const godot::Color AXIS_LINE(0.30f, 0.32f, 0.38f, 1.0f);
const godot::Color GHOST_OUTLINE(1.0f, 1.0f, 1.0f, 0.85f);

constexpr float AXIS_WIDTH = 1.0f;
constexpr float OUTLINE_WIDTH = 2.0f;
constexpr float OUTLINE_SHADE = 0.5f;
constexpr float GHOST_ALPHA = 0.45f;

// --- scene paths ---

const char *TOOLBAR_PATH = "Toolbar";
const char *ACTIONS_PATH = "Toolbar/Margin/Actions";
const char *PHASE_LABEL_PATH = "Toolbar/Margin/Actions/PhaseLabel";
const char *PALETTE_PANEL_PATH = "PalettePanel";
const char *PALETTE_TITLE_PATH = "PalettePanel/Margin/Body/PaletteTitle";
const char *PALETTE_SCROLL_PATH = "PalettePanel/Margin/Body/PaletteScroll";
const char *PALETTE_ROWS_PATH = "PalettePanel/Margin/Body/PaletteScroll/PaletteRows";
const char *ENTRY_TITLE_PATH = "PalettePanel/Margin/Body/EntryTitle";
const char *ENTRY_SCROLL_PATH = "PalettePanel/Margin/Body/EntryScroll";
const char *ENTRY_ROWS_PATH = "PalettePanel/Margin/Body/EntryScroll/EntryRows";
const char *STATUS_BAR_PATH = "StatusBar";
const char *INSTRUCTION_PATH = "StatusBar/Margin/Body/InstructionLabel";
const char *STATUS_PATH = "StatusBar/Margin/Body/Line/StatusLabel";
const char *SELECTION_PATH = "StatusBar/Margin/Body/Line/SelectionLabel";

const char *LATTICE_BUTTON = "LatticeButton";
const char *HEX12_BUTTON = "Hex12Button";
const char *CHOOSE_DOMAIN_BUTTON = "ChooseDomainButton";
const char *BUILD_PALETTE_BUTTON = "BuildPaletteButton";
const char *ROTATE_BUTTON = "RotateButton";
const char *CLEAR_BLUEPRINT_BUTTON = "ClearBlueprintButton";

// The two supply modes, in the order they are added to every row.
constexpr int SUPPLY_ITEM_UNLIMITED = 0;
constexpr int SUPPLY_ITEM_FINITE = 1;

double to_real(Coordinate p_coordinate) {
    return static_cast<double>(p_coordinate.raw()) / static_cast<double>(Coordinate::SCALE);
}

// Round to the nearest integer with exact ties away from zero: the same tie
// policy the q16.48 quantizer documents.
double round_half_away_from_zero(double p_value) {
    return p_value < 0.0 ? std::ceil(p_value - 0.5) : std::floor(p_value + 0.5);
}

godot::Color opaque(const godot::Color &p_color) {
    return godot::Color(p_color.r, p_color.g, p_color.b, 1.0f);
}

godot::Color outline_for(const godot::Color &p_fill) {
    return godot::Color(
        p_fill.r * OUTLINE_SHADE,
        p_fill.g * OUTLINE_SHADE,
        p_fill.b * OUTLINE_SHADE,
        p_fill.a);
}

godot::Color default_row_color(std::size_t p_index) {
    const double hue =
        std::fmod(static_cast<double>(p_index + 1) * DEFAULT_HUE_STEP, 1.0);
    return godot::Color::from_hsv(
        static_cast<float>(hue), DEFAULT_SATURATION, DEFAULT_VALUE, 1.0f);
}

godot::String number(std::int64_t p_value) {
    return godot::String::num_int64(p_value);
}

godot::String number(std::size_t p_value) {
    return godot::String::num_int64(static_cast<std::int64_t>(p_value));
}

godot::String number(std::uint64_t p_value) {
    return godot::String::num_uint64(p_value);
}

bool is_supported_domain(content::GeometryDomain p_domain) {
    return p_domain == content::GeometryDomain::lattice
        || p_domain == content::GeometryDomain::hex12;
}

const char *domain_name(content::GeometryDomain p_domain) {
    switch (p_domain) {
        case content::GeometryDomain::lattice:
            return "lattice";
        case content::GeometryDomain::hex12:
            return "hex12";
    }
    return "unsupported domain";
}

// --- exact bounds, used only to skip impossible contact work ---

struct Bounds final {
    Coordinate min_x;
    Coordinate min_y;
    Coordinate max_x;
    Coordinate max_y;
};

Bounds bounds_of(const Polygon &p_polygon) {
    const Polygon::Vertices &vertices = p_polygon.vertices();
    Bounds bounds {
        vertices.front().x,
        vertices.front().y,
        vertices.front().x,
        vertices.front().y,
    };
    for (const Point &vertex : vertices) {
        if (vertex.x < bounds.min_x) {
            bounds.min_x = vertex.x;
        }
        if (bounds.max_x < vertex.x) {
            bounds.max_x = vertex.x;
        }
        if (vertex.y < bounds.min_y) {
            bounds.min_y = vertex.y;
        }
        if (bounds.max_y < vertex.y) {
            bounds.max_y = vertex.y;
        }
    }
    return bounds;
}

bool bounds_touch(const Bounds &p_lhs, const Bounds &p_rhs) {
    return p_lhs.min_x <= p_rhs.max_x && p_rhs.min_x <= p_lhs.max_x
        && p_lhs.min_y <= p_rhs.max_y && p_rhs.min_y <= p_lhs.max_y;
}

// Whether a proposed footprint meets any placed footprint along a shared
// segment of positive length rather than only at isolated points.
//
// Isolated point contact is perfectly legal geometry, and the core still derives
// and admits it; it is simply not useful for building a tiling, so this editor
// does not offer it. The decision is exact — the core segment predicate on the
// same lattice values — never a projected distance, a pixel, or a tolerance.
bool shares_edge_contact(const Polygon &p_footprint, const Arrangement &p_arrangement) {
    const Polygon::Vertices &candidate = p_footprint.vertices();
    const Bounds candidate_bounds = bounds_of(p_footprint);

    for (const Entry &entry : p_arrangement.entries()) {
        // Closed bounding boxes that do not meet cannot share a single point,
        // let alone a segment. This is an exact conservative skip on the same
        // lattice values: it only ever avoids work.
        const Bounds placed_bounds = bounds_of(entry.placement.footprint());
        if (!bounds_touch(candidate_bounds, placed_bounds)) {
            continue;
        }

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

// Godot closes a filled polygon implicitly but draws a polyline open, so the
// outline repeats the first vertex and nothing else does.
godot::PackedVector2Array closed_boundary(const godot::PackedVector2Array &p_points) {
    godot::PackedVector2Array closed = p_points;
    if (!p_points.is_empty()) {
        closed.push_back(p_points[0]);
    }
    return closed;
}

// A presentation-only ray cast over an already-projected boundary. It answers
// only "which stored record did the pointer land on", and can therefore never
// produce a model coordinate.
bool screen_polygon_contains(
    const godot::PackedVector2Array &p_points, godot::Vector2 p_query) {
    bool inside = false;
    const std::int64_t count = p_points.size();
    if (count < 3) {
        return false;
    }
    for (std::int64_t i = 0, j = count - 1; i < count; j = i++) {
        const godot::Vector2 a = p_points[i];
        const godot::Vector2 b = p_points[j];
        const bool straddles =
            (static_cast<double>(a.y) > static_cast<double>(p_query.y))
            != (static_cast<double>(b.y) > static_cast<double>(p_query.y));
        if (!straddles) {
            continue;
        }
        const double dy = static_cast<double>(b.y) - static_cast<double>(a.y);
        if (dy == 0.0) {
            continue;
        }
        const double t =
            (static_cast<double>(p_query.y) - static_cast<double>(a.y)) / dy;
        const double x = static_cast<double>(a.x)
            + t * (static_cast<double>(b.x) - static_cast<double>(a.x));
        if (static_cast<double>(p_query.x) < x) {
            inside = !inside;
        }
    }
    return inside;
}

// --- typed error presentation ---
//
// Complete typed compiler errors are turned into text here, at the presentation
// boundary, and nowhere else. Nothing produced here flows back down, and no
// compiler flattens its own alternatives.

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

godot::String describe(const Hex12CompilationError &p_error) {
    switch (p_error.code) {
        case Hex12CompilationErrorCode::empty_orientation_set:
            return "no orientations were requested";
        case Hex12CompilationErrorCode::unsupported_polygon:
            return "its source polygon is not a hex-12 polygon";
        case Hex12CompilationErrorCode::unsupported_orientation:
            return "an unsupported orientation was requested";
        case Hex12CompilationErrorCode::coordinate_overflow:
            return "building a phase overflowed the coordinate range";
        case Hex12CompilationErrorCode::boundary_did_not_close:
            return "a compiled boundary did not close";
        case Hex12CompilationErrorCode::polygon_construction_failed:
            return p_error.polygon_error.has_value()
                ? describe(p_error.polygon_error.value())
                : godot::String("a compiled boundary was invalid");
        case Hex12CompilationErrorCode::prototile_construction_failed:
            return "its reference prototile could not be built";
        case Hex12CompilationErrorCode::normalization_overflow:
            return "normalizing a compiled boundary overflowed";
    }
    return "unknown hex-12 failure";
}

godot::String describe(const content::CanonicalOrientationCompilationError &p_error) {
    switch (p_error.code) {
        case content::CanonicalOrientationCompilationErrorCode::unsupported_domain:
            return "the geometry domain is not supported";
        case content::CanonicalOrientationCompilationErrorCode::
            prototile_unavailable_in_domain:
            return "it is not playable in this geometry domain";
        case content::CanonicalOrientationCompilationErrorCode::lattice_compilation_failed:
            return p_error.lattice_error.has_value()
                ? describe(p_error.lattice_error.value())
                : godot::String("unknown orientation failure");
        case content::CanonicalOrientationCompilationErrorCode::hex12_compilation_failed:
            return p_error.hex12_error.has_value()
                ? describe(p_error.hex12_error.value())
                : godot::String("unknown hex-12 failure");
    }
    return "unknown orientation failure";
}

godot::String describe(const engine::PaletteEntryCompilationError &p_error) {
    switch (p_error.code) {
        case engine::PaletteEntryCompilationErrorCode::empty_orientations:
            return "it compiled to no orientations";
        case engine::PaletteEntryCompilationErrorCode::inconsistent_prototile_id:
            return "its compiled orientations name different prototiles";
        case engine::PaletteEntryCompilationErrorCode::inconsistent_reference_boundary:
            return "its compiled orientations carry different reference geometry";
        case engine::PaletteEntryCompilationErrorCode::representatives_not_strictly_ordered:
            return "its compiled orientations are not in strict angular order";
        case engine::PaletteEntryCompilationErrorCode::duplicate_orientation_label:
            return "two compiled orientations claim the same angle";
        case engine::PaletteEntryCompilationErrorCode::duplicate_canonical_boundary:
            return "two compiled orientations share one boundary";
    }
    return "unknown palette entry failure";
}

godot::String describe(const PaletteResourceError &p_error) {
    godot::String prefix;
    if (p_error.entry.has_value()) {
        prefix = "palette entry " + number(p_error.entry.value()) + " ";
    }
    switch (p_error.code) {
        case PaletteResourceErrorCode::missing_resource:
            return "the palette is missing";
        case PaletteResourceErrorCode::unsupported_geometry_domain:
            return "the geometry domain is not supported";
        case PaletteResourceErrorCode::missing_entry:
            return prefix + "is missing";
        case PaletteResourceErrorCode::negative_prototile_id:
            return prefix + "has a negative prototile id";
        case PaletteResourceErrorCode::unknown_prototile_id:
            return prefix + "names a prototile id which is not in the catalog";
        case PaletteResourceErrorCode::prototile_unavailable_in_domain:
            return prefix + "names a prototile which this geometry domain does not admit";
        case PaletteResourceErrorCode::invalid_supply:
            return prefix + "has a supply which is neither -1 nor positive";
        case PaletteResourceErrorCode::orientation_compilation_failed:
            return prefix + "failed orientation compilation: "
                + (p_error.orientation_error.has_value()
                        ? describe(p_error.orientation_error.value())
                        : godot::String("unknown orientation failure"));
        case PaletteResourceErrorCode::palette_entry_construction_failed:
            return prefix + "could not be published: "
                + (p_error.palette_entry_error.has_value()
                        ? describe(p_error.palette_entry_error.value())
                        : godot::String("unknown palette entry failure"));
        case PaletteResourceErrorCode::palette_construction_failed:
            return p_error.palette_error.has_value()
                ? describe(p_error.palette_error.value())
                : godot::String("the palette is invalid");
    }
    return "unknown palette failure";
}

godot::String describe(PlacementError p_error) {
    switch (p_error) {
        case PlacementError::footprint_overflow:
            return "its footprint leaves the representable coordinate range";
        case PlacementError::footprint_construction_failed:
            return "its footprint is not a simple polygon";
    }
    return "unknown placement failure";
}

godot::String describe(const ArrangementError &p_error) {
    switch (p_error.code) {
        case ArrangementErrorCode::interior_overlap:
            return p_error.conflicting_placement.has_value()
                ? "it overlaps placement "
                    + number(p_error.conflicting_placement.value().value())
                : godot::String("it overlaps another placement");
        case ArrangementErrorCode::identifier_exhausted:
            return "the arrangement has no identity left to allocate";
    }
    return "unknown arrangement failure";
}

godot::String describe(const engine::BlueprintCompilationError &p_error) {
    godot::String prefix = "placement " + number(p_error.placement) + " ";
    switch (p_error.code) {
        case engine::BlueprintCompilationErrorCode::prototile_not_in_palette:
            return prefix + godot::String("names a tile which is not in this palette");
        case engine::BlueprintCompilationErrorCode::orientation_not_in_palette:
            return prefix
                + godot::String("names an orientation this palette does not compile");
        case engine::BlueprintCompilationErrorCode::supply_exhausted:
            return prefix + godot::String("has no remaining supply");
        case engine::BlueprintCompilationErrorCode::placement_construction_failed:
            return prefix
                + (p_error.placement_error.has_value()
                        ? describe(p_error.placement_error.value())
                        : godot::String("could not be constructed"));
        case engine::BlueprintCompilationErrorCode::arrangement_insertion_failed:
            return prefix
                + (p_error.arrangement_error.has_value()
                        ? describe(p_error.arrangement_error.value())
                        : godot::String("could not be inserted"));
    }
    return "unknown blueprint failure";
}

} // namespace

// --- registration ---

void LevelEditor::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_lattice_pressed"), &LevelEditor::on_lattice_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_hex12_pressed"), &LevelEditor::on_hex12_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_choose_domain_pressed"),
        &LevelEditor::on_choose_domain_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_build_palette_pressed"),
        &LevelEditor::on_build_palette_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_rotate_pressed"), &LevelEditor::on_rotate_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_clear_blueprint_pressed"),
        &LevelEditor::on_clear_blueprint_pressed);

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

    godot::ClassDB::bind_method(
        godot::D_METHOD("on_entry_pressed", "entry"), &LevelEditor::on_entry_pressed);
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
    palette_title_ = godot::Object::cast_to<godot::Label>(require(PALETTE_TITLE_PATH));
    palette_scroll_ = godot::Object::cast_to<godot::Control>(require(PALETTE_SCROLL_PATH));
    rows_container_ =
        godot::Object::cast_to<godot::VBoxContainer>(require(PALETTE_ROWS_PATH));
    entry_title_ = godot::Object::cast_to<godot::Label>(require(ENTRY_TITLE_PATH));
    entry_scroll_ = godot::Object::cast_to<godot::Control>(require(ENTRY_SCROLL_PATH));
    entries_container_ =
        godot::Object::cast_to<godot::VBoxContainer>(require(ENTRY_ROWS_PATH));
    instruction_label_ = godot::Object::cast_to<godot::Label>(require(INSTRUCTION_PATH));
    status_label_ = godot::Object::cast_to<godot::Label>(require(STATUS_PATH));
    selection_label_ = godot::Object::cast_to<godot::Label>(require(SELECTION_PATH));
    phase_label_ = godot::Object::cast_to<godot::Label>(require(PHASE_LABEL_PATH));

    lattice_button_ = require_action(LATTICE_BUTTON);
    hex12_button_ = require_action(HEX12_BUTTON);
    choose_domain_button_ = require_action(CHOOSE_DOMAIN_BUTTON);
    build_palette_button_ = require_action(BUILD_PALETTE_BUTTON);
    rotate_button_ = require_action(ROTATE_BUTTON);
    clear_blueprint_button_ = require_action(CLEAR_BLUEPRINT_BUTTON);

    // Every required child is reported once, together, rather than as a cascade
    // of null dereferences.
    if (!missing.is_empty()) {
        godot::UtilityFunctions::push_error(
            "[tiles] level editor initialization failed: missing or mistyped scene "
            "children: ",
            missing);
        return false;
    }

    lattice_button_->connect(
        godot::StringName("pressed"),
        godot::Callable(this, godot::StringName("on_lattice_pressed")));
    hex12_button_->connect(
        godot::StringName("pressed"),
        godot::Callable(this, godot::StringName("on_hex12_pressed")));
    choose_domain_button_->connect(
        godot::StringName("pressed"),
        godot::Callable(this, godot::StringName("on_choose_domain_pressed")));
    build_palette_button_->connect(
        godot::StringName("pressed"),
        godot::Callable(this, godot::StringName("on_build_palette_pressed")));
    rotate_button_->connect(
        godot::StringName("pressed"),
        godot::Callable(this, godot::StringName("on_rotate_pressed")));
    clear_blueprint_button_->connect(
        godot::StringName("pressed"),
        godot::Callable(this, godot::StringName("on_clear_blueprint_pressed")));

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

    set_focus_mode(godot::Control::FOCUS_ALL);
    update_canvas_rect();
    center_camera_on_origin();
    set_status("choose a geometry domain");
    refresh_controls();
    grab_focus();
    queue_redraw();

    godot::UtilityFunctions::print(
        "[tiles] level editor ready: ", number(catalog_->entries().size()),
        " canonical identities, choose lattice or hex12");
}

// --- phase one: the geometry domain ---

bool LevelEditor::choose_domain(content::GeometryDomain p_domain) {
    if (!catalog_.has_value()) {
        set_status("cannot begin: the canonical catalog is unavailable");
        return false;
    }
    if (!is_supported_domain(p_domain)) {
        // A value outside the enumeration installs nothing: there is no fallback
        // domain and no partially installed document.
        set_status("that geometry domain is not supported");
        refresh_controls();
        return false;
    }

    // Choosing a domain always discards the whole current document, including a
    // locked palette and a complete blueprint.
    Document document;
    document.domain = p_domain;

    const std::vector<const content::CanonicalPrototile *> entries =
        catalog_->entries_for(p_domain);
    document.rows.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        PaletteRow row;
        row.catalog_index = index;
        row.included = false;
        row.unlimited = true;
        row.finite_amount = MIN_FINITE_SUPPLY;
        row.color = default_row_color(index);
        document.rows.push_back(row);
    }

    document_ = std::move(document);

    proposals_.clear();
    active_proposal_.reset();
    pointer_.reset();
    pan_anchor_.reset();

    clear_entry_rows();
    build_palette_rows();
    sync_all_row_controls();

    update_canvas_rect();
    center_camera_on_origin();

    set_status(
        godot::String("domain ") + domain_name(p_domain) + ": "
        + number(entries.size()) + " tiles available");
    refresh_controls();
    queue_redraw();
    return true;
}

void LevelEditor::return_to_domain_choice() {
    document_.reset();
    proposals_.clear();
    active_proposal_.reset();
    pointer_.reset();
    pan_anchor_.reset();

    clear_palette_rows();
    clear_entry_rows();

    update_canvas_rect();
    center_camera_on_origin();

    set_status("choose a geometry domain");
    refresh_controls();
    queue_redraw();
}

// --- phase two: the palette ---

void LevelEditor::build_palette_rows() {
    clear_palette_rows();
    if (rows_container_ == nullptr || !catalog_.has_value() || !document_.has_value()) {
        return;
    }

    const std::vector<const content::CanonicalPrototile *> entries =
        catalog_->entries_for(document_->domain);
    row_controls_.reserve(entries.size());

    for (std::size_t index = 0; index < entries.size(); ++index) {
        godot::HBoxContainer *row = memnew(godot::HBoxContainer);
        row->set_name(godot::String("Row") + number(index));
        rows_container_->add_child(row);

        RowControls controls;

        controls.include = memnew(godot::CheckBox);
        controls.include->set_name("Include");
        controls.include->set_tooltip_text("include this tile in the palette");
        row->add_child(controls.include);

        controls.preview = memnew(PrototilePreview);
        controls.preview->set_name("Preview");
        controls.preview->set_custom_minimum_size(godot::Vector2(40.0f, 40.0f));
        controls.preview->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
        controls.preview->set_polygon(entries[index]->prototile().polygon());
        row->add_child(controls.preview);

        godot::VBoxContainer *body = memnew(godot::VBoxContainer);
        body->set_name("Body");
        body->set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
        row->add_child(body);

        controls.name = memnew(godot::Label);
        controls.name->set_name("Name");
        controls.name->set_text(godot::String(entries[index]->display_name().c_str()));
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
        controls.amount->set_tooltip_text("how many of this tile the palette provides");
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

void LevelEditor::clear_palette_rows() {
    row_controls_.clear();
    if (rows_container_ == nullptr) {
        return;
    }
    // Detached immediately so the visible row count never lags the document, and
    // freed on the ordinary deferred path.
    while (rows_container_->get_child_count() > 0) {
        godot::Node *child = rows_container_->get_child(0);
        rows_container_->remove_child(child);
        child->queue_free();
    }
}

void LevelEditor::set_row_included(std::size_t p_row, bool p_included) {
    if (!document_.has_value() || document_->palette.has_value()
        || p_row >= document_->rows.size()) {
        return;
    }
    document_->rows[p_row].included = p_included;
    sync_row_controls(p_row);
    refresh_controls();
}

void LevelEditor::set_row_unlimited(std::size_t p_row, bool p_unlimited) {
    if (!document_.has_value() || document_->palette.has_value()
        || p_row >= document_->rows.size()) {
        return;
    }
    document_->rows[p_row].unlimited = p_unlimited;
    sync_row_controls(p_row);
}

void LevelEditor::set_row_finite_amount(std::size_t p_row, std::int64_t p_amount) {
    if (!document_.has_value() || document_->palette.has_value()
        || p_row >= document_->rows.size()) {
        return;
    }
    document_->rows[p_row].finite_amount = std::min<std::int64_t>(
        std::max<std::int64_t>(p_amount, MIN_FINITE_SUPPLY), MAX_FINITE_SUPPLY);
    sync_row_controls(p_row);
}

void LevelEditor::set_row_color(std::size_t p_row, const godot::Color &p_color) {
    if (!document_.has_value() || document_->palette.has_value()
        || p_row >= document_->rows.size()) {
        return;
    }
    document_->rows[p_row].color = opaque(p_color);
    sync_row_controls(p_row);
}

void LevelEditor::sync_row_controls(std::size_t p_row) {
    if (!document_.has_value() || p_row >= document_->rows.size()
        || p_row >= row_controls_.size()) {
        return;
    }
    const PaletteRow &row = document_->rows[p_row];
    const RowControls &controls = row_controls_[p_row];
    const bool locked = document_->palette.has_value();

    suppress_row_signals_ = true;
    if (controls.include != nullptr) {
        controls.include->set_pressed_no_signal(row.included);
        controls.include->set_disabled(locked);
    }
    if (controls.supply != nullptr) {
        controls.supply->select(row.unlimited ? SUPPLY_ITEM_UNLIMITED : SUPPLY_ITEM_FINITE);
        controls.supply->set_disabled(locked || !row.included);
    }
    if (controls.amount != nullptr) {
        controls.amount->set_value_no_signal(static_cast<double>(row.finite_amount));
        controls.amount->set_editable(!locked && row.included && !row.unlimited);
    }
    if (controls.color != nullptr) {
        controls.color->set_pick_color(row.color);
        controls.color->set_disabled(locked || !row.included);
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

bool LevelEditor::build_palette() {
    if (!document_.has_value() || !catalog_.has_value()) {
        set_status("choose a geometry domain first");
        return false;
    }
    if (document_->palette.has_value()) {
        // Locked. There are no live palette edits in blueprint phase.
        set_status("the palette is locked; return to the domain choice to start over");
        return false;
    }

    const std::vector<const content::CanonicalPrototile *> entries =
        catalog_->entries_for(document_->domain);

    godot::Ref<PaletteResource> candidate;
    candidate.instantiate();
    godot::TypedArray<PaletteEntryResource> authored;
    std::vector<godot::Color> colors;
    for (const PaletteRow &row : document_->rows) {
        if (!row.included || row.catalog_index >= entries.size()) {
            continue;
        }
        godot::Ref<PaletteEntryResource> entry;
        entry.instantiate();
        entry->set_prototile_id(static_cast<std::int64_t>(
            entries[row.catalog_index]->prototile().id().value()));
        entry->set_supply(row.unlimited ? -1 : row.finite_amount);
        entry->set_color(opaque(row.color));
        authored.push_back(entry);
        colors.push_back(opaque(row.color));
    }
    candidate->set_entries(authored);

    if (authored.is_empty()) {
        set_status("choose at least one tile before building the palette");
        refresh_controls();
        return false;
    }

    auto compiled =
        compile_palette_resource(document_->domain, candidate, catalog_.value());
    if (!compiled) {
        set_status("the palette could not be built: " + describe(compiled.error()));
        refresh_controls();
        return false;
    }

    document_->palette = std::move(compiled).value();
    document_->colors = std::move(colors);
    document_->records.clear();
    document_->arrangement = Arrangement();
    document_->selection = Selection { 0, 0 };

    build_entry_rows();
    sync_all_row_controls();
    rebuild_proposals();

    set_status(
        "palette locked: " + number(document_->palette->order())
        + " tile types — click to place the first tile at the origin");
    refresh_controls();
    queue_redraw();
    return true;
}

// --- phase three: the blueprint ---

void LevelEditor::build_entry_rows() {
    clear_entry_rows();
    if (entries_container_ == nullptr || !document_.has_value()
        || !document_->palette.has_value() || !catalog_.has_value()) {
        return;
    }

    const std::vector<engine::PaletteEntry> &entries = document_->palette->entries();
    entry_controls_.reserve(entries.size());

    for (std::size_t index = 0; index < entries.size(); ++index) {
        godot::HBoxContainer *row = memnew(godot::HBoxContainer);
        row->set_name(godot::String("Entry") + number(index));
        entries_container_->add_child(row);

        EntryControls controls;

        controls.swatch = memnew(godot::ColorRect);
        controls.swatch->set_name("Swatch");
        controls.swatch->set_custom_minimum_size(godot::Vector2(16.0f, 16.0f));
        controls.swatch->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
        const std::optional<godot::Color> color = entry_color(index);
        controls.swatch->set_color(
            color.has_value() ? color.value() : godot::Color(1.0f, 1.0f, 1.0f, 1.0f));
        row->add_child(controls.swatch);

        controls.preview = memnew(PrototilePreview);
        controls.preview->set_name("Preview");
        controls.preview->set_custom_minimum_size(godot::Vector2(40.0f, 40.0f));
        controls.preview->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
        controls.preview->set_polygon(entries[index].prototile().polygon());
        row->add_child(controls.preview);

        controls.select = memnew(godot::Button);
        controls.select->set_name("Select");
        controls.select->set_h_size_flags(godot::Control::SIZE_EXPAND_FILL);
        const content::CanonicalPrototile *canonical =
            catalog_->find(entries[index].prototile().id());
        const godot::String label = canonical != nullptr
            ? godot::String(canonical->display_name().c_str())
            : godot::String("tile ") + number(entries[index].prototile().id().value());
        controls.select->set_text(label);
        row->add_child(controls.select);

        controls.supply = memnew(godot::Label);
        controls.supply->set_name("Supply");
        row->add_child(controls.supply);

        controls.select->connect(
            godot::StringName("pressed"),
            godot::Callable(this, godot::StringName("on_entry_pressed"))
                .bind(static_cast<std::int64_t>(index)));

        entry_controls_.push_back(controls);
    }

    sync_entry_rows();
}

void LevelEditor::clear_entry_rows() {
    entry_controls_.clear();
    if (entries_container_ == nullptr) {
        return;
    }
    while (entries_container_->get_child_count() > 0) {
        godot::Node *child = entries_container_->get_child(0);
        entries_container_->remove_child(child);
        child->queue_free();
    }
}

void LevelEditor::sync_entry_rows() {
    if (!document_.has_value() || !document_->palette.has_value()) {
        return;
    }
    const std::vector<engine::PaletteEntry> &entries = document_->palette->entries();
    for (std::size_t index = 0; index < entry_controls_.size() && index < entries.size();
         ++index) {
        const EntryControls &controls = entry_controls_[index];
        const bool selected = document_->selection.has_value()
            && document_->selection->entry == index;

        if (controls.supply != nullptr) {
            const std::optional<engine::Supply::Amount> remaining =
                remaining_supply(index);
            controls.supply->set_text(
                remaining.has_value() ? number(remaining.value()) + " left"
                                      : godot::String("unlimited"));
        }
        if (controls.preview != nullptr) {
            // The selected row shows the exact variant a click would place; every
            // other row shows its identity's reference geometry.
            const OrientedPrototile *variant = selected ? selected_variant() : nullptr;
            controls.preview->set_polygon(
                variant != nullptr ? variant->canonical_polygon()
                                   : entries[index].prototile().polygon());
        }
        if (controls.select != nullptr) {
            controls.select->set_flat(!selected);
        }
    }
}

void LevelEditor::select_entry(std::size_t p_entry) {
    if (!document_.has_value() || !document_->palette.has_value()) {
        return;
    }
    if (p_entry >= document_->palette->entries().size()) {
        return;
    }
    // Changing entries resets the orientation, because an orientation index is
    // only meaningful within one entry's distinct set.
    document_->selection = Selection { p_entry, 0 };
    rebuild_proposals();
    sync_entry_rows();
    refresh_controls();
    queue_redraw();
}

void LevelEditor::cycle_entry(bool p_forward) {
    if (!document_.has_value() || !document_->palette.has_value()
        || !document_->selection.has_value()) {
        return;
    }
    const std::size_t count = document_->palette->entries().size();
    if (count == 0) {
        return;
    }
    const std::size_t current =
        document_->selection->entry < count ? document_->selection->entry : 0;
    select_entry(p_forward ? (current + 1) % count : (current + count - 1) % count);
}

void LevelEditor::cycle_orientation(bool p_forward) {
    const engine::PaletteEntry *entry = selected_entry();
    if (entry == nullptr) {
        return;
    }
    // Only the selected entry's distinct compiled orientations.
    // equivalent_orientations() records which requested angles collapsed onto one
    // boundary and is never iterated as though its labels were separate geometry.
    const std::size_t count = entry->orientations().size();
    if (count == 0) {
        return;
    }
    const std::size_t current = document_->selection->orientation < count
        ? document_->selection->orientation
        : 0;
    document_->selection->orientation =
        p_forward ? (current + 1) % count : (current + count - 1) % count;

    rebuild_proposals();
    sync_entry_rows();
    refresh_controls();
    queue_redraw();
}

void LevelEditor::rebuild_proposals() {
    proposals_.clear();
    active_proposal_.reset();

    const OrientedPrototile *candidate = selected_variant();
    if (candidate == nullptr || !document_.has_value()) {
        return;
    }

    const PrototileId id = candidate->prototile().id();
    const Orientation orientation = candidate->orientation();
    const Arrangement &arrangement = document_->arrangement;

    if (arrangement.entries().empty()) {
        // The first placement needs no pointer-derived model coordinate: absolute
        // blueprint position carries no meaning, so exact origin is the whole
        // offer.
        const Point origin { Coordinate::from_raw(0), Coordinate::from_raw(0) };
        auto placement = Placement::make(*candidate, origin);
        if (placement) {
            proposals_.push_back(Proposal {
                engine::BlueprintPlacement { id, orientation, origin },
                std::move(placement).value(),
            });
        }
        update_active_proposal();
        return;
    }

    // Every polygon edge is the cyclic edge beginning at one stored vertex, so a
    // polygon has exactly as many edge indices as vertices. Nothing here assumes
    // a vertex count, an edge length, or an orientation count: the bounds come
    // from the actual candidate and each actual anchor footprint. An ordinarily
    // rejected feature pair is not an error and is never logged.
    const std::size_t candidate_features =
        candidate->canonical_polygon().vertices().size();

    std::vector<Proposal> proven;
    const auto keep = [&](Placement p_placement) {
        proven.push_back(Proposal {
            engine::BlueprintPlacement { id, orientation, p_placement.translation() },
            std::move(p_placement),
        });
    };

    for (const Entry &entry : arrangement.entries()) {
        const std::size_t anchor_features =
            entry.placement.footprint().vertices().size();
        for (std::size_t anchor_edge = 0; anchor_edge < anchor_features; ++anchor_edge) {
            for (std::size_t edge = 0; edge < candidate_features; ++edge) {
                auto previewed = arrangement.preview_join_full_edges(
                    entry.id, EdgeIndex(anchor_edge), *candidate, EdgeIndex(edge));
                if (previewed) {
                    keep(std::move(previewed).value());
                }
            }
        }
    }

    for (const Entry &entry : arrangement.entries()) {
        const std::size_t anchor_features =
            entry.placement.footprint().vertices().size();
        for (std::size_t anchor_vertex = 0; anchor_vertex < anchor_features;
             ++anchor_vertex) {
            for (std::size_t vertex = 0; vertex < candidate_features; ++vertex) {
                auto previewed = arrangement.preview_join_vertices(
                    entry.id, VertexIndex(anchor_vertex), *candidate, VertexIndex(vertex));
                if (previewed) {
                    keep(std::move(previewed).value());
                }
            }
        }
    }

    // One selection fixes identity and representative orientation for this whole
    // rebuild, so two proofs describe the same physical proposal exactly when
    // their exact translations agree — compared on the q16.48 lattice, never on a
    // projected float. First discovery order is retained.
    std::vector<Proposal> distinct;
    for (Proposal &proposal : proven) {
        bool duplicate = false;
        for (const Proposal &kept : distinct) {
            if (kept.record.prototile_id == proposal.record.prototile_id
                && kept.record.orientation == proposal.record.orientation
                && kept.record.translation == proposal.record.translation) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            distinct.push_back(std::move(proposal));
        }
    }

    // Only placements which actually abut the blueprint are offered. The contact
    // test depends on the placement alone, so applying it after deduplication is
    // equivalent and far cheaper.
    for (Proposal &proposal : distinct) {
        if (shares_edge_contact(proposal.placement.footprint(), arrangement)) {
            proposals_.push_back(std::move(proposal));
        }
    }

    update_active_proposal();
}

bool LevelEditor::update_active_proposal() {
    const std::optional<std::size_t> previous = active_proposal_;

    std::optional<std::size_t> chosen;
    if (!proposals_.empty()) {
        if (pointer_.has_value()) {
            const godot::Vector2 pointer = pointer_.value();
            double best = 0.0;
            for (std::size_t i = 0; i < proposals_.size(); ++i) {
                // One presentation-only handle per proposal: the arithmetic mean
                // of its projected footprint vertices. For one fixed selected
                // orientation the local mean offset is constant, so distinct
                // exact translations produce correspondingly translated handles,
                // which is all ranking needs.
                const Polygon::Vertices &vertices =
                    proposals_[i].placement.footprint().vertices();
                double sum_x = 0.0;
                double sum_y = 0.0;
                for (const Point &vertex : vertices) {
                    const godot::Vector2 projected = to_screen(vertex);
                    sum_x += static_cast<double>(projected.x);
                    sum_y += static_cast<double>(projected.y);
                }
                const double count = static_cast<double>(vertices.size());
                const double dx =
                    sum_x / count - static_cast<double>(pointer.x);
                const double dy =
                    sum_y / count - static_cast<double>(pointer.y);
                const double distance = dx * dx + dy * dy;
                // Strictly nearer only, so a floating-point tie keeps the earlier
                // proposal and the deterministic discovery order decides.
                if (!chosen.has_value() || distance < best) {
                    chosen = i;
                    best = distance;
                }
            }
        } else {
            // No pointer has been seen yet. The first proposal in discovery order
            // is the deterministic choice; nothing about it is invented.
            chosen = 0;
        }
    }

    active_proposal_ = chosen;
    return active_proposal_ != previous;
}

bool LevelEditor::publish(std::vector<engine::BlueprintPlacement> p_candidate) {
    if (!document_.has_value() || !document_->palette.has_value()) {
        return false;
    }

    auto compiled = compile_blueprint(document_->palette.value(), p_candidate);
    if (!compiled) {
        // Every offered proposal was proven against this exact arrangement and
        // nothing mutates between the proof and the click, so this is unexpected.
        // It is reported in full rather than asserted away, and the current
        // records and arrangement survive untouched.
        godot::UtilityFunctions::push_error(
            "[tiles] level editor: a candidate blueprint failed to compile: ",
            describe(compiled.error()));
        set_status("that change could not be published: " + describe(compiled.error()));
        refresh_controls();
        return false;
    }

    document_->records = std::move(p_candidate);
    document_->arrangement = std::move(compiled).value();

    rebuild_proposals();
    sync_entry_rows();
    refresh_controls();
    queue_redraw();
    return true;
}

bool LevelEditor::accept_active_proposal() {
    if (!document_.has_value() || !document_->palette.has_value()) {
        return false;
    }
    if (!active_proposal_.has_value() || active_proposal_.value() >= proposals_.size()) {
        set_status("no placement is offered here");
        return false;
    }
    if (!document_->selection.has_value()) {
        return false;
    }

    const std::optional<engine::Supply::Amount> remaining =
        remaining_supply(document_->selection->entry);
    if (remaining.has_value() && remaining.value() == 0) {
        set_status("no more of that tile remains");
        return false;
    }

    std::vector<engine::BlueprintPlacement> candidate = document_->records;
    candidate.push_back(proposals_[active_proposal_.value()].record);
    if (!publish(std::move(candidate))) {
        return false;
    }
    set_status("placed: " + number(document_->records.size()) + " tiles");
    return true;
}

bool LevelEditor::remove_record(std::size_t p_record) {
    if (!document_.has_value() || !document_->palette.has_value()) {
        return false;
    }
    if (p_record >= document_->records.size()) {
        return false;
    }

    std::vector<engine::BlueprintPlacement> candidate;
    candidate.reserve(document_->records.size() - 1);
    for (std::size_t index = 0; index < document_->records.size(); ++index) {
        if (index != p_record) {
            candidate.push_back(document_->records[index]);
        }
    }

    if (!publish(std::move(candidate))) {
        return false;
    }
    set_status("removed: " + number(document_->records.size()) + " tiles");
    return true;
}

void LevelEditor::clear_blueprint() {
    if (!document_.has_value() || !document_->palette.has_value()) {
        return;
    }
    if (!publish(std::vector<engine::BlueprintPlacement>())) {
        return;
    }
    set_status("blueprint cleared");
}

// --- pointer ---

void LevelEditor::set_pointer(godot::Vector2 p_local) {
    pointer_ = p_local;
    if (update_active_proposal()) {
        queue_redraw();
    }
}

void LevelEditor::clear_pointer() {
    if (!pointer_.has_value()) {
        return;
    }
    pointer_.reset();
    if (update_active_proposal()) {
        queue_redraw();
    }
}

std::optional<std::size_t> LevelEditor::record_at_local(godot::Vector2 p_local) const {
    if (!document_.has_value()) {
        return std::nullopt;
    }
    const std::vector<Entry> &entries = document_->arrangement.entries();
    // Storage order is draw order, so the last containing entry is the topmost
    // rendered one. Record index and entry index agree: compilation inserts the
    // records in stored order and every insertion succeeded.
    for (std::size_t offset = entries.size(); offset > 0; --offset) {
        const std::size_t index = offset - 1;
        godot::PackedVector2Array points;
        for (const Point &vertex : entries[index].placement.footprint().vertices()) {
            points.push_back(to_screen(vertex));
        }
        if (screen_polygon_contains(points, p_local)) {
            return index;
        }
    }
    return std::nullopt;
}

// --- observation ---

LevelEditor::EditorPhase LevelEditor::phase() const {
    if (!document_.has_value()) {
        return EditorPhase::choose_domain;
    }
    return document_->palette.has_value() ? EditorPhase::build_blueprint
                                          : EditorPhase::choose_palette;
}

std::optional<content::GeometryDomain> LevelEditor::domain() const {
    if (!document_.has_value()) {
        return std::nullopt;
    }
    return document_->domain;
}

const content::PrototileCatalog *LevelEditor::catalog() const {
    return catalog_.has_value() ? &catalog_.value() : nullptr;
}

std::vector<const content::CanonicalPrototile *> LevelEditor::domain_entries() const {
    if (!catalog_.has_value() || !document_.has_value()) {
        return {};
    }
    return catalog_->entries_for(document_->domain);
}

const std::vector<LevelEditor::PaletteRow> &LevelEditor::palette_rows() const {
    static const std::vector<PaletteRow> empty;
    return document_.has_value() ? document_->rows : empty;
}

const engine::Palette *LevelEditor::palette() const {
    if (!document_.has_value() || !document_->palette.has_value()) {
        return nullptr;
    }
    return &document_->palette.value();
}

std::optional<godot::Color> LevelEditor::entry_color(std::size_t p_entry) const {
    if (!document_.has_value() || p_entry >= document_->colors.size()) {
        return std::nullopt;
    }
    return document_->colors[p_entry];
}

const std::vector<engine::BlueprintPlacement> &LevelEditor::blueprint() const {
    static const std::vector<engine::BlueprintPlacement> empty;
    return document_.has_value() ? document_->records : empty;
}

const Arrangement *LevelEditor::arrangement() const {
    return document_.has_value() ? &document_->arrangement : nullptr;
}

std::optional<LevelEditor::Selection> LevelEditor::selection() const {
    if (!document_.has_value()) {
        return std::nullopt;
    }
    return document_->selection;
}

const engine::PaletteEntry *LevelEditor::selected_entry() const {
    if (!document_.has_value() || !document_->palette.has_value()
        || !document_->selection.has_value()) {
        return nullptr;
    }
    const std::vector<engine::PaletteEntry> &entries = document_->palette->entries();
    if (document_->selection->entry >= entries.size()) {
        return nullptr;
    }
    return &entries[document_->selection->entry];
}

const OrientedPrototile *LevelEditor::selected_variant() const {
    const engine::PaletteEntry *entry = selected_entry();
    if (entry == nullptr) {
        return nullptr;
    }
    const std::vector<OrientedPrototile> &orientations = entry->orientations();
    if (document_->selection->orientation >= orientations.size()) {
        return nullptr;
    }
    return &orientations[document_->selection->orientation];
}

std::optional<engine::Supply::Amount> LevelEditor::remaining_supply(
    std::size_t p_entry) const {
    if (!document_.has_value() || !document_->palette.has_value()) {
        return std::nullopt;
    }
    const std::vector<engine::PaletteEntry> &entries = document_->palette->entries();
    if (p_entry >= entries.size()) {
        return std::nullopt;
    }
    const std::optional<engine::Supply::Amount> amount =
        entries[p_entry].supply().finite_amount();
    if (!amount.has_value()) {
        return std::nullopt;
    }

    // Derived from the blueprint records, never decremented anywhere.
    const PrototileId id = entries[p_entry].prototile().id();
    engine::Supply::Amount used = 0;
    for (const engine::BlueprintPlacement &record : document_->records) {
        if (record.prototile_id == id) {
            ++used;
        }
    }
    return used >= amount.value() ? 0 : amount.value() - used;
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

godot::Button *LevelEditor::entry_select_control(std::size_t p_entry) const {
    return p_entry < entry_controls_.size() ? entry_controls_[p_entry].select : nullptr;
}

godot::Label *LevelEditor::entry_supply_control(std::size_t p_entry) const {
    return p_entry < entry_controls_.size() ? entry_controls_[p_entry].supply : nullptr;
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

void LevelEditor::refresh_instructions() {
    if (instruction_label_ == nullptr) {
        return;
    }
    const godot::String navigation = "\nmiddle-drag pans · wheel zooms";
    switch (phase()) {
        case EditorPhase::choose_domain:
            instruction_label_->set_text(
                godot::String("choose a geometry domain: lattice or hex12.") + navigation);
            return;
        case EditorPhase::choose_palette:
            instruction_label_->set_text(
                godot::String("include tiles, set supply and color, then build the "
                              "palette. changing the domain discards everything.")
                + navigation);
            return;
        case EditorPhase::build_blueprint:
            instruction_label_->set_text(
                godot::String("left click places the ghosted tile · right click removes "
                              "one · tab cycles tiles · r rotates")
                + navigation);
            return;
    }
}

void LevelEditor::refresh_selection_label() {
    if (selection_label_ == nullptr) {
        return;
    }
    const engine::PaletteEntry *entry = selected_entry();
    const OrientedPrototile *variant = selected_variant();
    if (entry == nullptr || variant == nullptr || !document_.has_value()
        || !document_->palette.has_value()) {
        selection_label_->set_text("—");
        return;
    }

    const std::optional<engine::Supply::Amount> remaining =
        remaining_supply(document_->selection->entry);
    selection_label_->set_text(
        "tile " + number(document_->selection->entry + 1) + "/"
        + number(document_->palette->order()) + " · orientation "
        + number(document_->selection->orientation + 1) + "/"
        + number(entry->orientations().size()) + " · "
        + (remaining.has_value() ? number(remaining.value()) + " left"
                                 : godot::String("unlimited"))
        + " · " + number(document_->records.size()) + " placed");
}

void LevelEditor::refresh_controls() {
    const EditorPhase current = phase();
    const bool has_document = document_.has_value();

    if (lattice_button_ != nullptr) {
        lattice_button_->set_disabled(!catalog_.has_value());
    }
    if (hex12_button_ != nullptr) {
        hex12_button_->set_disabled(!catalog_.has_value());
    }
    if (choose_domain_button_ != nullptr) {
        choose_domain_button_->set_disabled(!has_document);
    }
    if (build_palette_button_ != nullptr) {
        build_palette_button_->set_disabled(current != EditorPhase::choose_palette);
    }
    if (rotate_button_ != nullptr) {
        rotate_button_->set_disabled(current != EditorPhase::build_blueprint);
    }
    if (clear_blueprint_button_ != nullptr) {
        clear_blueprint_button_->set_disabled(current != EditorPhase::build_blueprint);
    }

    if (palette_title_ != nullptr) {
        palette_title_->set_visible(current == EditorPhase::choose_palette);
    }
    if (palette_scroll_ != nullptr) {
        palette_scroll_->set_visible(current == EditorPhase::choose_palette);
    }
    if (entry_title_ != nullptr) {
        entry_title_->set_visible(current == EditorPhase::build_blueprint);
    }
    if (entry_scroll_ != nullptr) {
        entry_scroll_->set_visible(current == EditorPhase::build_blueprint);
    }

    if (phase_label_ != nullptr) {
        switch (current) {
            case EditorPhase::choose_domain:
                phase_label_->set_text("no document");
                break;
            case EditorPhase::choose_palette:
                phase_label_->set_text(
                    godot::String(domain_name(document_->domain)) + " · palette");
                break;
            case EditorPhase::build_blueprint:
                phase_label_->set_text(
                    godot::String(domain_name(document_->domain)) + " · blueprint");
                break;
        }
    }

    refresh_instructions();
    refresh_selection_label();
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

godot::Vector2 LevelEditor::to_screen(double p_x, double p_y) const {
    return godot::Vector2(
        static_cast<real_t>(
            static_cast<double>(camera_.origin_pixels.x) + camera_.pixels_per_unit * p_x),
        static_cast<real_t>(
            static_cast<double>(camera_.origin_pixels.y) - camera_.pixels_per_unit * p_y));
}

godot::Vector2 LevelEditor::to_screen(Point p_point) const {
    return to_screen(to_real(p_point.x), to_real(p_point.y));
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

    set_pointer(p_local);
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
        if (phase() != EditorPhase::build_blueprint) {
            return;
        }
        const godot::Key code = key->get_keycode();
        const bool backward = key->is_shift_pressed();
        if (code == godot::KEY_TAB) {
            cycle_entry(!backward);
            accept_event();
            return;
        }
        if (code == godot::KEY_R) {
            cycle_orientation(!backward);
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
            set_pointer(position);
            queue_redraw();
            accept_event();
            return;
        }
        if (canvas_rect_.has_point(position)) {
            set_pointer(position);
        } else {
            clear_pointer();
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

        if (phase() != EditorPhase::build_blueprint) {
            return;
        }

        if (index == godot::MOUSE_BUTTON_LEFT) {
            grab_focus();
            // The proposal already highlighted as the ghost is the one applied, so
            // a click adds exactly what was shown; the click's own position then
            // takes over ranking for the rebuilt set.
            accept_active_proposal();
            set_pointer(position);
            accept_event();
            return;
        }
        if (index == godot::MOUSE_BUTTON_RIGHT) {
            grab_focus();
            const std::optional<std::size_t> record = record_at_local(position);
            if (record.has_value()) {
                remove_record(record.value());
            } else {
                set_status("no placed tile is under the pointer");
            }
            set_pointer(position);
            accept_event();
            return;
        }
    }
}

// --- toolbar ---

void LevelEditor::on_lattice_pressed() {
    choose_domain(content::GeometryDomain::lattice);
    grab_focus();
}

void LevelEditor::on_hex12_pressed() {
    choose_domain(content::GeometryDomain::hex12);
    grab_focus();
}

void LevelEditor::on_choose_domain_pressed() {
    return_to_domain_choice();
    grab_focus();
}

void LevelEditor::on_build_palette_pressed() {
    build_palette();
    grab_focus();
}

void LevelEditor::on_rotate_pressed() {
    cycle_orientation(true);
    grab_focus();
}

void LevelEditor::on_clear_blueprint_pressed() {
    clear_blueprint();
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
    set_row_unlimited(static_cast<std::size_t>(p_row), p_index == SUPPLY_ITEM_UNLIMITED);
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

void LevelEditor::on_entry_pressed(std::int64_t p_entry) {
    if (p_entry < 0) {
        return;
    }
    select_entry(static_cast<std::size_t>(p_entry));
    grab_focus();
}

// --- drawing ---

void LevelEditor::draw_axes() {
    const double left = static_cast<double>(canvas_rect_.position.x);
    const double top = static_cast<double>(canvas_rect_.position.y);
    const double right = left + static_cast<double>(canvas_rect_.size.x);
    const double bottom = top + static_cast<double>(canvas_rect_.size.y);
    const godot::Vector2 origin = to_screen(0.0, 0.0);

    if (static_cast<double>(origin.x) >= left && static_cast<double>(origin.x) <= right) {
        draw_line(
            godot::Vector2(origin.x, static_cast<real_t>(top)),
            godot::Vector2(origin.x, static_cast<real_t>(bottom)),
            AXIS_LINE,
            AXIS_WIDTH);
    }
    if (static_cast<double>(origin.y) >= top && static_cast<double>(origin.y) <= bottom) {
        draw_line(
            godot::Vector2(static_cast<real_t>(left), origin.y),
            godot::Vector2(static_cast<real_t>(right), origin.y),
            AXIS_LINE,
            AXIS_WIDTH);
    }
}

void LevelEditor::draw_arrangement() {
    if (!document_.has_value() || !document_->palette.has_value()) {
        return;
    }

    for (const Entry &entry : document_->arrangement.entries()) {
        const Polygon &footprint = entry.placement.footprint();

        // One authored color per identity, found in locked palette order. The
        // exact certified triangulation is drawn directly, so concave footprints
        // fill correctly without a second triangulator.
        godot::Color fill(0.7f, 0.7f, 0.7f, 1.0f);
        const std::vector<engine::PaletteEntry> &entries = document_->palette->entries();
        for (std::size_t index = 0; index < entries.size(); ++index) {
            if (entries[index].prototile().id() == entry.placement.prototile().id()) {
                const std::optional<godot::Color> color = entry_color(index);
                if (color.has_value()) {
                    fill = color.value();
                }
                break;
            }
        }

        for (const Triangle &triangle : footprint.triangulation()) {
            godot::PackedVector2Array points;
            for (const Point &vertex : triangle.vertices) {
                points.push_back(to_screen(vertex));
            }
            draw_colored_polygon(points, fill);
        }

        godot::PackedVector2Array boundary;
        for (const Point &vertex : footprint.vertices()) {
            boundary.push_back(to_screen(vertex));
        }
        draw_polyline(closed_boundary(boundary), outline_for(fill), OUTLINE_WIDTH);
    }
}

void LevelEditor::draw_ghost() {
    if (!active_proposal_.has_value() || active_proposal_.value() >= proposals_.size()) {
        return;
    }
    if (!document_.has_value() || !document_->selection.has_value()) {
        return;
    }

    // Authoritative preview geometry, projected exactly like a placed footprint.
    // It is never a pointer-translated polygon: it jumps between exact
    // feature-derived placements as the active proposal changes.
    const Placement &placement = proposals_[active_proposal_.value()].placement;

    godot::Color fill(0.9f, 0.9f, 0.9f, 1.0f);
    const std::optional<godot::Color> color = entry_color(document_->selection->entry);
    if (color.has_value()) {
        fill = color.value();
    }
    fill.a = GHOST_ALPHA;

    for (const Triangle &triangle : placement.footprint().triangulation()) {
        godot::PackedVector2Array points;
        for (const Point &vertex : triangle.vertices) {
            points.push_back(to_screen(vertex));
        }
        draw_colored_polygon(points, fill);
    }

    godot::PackedVector2Array boundary;
    for (const Point &vertex : placement.footprint().vertices()) {
        boundary.push_back(to_screen(vertex));
    }
    draw_polyline(closed_boundary(boundary), GHOST_OUTLINE, OUTLINE_WIDTH);
}

void LevelEditor::_draw() {
    update_canvas_rect();
    if (canvas_rect_.size.x <= 0.0f || canvas_rect_.size.y <= 0.0f) {
        return;
    }

    draw_rect(canvas_rect_, CANVAS_BACKGROUND);
    draw_axes();
    draw_arrangement();
    draw_ghost();
}

} // namespace tiles::game
