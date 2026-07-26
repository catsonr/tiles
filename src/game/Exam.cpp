#include "game/Exam.h"

#include "game/LevelPlayer.h"
#include "game/PrototilePreview.h"
#include "game/resources/LevelResources.h"

#include <godot_cpp/classes/box_container.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_pan_gesture.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace tiles::game {

namespace {

// The one problem canvas presentation, instantiated per visible problem. The
// exam and its tests mount exactly this scene.
const char *CANVAS_SCENE = "res://level_player.tscn";

constexpr float PALETTE_WIDTH = 260.0f;
constexpr float STATUS_HEIGHT = 44.0f;

constexpr double TRANSITION_SECONDS = 0.28;

// A short quiet interval after a completed handoff. One wheel detent is one
// problem; the momentum tail of a trackpad flick is not another four.
constexpr double SCROLL_COOLDOWN_SECONDS = 0.18;

// One wheel detent reports exactly this much, so the threshold admits it and
// rejects the smallest incidental pan.
constexpr double SCROLL_THRESHOLD = 1.0;

const godot::Color SELECTED_MODULATE(1.0f, 1.0f, 1.0f, 1.0f);
const godot::Color UNSELECTED_MODULATE(0.62f, 0.62f, 0.62f, 1.0f);
const godot::Color TRANSPARENT(0.0f, 0.0f, 0.0f, 0.0f);

// One palette row: the authored shape at a size worth looking at, its remaining
// supply beside it, and nothing else.
const godot::Vector2 SHAPE_SIZE(144.0f, 112.0f);
constexpr float ROW_PADDING = 4.0f;
constexpr int ROW_CORNER_RADIUS = 4;

double ease_in_out(double p_t) {
    return p_t < 0.5 ? 4.0 * p_t * p_t * p_t : 1.0 - std::pow(-2.0 * p_t + 2.0, 3.0) / 2.0;
}

} // namespace

void Exam::ShellObserver::on_problem_presentation_changed() {
    if (exam != nullptr) {
        exam->refresh_shell();
    }
}

void Exam::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_manifest", "manifest"), &Exam::set_manifest);
    godot::ClassDB::bind_method(godot::D_METHOD("get_manifest"), &Exam::get_manifest);
    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::PACKED_STRING_ARRAY, "manifest"),
        "set_manifest",
        "get_manifest");

    godot::ClassDB::bind_method(
        godot::D_METHOD("on_palette_pressed", "entry"), &Exam::on_palette_pressed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_canvas_palette_selected", "entry"),
        &Exam::on_canvas_palette_selected);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_canvas_orientation_changed", "entry", "orientation", "forward"),
        &Exam::on_canvas_orientation_changed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_canvas_placement_succeeded", "entry"),
        &Exam::on_canvas_placement_succeeded);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_canvas_removal_succeeded"), &Exam::on_canvas_removal_succeeded);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_canvas_undo_succeeded"), &Exam::on_canvas_undo_succeeded);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_canvas_active_proposal_changed", "present", "proposal"),
        &Exam::on_canvas_active_proposal_changed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_canvas_victory_reached"), &Exam::on_canvas_victory_reached);

    // The application's semantic event surface. It is the stable one: canvases
    // come and go with navigation, so a consumer connects here once. Nothing
    // below names a sound, a file, a cue, a volume, or a backend, and no part of
    // play consults any of it.
    ADD_SIGNAL(godot::MethodInfo(
        "palette_selected", godot::PropertyInfo(godot::Variant::INT, "entry")));
    ADD_SIGNAL(godot::MethodInfo(
        "orientation_changed",
        godot::PropertyInfo(godot::Variant::INT, "entry"),
        godot::PropertyInfo(godot::Variant::INT, "orientation"),
        godot::PropertyInfo(godot::Variant::BOOL, "forward")));
    ADD_SIGNAL(godot::MethodInfo(
        "placement_succeeded", godot::PropertyInfo(godot::Variant::INT, "entry")));
    ADD_SIGNAL(godot::MethodInfo("removal_succeeded"));
    ADD_SIGNAL(godot::MethodInfo("undo_succeeded"));
    ADD_SIGNAL(godot::MethodInfo(
        "active_proposal_changed",
        godot::PropertyInfo(godot::Variant::BOOL, "present"),
        godot::PropertyInfo(godot::Variant::INT, "proposal")));
    ADD_SIGNAL(godot::MethodInfo("victory_reached"));
    ADD_SIGNAL(godot::MethodInfo(
        "problem_changed",
        godot::PropertyInfo(godot::Variant::INT, "index"),
        godot::PropertyInfo(godot::Variant::INT, "number")));
}

void Exam::set_manifest(const godot::PackedStringArray &p_manifest) {
    manifest_ = p_manifest;
}

godot::PackedStringArray Exam::get_manifest() const {
    return manifest_;
}

// --- startup ---

void Exam::_ready() {
    observer_.exam = this;
    if (!bind_scene()) {
        return;
    }
    set_focus_mode(godot::Control::FOCUS_ALL);
    set_process(true);
    canvas_scene_ = godot::ResourceLoader::get_singleton()->load(CANVAS_SCENE);
    if (canvas_scene_.is_null()) {
        godot::UtilityFunctions::push_error(
            "[tiles] exam: the problem canvas scene failed to load: ", CANVAS_SCENE);
        return;
    }
    if (!load_problems()) {
        // Nothing partially loaded is ever exposed: a manifest which cannot be
        // completely loaded produces no problems at all.
        problems_.clear();
        return;
    }
    update_shell_layout();
    current_canvas_ = make_canvas(0, 0.0f);
    if (current_canvas_ == nullptr) {
        problems_.clear();
        return;
    }
    current_index_ = 0;
    ready_ = true;
    build_palette();
    refresh_shell();
    grab_focus();
    godot::UtilityFunctions::print(
        "[tiles] exam ready: ",
        static_cast<std::int64_t>(problems_.size()),
        " problems, showing problem ",
        problem_number(current_index_));
}

bool Exam::bind_scene() {
    godot::String missing;
    const auto require = [&](const char *p_path) -> godot::Node * {
        godot::Node *node = get_node_or_null(godot::NodePath(p_path));
        if (node == nullptr) {
            missing += missing.is_empty() ? godot::String() : godot::String(", ");
            missing += p_path;
        }
        return node;
    };
    viewport_ = godot::Object::cast_to<godot::Control>(require("Viewport"));
    palette_ = godot::Object::cast_to<godot::Control>(require("Palette"));
    palette_rows_ = godot::Object::cast_to<godot::Control>(require("Palette/Margin/Rows"));
    status_label_ = godot::Object::cast_to<godot::Label>(require("Status"));
    if (viewport_ == nullptr || palette_ == nullptr || palette_rows_ == nullptr
        || status_label_ == nullptr) {
        godot::UtilityFunctions::push_error(
            "[tiles] exam scene is incomplete: missing or mistyped children: ", missing);
        return false;
    }
    // The clip is what makes a moving canvas leave the visible area instead of
    // being drawn over the fixed shell.
    viewport_->set_clip_contents(true);
    viewport_->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
    return true;
}

void Exam::report_load_failure(
    const godot::String &p_path, const LoadLevelError &p_error) const {
    godot::UtilityFunctions::push_error(
        "[tiles] exam: ",
        p_path,
        " failed to load with typed code ",
        static_cast<std::int64_t>(p_error.code));
    if (!p_error.compilation_error.has_value()) {
        return;
    }
    const LevelResourceError &level_error = p_error.compilation_error.value();
    godot::UtilityFunctions::push_error(
        "[tiles] exam: ",
        p_path,
        " failed compilation with typed code ",
        static_cast<std::int64_t>(level_error.code));
    if (level_error.palette_error.has_value()) {
        godot::UtilityFunctions::push_error(
            "[tiles] exam: ",
            p_path,
            " palette stage typed code ",
            static_cast<std::int64_t>(level_error.palette_error->code));
    }
    if (level_error.blueprint_error.has_value()) {
        godot::UtilityFunctions::push_error(
            "[tiles] exam: ",
            p_path,
            " blueprint stage typed code ",
            static_cast<std::int64_t>(level_error.blueprint_error->code));
    }
    if (level_error.arrangement_error.has_value()) {
        godot::UtilityFunctions::push_error(
            "[tiles] exam: ",
            p_path,
            " arrangement stage typed code ",
            static_cast<std::int64_t>(level_error.arrangement_error->code));
    }
    if (level_error.region_error.has_value()) {
        godot::UtilityFunctions::push_error(
            "[tiles] exam: ",
            p_path,
            " region stage typed code ",
            static_cast<std::int64_t>(level_error.region_error->code));
    }
}

// Every problem is loaded, compiled, and given its empty exact Session before
// anything is shown. There is no background loading, no prefetch, and no
// eviction: only presentation is built on demand.
bool Exam::load_problems() {
    problems_.clear();
    if (manifest_.is_empty()) {
        godot::UtilityFunctions::push_error("[tiles] exam: the manifest is empty");
        return false;
    }
    auto catalog = content::make_canonical_prototile_catalog();
    if (!catalog) {
        godot::UtilityFunctions::push_error("[tiles] exam: canonical catalog failed");
        return false;
    }
    catalog_ = std::move(catalog).value();
    for (std::int64_t i = 0; i < manifest_.size(); ++i) {
        const godot::String path = manifest_[i];
        if (path.is_empty()) {
            godot::UtilityFunctions::push_error(
                "[tiles] exam: manifest entry ", i, " is empty");
            return false;
        }
        for (std::int64_t j = 0; j < i; ++j) {
            if (godot::String(manifest_[j]) == path) {
                godot::UtilityFunctions::push_error(
                    "[tiles] exam: manifest entry ", i, " repeats ", path);
                return false;
            }
        }
        auto loaded = load_level_resource(path, catalog_.value());
        if (!loaded) {
            report_load_failure(path, loaded.error());
            return false;
        }
        LoadedLevelResource level = std::move(loaded).value();
        const godot::Ref<PaletteResource> palette_resource = level.resource->get_palette();
        if (palette_resource.is_null()) {
            godot::UtilityFunctions::push_error(
                "[tiles] exam: ", path, " compiled without an authored palette");
            return false;
        }
        const godot::TypedArray<PaletteEntryResource> entries = palette_resource->get_entries();
        std::vector<godot::Color> colors;
        colors.reserve(static_cast<std::size_t>(entries.size()));
        for (std::int64_t entry = 0; entry < entries.size(); ++entry) {
            const godot::Ref<PaletteEntryResource> authored = entries[entry];
            if (authored.is_null()) {
                godot::UtilityFunctions::push_error(
                    "[tiles] exam: ", path, " palette entry ", entry, " is missing");
                return false;
            }
            colors.push_back(authored->get_color());
        }
        // Authored colors are read in palette order and are parallel to the
        // compiled entries. A presentation indexes one with the other.
        if (colors.size() != level.compiled.level.palette().entries().size()) {
            godot::UtilityFunctions::push_error(
                "[tiles] exam: ", path, " authored colors do not match its compiled palette");
            return false;
        }
        // The compiled witness stays authoring proof: the session begins empty.
        problems_.push_back(std::make_unique<ProblemState>(
            engine::Session(engine::State(std::move(level.compiled.level))), std::move(colors)));
    }
    return true;
}

// --- layout ---

godot::Rect2 Exam::viewport_rect() const {
    if (viewport_ == nullptr) {
        return godot::Rect2();
    }
    return godot::Rect2(viewport_->get_position(), viewport_->get_size());
}

// One authority for the playable area, so the projection a canvas fits into is
// never a layout accident.
void Exam::update_shell_layout() {
    if (viewport_ == nullptr) {
        return;
    }
    const godot::Vector2 size = get_size();
    const godot::Vector2 play(
        std::max(0.0f, size.x - PALETTE_WIDTH), std::max(0.0f, size.y - STATUS_HEIGHT));
    viewport_->set_position(godot::Vector2(PALETTE_WIDTH, 0.0f));
    viewport_->set_size(play);
    palette_->set_position(godot::Vector2(0.0f, 0.0f));
    palette_->set_size(godot::Vector2(PALETTE_WIDTH, play.y));
    status_label_->set_position(godot::Vector2(0.0f, play.y));
    status_label_->set_size(godot::Vector2(size.x, STATUS_HEIGHT));
    if (current_canvas_ != nullptr && !transition_.has_value()) {
        current_canvas_->set_position(godot::Vector2(0.0f, 0.0f));
        current_canvas_->set_size(play);
    }
}

void Exam::_notification(int p_what) {
    if (p_what != godot::Control::NOTIFICATION_RESIZED) {
        return;
    }
    // A resize mid-transition finishes it immediately. That leaves exactly one
    // valid current problem and one correctly fitted canvas, and it mutates
    // neither session.
    if (transition_.has_value()) {
        advance_transition(TRANSITION_SECONDS);
    }
    update_shell_layout();
}

// --- canvases and navigation ---

LevelPlayer *Exam::make_canvas(std::size_t p_index, float p_offset) {
    if (canvas_scene_.is_null() || viewport_ == nullptr || p_index >= problems_.size()) {
        return nullptr;
    }
    godot::Node *node = canvas_scene_->instantiate();
    auto *canvas = godot::Object::cast_to<LevelPlayer>(node);
    if (canvas == nullptr) {
        godot::UtilityFunctions::push_error(
            "[tiles] exam: the problem canvas scene root is not a player");
        if (node != nullptr) {
            memdelete(node);
        }
        return nullptr;
    }
    // The order is the whole point of a pop-free handoff: the canvas exists, has
    // its final size, is bound to its persistent problem, and has fitted,
    // projected, and rebuilt everything it draws before it is placed anywhere.
    viewport_->add_child(canvas);
    canvas->set_size(viewport_->get_size());
    canvas->bind(*problems_[p_index], problem_number(p_index), &observer_);
    canvas->set_position(godot::Vector2(0.0f, p_offset));
    canvas->connect(
        godot::StringName("palette_selected"),
        godot::Callable(this, godot::StringName("on_canvas_palette_selected")));
    canvas->connect(
        godot::StringName("orientation_changed"),
        godot::Callable(this, godot::StringName("on_canvas_orientation_changed")));
    canvas->connect(
        godot::StringName("placement_succeeded"),
        godot::Callable(this, godot::StringName("on_canvas_placement_succeeded")));
    canvas->connect(
        godot::StringName("removal_succeeded"),
        godot::Callable(this, godot::StringName("on_canvas_removal_succeeded")));
    canvas->connect(
        godot::StringName("undo_succeeded"),
        godot::Callable(this, godot::StringName("on_canvas_undo_succeeded")));
    canvas->connect(
        godot::StringName("active_proposal_changed"),
        godot::Callable(this, godot::StringName("on_canvas_active_proposal_changed")));
    canvas->connect(
        godot::StringName("victory_reached"),
        godot::Callable(this, godot::StringName("on_canvas_victory_reached")));
    return canvas;
}

bool Exam::navigate(int p_direction) {
    if (!ready_ || transition_.has_value() || current_canvas_ == nullptr) {
        return false;
    }
    if (p_direction != 1 && p_direction != -1) {
        return false;
    }
    if (p_direction > 0 && current_index_ + 1 >= problems_.size()) {
        return false;
    }
    if (p_direction < 0 && current_index_ == 0) {
        return false;
    }
    const std::size_t destination_index =
        p_direction > 0 ? current_index_ + 1 : current_index_ - 1;
    const float travel = viewport_->get_size().y;
    LevelPlayer *destination =
        make_canvas(destination_index, p_direction > 0 ? travel : -travel);
    if (destination == nullptr) {
        return false;
    }
    Transition transition;
    transition.source = current_canvas_;
    transition.destination = destination;
    transition.destination_index = destination_index;
    transition.direction = p_direction;
    transition.elapsed = 0.0;
    transition.travel = travel;
    transition_ = transition;
    return true;
}

bool Exam::advance_transition(double p_delta) {
    if (!transition_.has_value()) {
        return false;
    }
    Transition &transition = transition_.value();
    transition.elapsed += std::max(0.0, p_delta);
    const double raw = TRANSITION_SECONDS <= 0.0
        ? 1.0
        : std::min(1.0, transition.elapsed / TRANSITION_SECONDS);
    // Both canvases move by exactly the same offset, so the edge they share
    // never opens.
    const float shift = static_cast<float>(
        ease_in_out(raw) * static_cast<double>(transition.travel)
        * (transition.direction > 0 ? -1.0 : 1.0));
    const float entry_offset =
        transition.direction > 0 ? transition.travel : -transition.travel;
    if (transition.source != nullptr) {
        transition.source->set_position(godot::Vector2(0.0f, shift));
    }
    transition.destination->set_position(godot::Vector2(0.0f, entry_offset + shift));
    if (raw < 1.0) {
        return true;
    }
    complete_transition();
    return false;
}

void Exam::complete_transition() {
    if (!transition_.has_value()) {
        return;
    }
    LevelPlayer *source = transition_->source;
    LevelPlayer *destination = transition_->destination;
    destination->set_position(godot::Vector2(0.0f, 0.0f));
    destination->set_size(viewport_->get_size());
    current_canvas_ = destination;
    current_index_ = transition_->destination_index;
    build_palette();
    refresh_shell();
    emit_signal(
        godot::StringName("problem_changed"),
        static_cast<std::int64_t>(current_index_),
        problem_number(current_index_));
    if (source != nullptr) {
        viewport_->remove_child(source);
        source->queue_free();
    }
    transition_.reset();
    scroll_cooldown_ = SCROLL_COOLDOWN_SECONDS;
}

void Exam::_process(double p_delta) {
    if (scroll_cooldown_ > 0.0) {
        scroll_cooldown_ = std::max(0.0, scroll_cooldown_ - p_delta);
    }
    if (transition_.has_value()) {
        advance_transition(p_delta);
    }
}

// --- input ---

godot::Vector2 Exam::to_canvas_local(godot::Vector2 p_position) const {
    godot::Vector2 local = p_position - viewport_->get_position();
    if (current_canvas_ != nullptr) {
        local -= current_canvas_->get_position();
    }
    return local;
}

bool Exam::request_scroll_navigation(godot::Vector2 p_position, double p_amount) {
    // Scrolling is a property of the playable canvas. The fixed palette and the
    // fixed control line never navigate.
    if (!viewport_rect().has_point(p_position)) {
        return false;
    }
    if (transition_.has_value() || scroll_cooldown_ > 0.0) {
        return false;
    }
    if (std::abs(p_amount) < SCROLL_THRESHOLD) {
        return false;
    }
    return navigate(p_amount > 0.0 ? 1 : -1);
}

void Exam::_gui_input(const godot::Ref<godot::InputEvent> &p_event) {
    if (!ready_) {
        return;
    }
    const godot::Ref<godot::InputEventKey> key = p_event;
    if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
        if (transition_.has_value() || current_canvas_ == nullptr) {
            return;
        }
        bool handled = true;
        if (key->is_ctrl_pressed() && key->get_keycode() == godot::KEY_Z) {
            current_canvas_->undo();
        } else if (key->get_keycode() == godot::KEY_TAB) {
            current_canvas_->cycle_entry(!key->is_shift_pressed());
        } else if (key->get_keycode() == godot::KEY_R) {
            current_canvas_->cycle_orientation(!key->is_shift_pressed());
        } else {
            handled = false;
        }
        if (handled) {
            // Tab belongs to the palette, not to focus navigation.
            accept_event();
        }
        return;
    }
    const godot::Ref<godot::InputEventPanGesture> pan = p_event;
    if (pan.is_valid()) {
        if (request_scroll_navigation(pan->get_position(), pan->get_delta().y)) {
            accept_event();
        }
        return;
    }
    const godot::Ref<godot::InputEventMouseMotion> motion = p_event;
    if (motion.is_valid()) {
        if (!transition_.has_value() && current_canvas_ != nullptr
            && viewport_rect().has_point(motion->get_position())) {
            current_canvas_->set_pointer(to_canvas_local(motion->get_position()));
        }
        return;
    }
    const godot::Ref<godot::InputEventMouseButton> button = p_event;
    if (!button.is_valid() || !button->is_pressed()) {
        return;
    }
    const godot::MouseButton index = button->get_button_index();
    if (index == godot::MOUSE_BUTTON_WHEEL_DOWN || index == godot::MOUSE_BUTTON_WHEEL_UP) {
        if (request_scroll_navigation(
                button->get_position(), index == godot::MOUSE_BUTTON_WHEEL_DOWN ? 1.0 : -1.0)) {
            accept_event();
        }
        return;
    }
    if (transition_.has_value() || current_canvas_ == nullptr
        || !viewport_rect().has_point(button->get_position())) {
        return;
    }
    grab_focus();
    if (index == godot::MOUSE_BUTTON_LEFT) {
        current_canvas_->accept_active_proposal();
    } else if (index == godot::MOUSE_BUTTON_RIGHT) {
        current_canvas_->remove_at_local(to_canvas_local(button->get_position()));
    }
}

// --- the fixed palette ---

void Exam::clear_palette() {
    entry_controls_.clear();
    if (palette_rows_ == nullptr) {
        return;
    }
    while (palette_rows_->get_child_count() > 0) {
        godot::Node *child = palette_rows_->get_child(0);
        palette_rows_->remove_child(child);
        child->queue_free();
    }
}

// The palette presents exactly one problem: the current one. It is rebuilt once,
// after a destination has become current, and never during a transition.
void Exam::build_palette() {
    clear_palette();
    if (palette_rows_ == nullptr || current_index_ >= problems_.size()) {
        return;
    }
    const ProblemState &state = *problems_[current_index_];
    const std::vector<engine::PaletteEntry> &entries = state.session().state().palette().entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        // One row, wide enough to be tinted behind the selected shape.
        auto *row = memnew(godot::PanelContainer);
        godot::Ref<godot::StyleBoxFlat> highlight;
        highlight.instantiate();
        highlight->set_bg_color(TRANSPARENT);
        highlight->set_corner_radius_all(ROW_CORNER_RADIUS);
        highlight->set_content_margin_all(ROW_PADDING);
        row->add_theme_stylebox_override(godot::StringName("panel"), highlight);
        auto *line = memnew(godot::HBoxContainer);
        line->set_alignment(godot::BoxContainer::ALIGNMENT_CENTER);
        row->add_child(line);
        // The shape itself is the control. The preview stays a non-focusable,
        // input-ignoring view of one exact polygon inside a clickable parent, so
        // pointer selection reaches exactly the same operation tab does.
        auto *button = memnew(godot::Button);
        button->set_flat(true);
        button->set_focus_mode(godot::Control::FOCUS_NONE);
        button->set_custom_minimum_size(SHAPE_SIZE);
        auto *preview = memnew(PrototilePreview);
        preview->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
        preview->set_anchors_preset(godot::Control::PRESET_FULL_RECT);
        preview->set_polygon(entries[i].orientations().front().canonical_polygon());
        preview->set_fill_color(state.colors()[i]);
        button->add_child(preview);
        auto *supply = memnew(godot::Label);
        supply->set_vertical_alignment(godot::VERTICAL_ALIGNMENT_CENTER);
        line->add_child(button);
        line->add_child(supply);
        palette_rows_->add_child(row);
        button->connect(
            godot::StringName("pressed"),
            godot::Callable(this, godot::StringName("on_palette_pressed"))
                .bind(static_cast<std::int64_t>(i)));
        entry_controls_.push_back(EntryControl { row, highlight, button, preview, supply });
    }
}

void Exam::refresh_shell() {
    if (current_index_ >= problems_.size()) {
        return;
    }
    const ProblemState &state = *problems_[current_index_];
    const engine::State &exact = state.session().state();
    const std::vector<engine::PaletteEntry> &entries = exact.palette().entries();
    const bool selected = state.selection().has_value();
    for (std::size_t i = 0; i < entry_controls_.size() && i < entries.size(); ++i) {
        const auto status = exact.supply_status(engine::PaletteEntryIndex(i));
        const bool finite = status.has_value() && status->remaining.has_value();
        entry_controls_[i].supply->set_visible(finite);
        if (finite) {
            entry_controls_[i].supply->set_text(
                godot::String::num_uint64(status->remaining.value()));
        }
        const bool current = selected && state.selection()->entry == i;
        if (entry_controls_[i].highlight.is_valid()) {
            // The selected row sits on the same fill the exact target is drawn
            // with, so the palette and the canvas agree about what "this one" is.
            entry_controls_[i].highlight->set_bg_color(
                current ? LevelPlayer::region_fill_color() : TRANSPARENT);
        }
        const std::size_t orientation = current ? state.selection()->orientation : 0;
        PrototilePreview *preview = entry_controls_[i].preview;
        if (preview != nullptr && orientation < entries[i].orientations().size()) {
            preview->set_polygon(entries[i].orientations()[orientation].canonical_polygon());
            preview->set_fill_color(state.colors()[i]);
            preview->set_modulate(current ? SELECTED_MODULATE : UNSELECTED_MODULATE);
        }
    }
    if (status_label_ != nullptr && current_canvas_ != nullptr) {
        status_label_->set_text(current_canvas_->status_text());
    }
}

void Exam::on_palette_pressed(std::int64_t p_entry) {
    if (transition_.has_value() || current_canvas_ == nullptr || p_entry < 0) {
        return;
    }
    grab_focus();
    current_canvas_->select_entry(static_cast<std::size_t>(p_entry));
}

// --- forwarded semantic hooks ---

void Exam::on_canvas_palette_selected(std::int64_t p_entry) {
    emit_signal(godot::StringName("palette_selected"), p_entry);
}

void Exam::on_canvas_orientation_changed(
    std::int64_t p_entry, std::int64_t p_orientation, bool p_forward) {
    emit_signal(godot::StringName("orientation_changed"), p_entry, p_orientation, p_forward);
}

void Exam::on_canvas_placement_succeeded(std::int64_t p_entry) {
    emit_signal(godot::StringName("placement_succeeded"), p_entry);
}

void Exam::on_canvas_removal_succeeded() {
    emit_signal(godot::StringName("removal_succeeded"));
}

void Exam::on_canvas_undo_succeeded() {
    emit_signal(godot::StringName("undo_succeeded"));
}

void Exam::on_canvas_active_proposal_changed(bool p_present, std::int64_t p_proposal) {
    emit_signal(godot::StringName("active_proposal_changed"), p_present, p_proposal);
}

void Exam::on_canvas_victory_reached() {
    emit_signal(godot::StringName("victory_reached"));
}

// --- observation ---

bool Exam::ready_to_play() const {
    return ready_;
}

std::size_t Exam::problem_count() const {
    return problems_.size();
}

std::size_t Exam::current_index() const {
    return current_index_;
}

// The first manifest entry is the first problem shown and carries the largest
// number. Nothing here knows how many problems the exam contains.
std::int64_t Exam::problem_number(std::size_t p_index) const {
    if (p_index >= problems_.size()) {
        return 0;
    }
    return static_cast<std::int64_t>(problems_.size() - p_index);
}

ProblemState *Exam::problem(std::size_t p_index) {
    return p_index < problems_.size() ? problems_[p_index].get() : nullptr;
}

LevelPlayer *Exam::current_canvas() const {
    return current_canvas_;
}

LevelPlayer *Exam::incoming_canvas() const {
    return transition_.has_value() ? transition_->destination : nullptr;
}

bool Exam::transitioning() const {
    return transition_.has_value();
}

std::size_t Exam::entry_row_count() const {
    return entry_controls_.size();
}

const PrototilePreview *Exam::entry_preview(std::size_t p_entry) const {
    return p_entry < entry_controls_.size() ? entry_controls_[p_entry].preview : nullptr;
}

godot::Button *Exam::entry_button(std::size_t p_entry) const {
    return p_entry < entry_controls_.size() ? entry_controls_[p_entry].button : nullptr;
}

bool Exam::entry_supply_visible(std::size_t p_entry) const {
    return p_entry < entry_controls_.size() && entry_controls_[p_entry].supply->is_visible();
}

godot::String Exam::entry_supply_text(std::size_t p_entry) const {
    return p_entry < entry_controls_.size() ? entry_controls_[p_entry].supply->get_text()
                                            : godot::String();
}

godot::Color Exam::entry_row_highlight(std::size_t p_entry) const {
    if (p_entry >= entry_controls_.size() || entry_controls_[p_entry].highlight.is_null()) {
        return TRANSPARENT;
    }
    return entry_controls_[p_entry].highlight->get_bg_color();
}

godot::Control *Exam::problem_viewport() const {
    return viewport_;
}

godot::Control *Exam::palette_panel() const {
    return palette_;
}

godot::Control *Exam::status_line() const {
    return status_label_;
}

} // namespace tiles::game
