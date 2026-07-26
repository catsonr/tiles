#include "game/testing/ExamIntegrationRunner.h"

#include "core/geometry/Coordinate.h"
#include "game/Exam.h"
#include "game/LevelPlayer.h"
#include "game/PrototilePreview.h"
#include "game/ProblemState.h"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <initializer_list>
#include <optional>

namespace tiles::game {

namespace {

const char *EXAM_SCENE = "res://exam.tscn";

// The authored exam, in the order it must ship in. This is content, restated
// here rather than read back out of the scene it is checking.
const char *CONTRACT_MANIFEST[] = {
    "res://levels/act0-level.tres",
    "res://levels/raine_flower.tres",
    "res://levels/horse.tres",
    "res://levels/izjl.tres",
    "res://levels/small.tres",
    "res://levels/raine_candy_bug.tres",
    "res://levels/ring.tres",
    "res://levels/zooble.tres",
    "res://levels/goul.tres",
    "res://levels/glider.tres",
    "res://levels/bird.tres",
    "res://levels/spore.tres",
    "res://levels/pythag.tres",
    "res://levels/snake_tri_hex.tres",
    "res://levels/guy_f.tres",
    "res://levels/dude.tres",
    "res://levels/galaxy.tres",
    "res://levels/fan.tres",
    "res://levels/zig.tres",
    "res://levels/bug.tres",
    "res://levels/slurm.tres",
    "res://levels/spider.tres",
    "res://levels/message.tres",
    "res://levels/perkins.tres",
};
constexpr std::size_t CONTRACT_MANIFEST_SIZE =
    sizeof(CONTRACT_MANIFEST) / sizeof(CONTRACT_MANIFEST[0]);

const char *FIXTURE_LEVEL = "res://tests/fixtures/canonical_level.tres";
const char *ACT_LEVEL = "res://levels/act0-level.tres";
const char *SMALL_LEVEL = "res://levels/small.tres";
const char *IZJL_LEVEL = "res://levels/izjl.tres";

// A deterministic window for the headless shell. Every offset expectation is
// derived from the resulting viewport rather than from these numbers.
constexpr float EXAM_WIDTH = 1280.0f;
constexpr float EXAM_HEIGHT = 720.0f;

godot::PackedStringArray manifest_of(std::initializer_list<const char *> p_paths) {
    godot::PackedStringArray manifest;
    for (const char *path : p_paths) {
        manifest.push_back(path);
    }
    return manifest;
}

godot::Ref<godot::InputEventKey> key_event(godot::Key p_keycode, bool p_shift, bool p_ctrl) {
    godot::Ref<godot::InputEventKey> event;
    event.instantiate();
    event->set_keycode(p_keycode);
    event->set_pressed(true);
    event->set_shift_pressed(p_shift);
    event->set_ctrl_pressed(p_ctrl);
    return event;
}

godot::Ref<godot::InputEventMouseButton> button_event(
    godot::MouseButton p_index, godot::Vector2 p_position) {
    godot::Ref<godot::InputEventMouseButton> event;
    event.instantiate();
    event->set_button_index(p_index);
    event->set_pressed(true);
    event->set_position(p_position);
    return event;
}

godot::Vector2 centroid(const LevelPlayer &p_canvas, const Placement &p_placement) {
    double x = 0.0;
    double y = 0.0;
    const auto &vertices = p_placement.footprint().vertices();
    for (const Point &vertex : vertices) {
        const godot::Vector2 projected = p_canvas.project(vertex);
        x += projected.x;
        y += projected.y;
    }
    return godot::Vector2(
        static_cast<real_t>(x / vertices.size()), static_cast<real_t>(y / vertices.size()));
}

std::vector<PlacementId::Value> placement_ids(const engine::State &p_state) {
    std::vector<PlacementId::Value> ids;
    for (const Entry &entry : p_state.arrangement().entries()) {
        ids.push_back(entry.id.value());
    }
    return ids;
}

std::vector<std::optional<engine::Supply::Amount>> supplies(const engine::State &p_state) {
    std::vector<std::optional<engine::Supply::Amount>> remaining;
    for (std::size_t i = 0; i < p_state.palette().entries().size(); ++i) {
        const auto status = p_state.supply_status(engine::PaletteEntryIndex(i));
        remaining.push_back(status.has_value() ? status->remaining : std::nullopt);
    }
    return remaining;
}

} // namespace

void ExamIntegrationRunner::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_palette_selected", "entry"),
        &ExamIntegrationRunner::on_palette_selected);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_orientation_changed", "entry", "orientation", "forward"),
        &ExamIntegrationRunner::on_orientation_changed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_placement_succeeded", "entry"),
        &ExamIntegrationRunner::on_placement_succeeded);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_removal_succeeded"), &ExamIntegrationRunner::on_removal_succeeded);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_undo_succeeded"), &ExamIntegrationRunner::on_undo_succeeded);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_active_proposal_changed", "present", "proposal"),
        &ExamIntegrationRunner::on_active_proposal_changed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_victory_reached"), &ExamIntegrationRunner::on_victory_reached);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_problem_changed", "index", "number"),
        &ExamIntegrationRunner::on_problem_changed);
}

void ExamIntegrationRunner::on_palette_selected(std::int64_t) {
    ++hooks_.palette_selected;
}

void ExamIntegrationRunner::on_orientation_changed(std::int64_t, std::int64_t, bool) {
    ++hooks_.orientation_changed;
}

void ExamIntegrationRunner::on_placement_succeeded(std::int64_t) {
    ++hooks_.placement_succeeded;
}

void ExamIntegrationRunner::on_removal_succeeded() {
    ++hooks_.removal_succeeded;
}

void ExamIntegrationRunner::on_undo_succeeded() {
    ++hooks_.undo_succeeded;
}

void ExamIntegrationRunner::on_active_proposal_changed(bool, std::int64_t) {
    ++hooks_.active_proposal_changed;
}

void ExamIntegrationRunner::on_victory_reached() {
    ++hooks_.victory_reached;
}

void ExamIntegrationRunner::on_problem_changed(std::int64_t p_index, std::int64_t p_number) {
    ++hooks_.problem_changed;
    problem_changes_.push_back(p_index);
    problem_changes_.push_back(p_number);
}

bool ExamIntegrationRunner::expect(bool p_condition, const char *p_description) {
    ++checks_;
    if (!p_condition) {
        ++failures_;
        godot::UtilityFunctions::push_error("[tiles] exam integration: ", p_description);
    }
    return p_condition;
}

Exam *ExamIntegrationRunner::make_exam(const godot::PackedStringArray &p_manifest) {
    const godot::Ref<godot::PackedScene> scene =
        godot::ResourceLoader::get_singleton()->load(EXAM_SCENE);
    if (!expect(scene.is_valid(), "the real exam scene loads")) {
        return nullptr;
    }
    godot::Node *node = scene->instantiate();
    auto *exam = godot::Object::cast_to<Exam>(node);
    if (!expect(exam != nullptr, "the exam scene root is an Exam")) {
        if (node != nullptr) {
            memdelete(node);
        }
        return nullptr;
    }
    // An empty replacement means "keep what the scene authored".
    if (!p_manifest.is_empty()) {
        exam->set_manifest(p_manifest);
    }
    add_child(exam);
    exam->set_size(godot::Vector2(EXAM_WIDTH, EXAM_HEIGHT));
    return exam;
}

void ExamIntegrationRunner::connect_hooks(Exam &p_exam) {
    hooks_ = HookCounts {};
    problem_changes_.clear();
    p_exam.connect(
        godot::StringName("palette_selected"),
        godot::Callable(this, godot::StringName("on_palette_selected")));
    p_exam.connect(
        godot::StringName("orientation_changed"),
        godot::Callable(this, godot::StringName("on_orientation_changed")));
    p_exam.connect(
        godot::StringName("placement_succeeded"),
        godot::Callable(this, godot::StringName("on_placement_succeeded")));
    p_exam.connect(
        godot::StringName("removal_succeeded"),
        godot::Callable(this, godot::StringName("on_removal_succeeded")));
    p_exam.connect(
        godot::StringName("undo_succeeded"),
        godot::Callable(this, godot::StringName("on_undo_succeeded")));
    p_exam.connect(
        godot::StringName("active_proposal_changed"),
        godot::Callable(this, godot::StringName("on_active_proposal_changed")));
    p_exam.connect(
        godot::StringName("victory_reached"),
        godot::Callable(this, godot::StringName("on_victory_reached")));
    p_exam.connect(
        godot::StringName("problem_changed"),
        godot::Callable(this, godot::StringName("on_problem_changed")));
}

// The production step, run to completion. Nothing here samples an arbitrary
// frame of the motion or reaches around the handoff.
void ExamIntegrationRunner::finish_transition(Exam &p_exam) {
    std::size_t guard = 0;
    while (p_exam.transitioning() && guard < 8) {
        p_exam.advance_transition(1.0);
        ++guard;
    }
    expect(!p_exam.transitioning(), "the production transition step completes the handoff");
}

bool ExamIntegrationRunner::place_somewhere(LevelPlayer &p_canvas) {
    return p_canvas.active_proposal().has_value() && p_canvas.accept_active_proposal();
}

// --- manifest and startup ---

void ExamIntegrationRunner::check_manifest_and_startup() {
    Exam *exam = make_exam(godot::PackedStringArray());
    if (exam == nullptr) {
        return;
    }
    const godot::PackedStringArray manifest = exam->get_manifest();
    expect(
        static_cast<std::size_t>(manifest.size()) == CONTRACT_MANIFEST_SIZE,
        "the authored manifest contains exactly the authored problems");
    bool ordered = static_cast<std::size_t>(manifest.size()) == CONTRACT_MANIFEST_SIZE;
    for (std::size_t i = 0; i < CONTRACT_MANIFEST_SIZE && ordered; ++i) {
        if (godot::String(manifest[static_cast<std::int64_t>(i)])
            != godot::String(CONTRACT_MANIFEST[i])) {
            ordered = false;
        }
    }
    expect(ordered, "the authored manifest paths and their order are exactly the authored order");

    expect(exam->ready_to_play(), "every authored manifest entry loads and compiles");
    expect(
        exam->problem_count() == static_cast<std::size_t>(manifest.size()),
        "one persistent problem exists per manifest entry");
    bool empty_sessions = true;
    bool distinct_slots = true;
    for (std::size_t i = 0; i < exam->problem_count(); ++i) {
        ProblemState *state = exam->problem(i);
        if (state == nullptr || !state->session().state().arrangement().entries().empty()
            || state->session().undo_depth() != 0) {
            empty_sessions = false;
            continue;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (exam->problem(j) == state) {
                distinct_slots = false;
            }
        }
    }
    expect(empty_sessions, "every problem session begins empty, with no witness placement");
    expect(distinct_slots, "every problem owns its own state slot");
    expect(exam->current_index() == 0, "the exam begins at the first manifest entry");
    expect(
        exam->problem_number(0) == static_cast<std::int64_t>(exam->problem_count()),
        "the first displayed problem number is the manifest size");
    expect(
        exam->problem_number(exam->problem_count() - 1) == 1,
        "the last manifest entry is problem 1");
    LevelPlayer *canvas = exam->current_canvas();
    if (expect(canvas != nullptr, "ordinary startup mounts the first problem")) {
        expect(
            canvas->state() == exam->problem(0),
            "the mounted canvas borrows the first persistent problem");
        expect(
            canvas->problem_number() == static_cast<std::int64_t>(exam->problem_count()),
            "the mounted canvas presents the derived problem number");
        const godot::Label *number = canvas->number_label();
        expect(
            number != nullptr
                && number->get_text()
                    == godot::String("problem ")
                        + godot::String::num_int64(
                            static_cast<std::int64_t>(exam->problem_count())),
            "the canvas shows its problem number and nothing else about the problem");
    }
    exam->queue_free();

    // The same code, a different manifest: nothing counts 24.
    Exam *small = make_exam(manifest_of({ SMALL_LEVEL, IZJL_LEVEL, ACT_LEVEL }));
    if (small != nullptr) {
        expect(small->ready_to_play(), "a smaller manifest loads through the same path");
        expect(small->problem_count() == 3, "a smaller manifest holds exactly its own problems");
        expect(
            small->problem_number(0) == 3 && small->problem_number(1) == 2
                && small->problem_number(2) == 1,
            "a smaller manifest derives its own descending numbers");
        expect(
            small->current_canvas() != nullptr && small->current_canvas()->problem_number() == 3,
            "the first problem of a smaller manifest is numbered from its own size");
        small->queue_free();
    }
}

void ExamIntegrationRunner::check_invalid_manifests() {
    godot::UtilityFunctions::print(
        "[tiles] exam integration: three deliberately invalid manifests follow; their "
        "errors are expected");

    Exam *empty = make_exam(manifest_of({ "" }));
    if (empty != nullptr) {
        expect(!empty->ready_to_play(), "an empty manifest path exposes no exam");
        expect(empty->problem_count() == 0, "an empty manifest path loads no problem");
        expect(empty->current_canvas() == nullptr, "an empty manifest path mounts no canvas");
        empty->queue_free();
    }

    Exam *duplicated = make_exam(manifest_of({ ACT_LEVEL, SMALL_LEVEL, ACT_LEVEL }));
    if (duplicated != nullptr) {
        expect(!duplicated->ready_to_play(), "a repeated manifest path exposes no exam");
        expect(duplicated->problem_count() == 0, "a repeated manifest path loads no problem");
        expect(
            duplicated->current_canvas() == nullptr,
            "a repeated manifest path mounts no canvas");
        duplicated->queue_free();
    }

    Exam *invalid = make_exam(manifest_of({ ACT_LEVEL, "res://levels/not_a_level.tres" }));
    if (invalid != nullptr) {
        expect(!invalid->ready_to_play(), "an unloadable manifest entry exposes no exam");
        expect(
            invalid->problem_count() == 0,
            "an unloadable manifest entry leaves no partially loaded exam");
        expect(invalid->current_canvas() == nullptr, "an unloadable entry mounts no canvas");
        invalid->queue_free();
    }
}

// --- persistent state ---

void ExamIntegrationRunner::check_persistent_state() {
    Exam *exam = make_exam(manifest_of({ ACT_LEVEL, SMALL_LEVEL }));
    if (exam == nullptr || !expect(exam->ready_to_play(), "the two-problem exam is ready")) {
        return;
    }
    LevelPlayer *first = exam->current_canvas();
    ProblemState *first_state = exam->problem(0);
    if (first == nullptr || first_state == nullptr) {
        return;
    }

    const std::size_t entry_count = first_state->session().state().palette().entries().size();
    const std::size_t chosen = entry_count > 1 ? 1 : 0;
    first->select_entry(chosen);
    expect(
        first_state->selection().has_value() && first_state->selection()->entry == chosen,
        "selection is written straight to the persistent problem state");
    expect(place_somewhere(*first), "a real player operation places on the first problem");

    const engine::State &recorded_state = first_state->session().state();
    const std::size_t recorded_entries = recorded_state.arrangement().entries().size();
    const std::vector<PlacementId::Value> recorded_ids = placement_ids(recorded_state);
    const std::optional<PlacementId> recorded_next = recorded_state.arrangement().next_id();
    const std::size_t recorded_depth = first_state->session().undo_depth();
    const std::vector<std::optional<engine::Supply::Amount>> recorded_supplies =
        supplies(recorded_state);
    const PaletteSelection recorded_selection = first_state->selection().value();
    const bool recorded_solved = recorded_state.solved();

    expect(exam->navigate(1), "navigation forward is accepted");
    finish_transition(*exam);
    expect(exam->current_index() == 1, "the destination problem became current");

    LevelPlayer *second = exam->current_canvas();
    ProblemState *second_state = exam->problem(1);
    if (second != nullptr && second_state != nullptr) {
        expect(second->state() == second_state, "the new canvas borrows the second problem");
        expect(
            second_state->session().state().arrangement().entries().empty(),
            "the second problem was untouched by work on the first");
        expect(place_somewhere(*second), "the second problem mutates independently");
    }

    expect(exam->navigate(-1), "navigation back is accepted");
    finish_transition(*exam);
    expect(exam->current_index() == 0, "the first problem became current again");

    const engine::State &restored = first_state->session().state();
    expect(
        restored.arrangement().entries().size() == recorded_entries,
        "returning restores the exact arrangement size");
    expect(placement_ids(restored) == recorded_ids,
        "returning restores every placement identity, in order");
    expect(
        restored.arrangement().next_id() == recorded_next,
        "returning restores the identifier allocator");
    expect(
        first_state->session().undo_depth() == recorded_depth,
        "returning restores the undo history depth");
    expect(supplies(restored) == recorded_supplies, "returning restores every derived supply");
    expect(
        first_state->selection().has_value()
            && first_state->selection().value() == recorded_selection,
        "returning restores the saved selection");
    expect(restored.solved() == recorded_solved, "returning restores completion");
    expect(
        second_state != nullptr
            && second_state->session().state().arrangement().entries().size() == 1,
        "the second problem kept its own independent state");

    // Both handoffs destroyed a canvas. Neither problem slot moved or lost
    // anything, and the current canvas is a different node bound to the same
    // state.
    LevelPlayer *remounted = exam->current_canvas();
    expect(
        remounted != nullptr && remounted != first && remounted->state() == first_state,
        "destroying presentation nodes never destroys a problem slot");
    exam->queue_free();
}

// --- fixed shell and transition ---

void ExamIntegrationRunner::check_shell_and_transition() {
    Exam *exam = make_exam(manifest_of({ IZJL_LEVEL, ACT_LEVEL, SMALL_LEVEL }));
    if (exam == nullptr || !expect(exam->ready_to_play(), "the three-problem exam is ready")) {
        return;
    }
    godot::Control *viewport = exam->problem_viewport();
    godot::Control *palette = exam->palette_panel();
    godot::Control *status = exam->status_line();
    if (viewport == nullptr || palette == nullptr || status == nullptr) {
        expect(false, "the exam shell exposes its viewport, palette, and control line");
        return;
    }
    expect(
        palette->get_parent() == exam && status->get_parent() == exam,
        "the palette and the bottom control line are not children of the animated layer");
    expect(
        exam->current_canvas()->get_parent() == viewport,
        "the moving canvas lives inside the clipped problem viewport");
    expect(viewport->is_clipping_contents(), "the problem viewport clips its moving canvases");
    expect(viewport->get_size().y > 0.0f, "the problem viewport has a real height");

    // Every row's shape is clickable, and it reaches the same operation tab does.
    LevelPlayer *canvas = exam->current_canvas();
    const std::size_t rows = exam->entry_row_count();
    expect(
        rows == canvas->session()->state().palette().entries().size(),
        "the palette presents one row per exact palette entry");
    bool clicks_select = rows > 0;
    for (std::size_t i = 0; i < rows; ++i) {
        godot::Button *button = exam->entry_button(i);
        if (button == nullptr) {
            clicks_select = false;
            continue;
        }
        button->emit_signal(godot::StringName("pressed"));
        if (!canvas->selection().has_value() || canvas->selection()->entry != i) {
            clicks_select = false;
        }
    }
    expect(clicks_select, "clicking every palette shape selects its exact authored entry");

    // Exactly one row is lifted, and it is lifted with the region's own fill.
    bool one_highlight = rows > 0 && canvas->selection().has_value();
    for (std::size_t i = 0; i < rows; ++i) {
        const bool current = canvas->selection()->entry == i;
        const godot::Color highlight = exam->entry_row_highlight(i);
        if (current && highlight != LevelPlayer::region_fill_color()) {
            one_highlight = false;
        }
        if (!current && highlight.a != 0.0f) {
            one_highlight = false;
        }
    }
    expect(
        one_highlight,
        "only the selected row is highlighted, in the same fill the region is drawn with");
    expect(
        exam->entry_preview(0) != nullptr
            && exam->entry_preview(0)->get_focus_mode() == godot::Control::FOCUS_NONE,
        "palette shape previews cannot take keyboard focus");
    expect(
        exam->entry_button(0) != nullptr
            && exam->entry_button(0)->get_focus_mode() == godot::Control::FOCUS_NONE,
        "the clickable palette parent cannot take keyboard focus either");
    expect(exam->entry_supply_visible(0), "every shipped palette row presents a finite supply");
    const auto supply_status =
        exam->problem(0)->session().state().supply_status(engine::PaletteEntryIndex(0));
    expect(
        supply_status.has_value() && supply_status->remaining.has_value()
            && exam->entry_supply_text(0)
                == godot::String::num_uint64(supply_status->remaining.value()),
        "a palette row carries its remaining supply and no other text");
    bool authored_colors = rows > 0;
    for (std::size_t i = 0; i < rows; ++i) {
        const PrototilePreview *preview = exam->entry_preview(i);
        if (preview == nullptr || preview->fill_color() != exam->problem(0)->colors()[i]) {
            authored_colors = false;
        }
    }
    expect(authored_colors, "palette shapes use the same authored colors as placed tiles");

    if (rows > 1) {
        exam->entry_button(0)->emit_signal(godot::StringName("pressed"));
        const PaletteSelection after_click = canvas->selection().value();
        exam->_gui_input(key_event(godot::KEY_TAB, false, false));
        const PaletteSelection after_tab = canvas->selection().value();
        expect(after_tab.entry == (after_click.entry + 1) % rows, "tab selects the next entry");
        exam->entry_button(after_tab.entry)->emit_signal(godot::StringName("pressed"));
        expect(
            canvas->selection().value() == after_tab,
            "clicking a shape and tabbing to it leave exactly the same selection");
    }

    // Endpoints are quiet.
    const std::size_t before_entries =
        exam->problem(0)->session().state().arrangement().entries().size();
    expect(!exam->navigate(-1), "navigating before the first problem is refused");
    expect(exam->current_index() == 0, "a refused navigation leaves the current index alone");
    expect(
        exam->problem(0)->session().state().arrangement().entries().size() == before_entries,
        "a refused navigation mutates no session");

    // The fixed controls are not the playable canvas.
    const godot::Vector2 over_palette =
        palette->get_position() + palette->get_size() * godot::Vector2(0.5f, 0.5f);
    const godot::Vector2 over_status =
        status->get_position() + status->get_size() * godot::Vector2(0.5f, 0.5f);
    exam->_gui_input(button_event(godot::MOUSE_BUTTON_WHEEL_DOWN, over_palette));
    exam->_gui_input(button_event(godot::MOUSE_BUTTON_WHEEL_DOWN, over_status));
    expect(
        !exam->transitioning() && exam->current_index() == 0,
        "scrolling over the palette or the control line never navigates");

    // Preparation: two fully bound, final-sized canvases, edge-adjacent.
    const float height = viewport->get_size().y;
    const godot::Vector2 over_canvas =
        viewport->get_position() + viewport->get_size() * godot::Vector2(0.5f, 0.5f);
    exam->_gui_input(button_event(godot::MOUSE_BUTTON_WHEEL_DOWN, over_canvas));
    expect(exam->transitioning(), "a scroll over the playable canvas requests the next problem");
    LevelPlayer *source = exam->current_canvas();
    LevelPlayer *destination = exam->incoming_canvas();
    if (expect(
            destination != nullptr && source != nullptr && destination != source,
            "a transition prepares a second canvas")) {
        expect(viewport->get_child_count() == 2, "exactly two canvases exist during a transition");
        expect(destination->bound(), "the destination canvas is bound before anything moves");
        expect(
            destination->state() == exam->problem(1),
            "the destination canvas borrows the destination problem");
        expect(
            destination->get_size() == viewport->get_size()
                && source->get_size() == viewport->get_size(),
            "both canvases carry the final viewport size before moving");
        expect(
            source->get_position() == godot::Vector2(0.0f, 0.0f)
                && destination->get_position() == godot::Vector2(0.0f, height),
            "a forward transition begins at 0 and one full viewport below");
        expect(
            !destination->proposals().empty(),
            "the destination rebuilt its transient proposals before entering the viewport");
        expect(
            destination->number_label() != nullptr
                && destination->number_label()->get_text()
                    == godot::String("problem ") + godot::String::num_int64(2),
            "the destination presents its own problem number before entering the viewport");
    }

    // Input is inert while the handoff runs.
    const std::size_t source_entries =
        exam->problem(0)->session().state().arrangement().entries().size();
    const std::size_t destination_entries =
        exam->problem(1)->session().state().arrangement().entries().size();
    exam->_gui_input(button_event(godot::MOUSE_BUTTON_LEFT, over_canvas));
    exam->_gui_input(key_event(godot::KEY_Z, false, true));
    exam->_gui_input(key_event(godot::KEY_TAB, false, false));
    exam->_gui_input(button_event(godot::MOUSE_BUTTON_WHEEL_DOWN, over_canvas));
    expect(
        exam->problem(0)->session().state().arrangement().entries().size() == source_entries
            && exam->problem(1)->session().state().arrangement().entries().size()
                == destination_entries,
        "no input mutates a session during a transition");
    expect(
        exam->transitioning() && exam->incoming_canvas() == destination,
        "a scroll during a transition starts no second transition");

    finish_transition(*exam);
    expect(exam->current_index() == 1, "the destination index became current");
    expect(exam->current_canvas() == destination, "the destination canvas became current");
    expect(
        destination->get_position() == godot::Vector2(0.0f, 0.0f),
        "the destination is installed at the canonical zero offset");
    expect(viewport->get_child_count() == 1, "a completed handoff leaves exactly one canvas");
    expect(
        exam->entry_row_count()
            == exam->problem(1)->session().state().palette().entries().size(),
        "the palette rebuilt once from the destination problem");

    // Leave a distinctive state behind, then prove the fixed palette shows it on
    // return.
    LevelPlayer *current = exam->current_canvas();
    const std::size_t second_rows = exam->entry_row_count();
    const std::size_t marked = second_rows > 1 ? 1 : 0;
    current->select_entry(marked);
    expect(place_somewhere(*current), "a tile is placed on the second problem");
    const godot::String spent_supply = exam->entry_supply_text(marked);

    expect(exam->navigate(-1), "navigating back is accepted");
    LevelPlayer *returning = exam->incoming_canvas();
    if (expect(returning != nullptr, "navigating back prepares a canvas")) {
        expect(
            exam->current_canvas()->get_position() == godot::Vector2(0.0f, 0.0f)
                && returning->get_position() == godot::Vector2(0.0f, -height),
            "a reverse transition begins at 0 and one full viewport above");
    }
    finish_transition(*exam);
    expect(exam->current_index() == 0, "the reverse handoff installed the previous problem");

    expect(exam->navigate(1), "navigating forward again is accepted");
    finish_transition(*exam);
    expect(
        exam->problem(1)->selection().has_value()
            && exam->problem(1)->selection()->entry == marked,
        "the returned problem kept its saved selection");
    expect(
        exam->entry_supply_text(marked) == spent_supply,
        "the fixed palette shows the returned problem's derived supplies");
    expect(
        exam->entry_preview(marked) != nullptr
            && exam->entry_preview(marked)->get_modulate() == godot::Color(1, 1, 1, 1),
        "the fixed palette shows the returned problem's saved selection");

    // The far endpoint is quiet too.
    expect(exam->navigate(1), "navigating to the last problem is accepted");
    finish_transition(*exam);
    expect(exam->current_index() == 2, "the last problem is current");
    expect(!exam->navigate(1), "navigating past the last problem is refused");
    expect(exam->current_index() == 2, "a refused navigation at the end changes nothing");
    expect(
        viewport->get_child_count() == 1,
        "a refused navigation leaves the steady-state single canvas");

    // A resize while a handoff is running finishes it rather than leaving a
    // half-moved pair behind.
    expect(exam->navigate(-1), "one more transition is started, to be interrupted");
    const std::size_t interrupted_source =
        exam->problem(2)->session().state().arrangement().entries().size();
    const std::size_t interrupted_destination =
        exam->problem(1)->session().state().arrangement().entries().size();
    exam->set_size(godot::Vector2(EXAM_WIDTH - 140.0f, EXAM_HEIGHT - 90.0f));
    expect(!exam->transitioning(), "a resize during a transition completes the handoff");
    expect(exam->current_index() == 1, "the interrupted handoff installed its destination");
    expect(viewport->get_child_count() == 1, "the interrupted handoff left exactly one canvas");
    expect(
        exam->current_canvas() != nullptr
            && exam->current_canvas()->get_position() == godot::Vector2(0.0f, 0.0f)
            && exam->current_canvas()->get_size() == viewport->get_size(),
        "the surviving canvas is installed at zero and refitted to the new viewport");
    expect(
        exam->problem(2)->session().state().arrangement().entries().size() == interrupted_source
            && exam->problem(1)->session().state().arrangement().entries().size()
                == interrupted_destination,
        "a resize during a transition mutates no session");
    exam->queue_free();
}

// --- presentation and hooks ---

void ExamIntegrationRunner::check_presentation_and_hooks() {
    Exam *exam = make_exam(manifest_of({ FIXTURE_LEVEL, ACT_LEVEL }));
    if (exam == nullptr || !expect(exam->ready_to_play(), "the fixture exam is ready")) {
        return;
    }
    connect_hooks(*exam);
    LevelPlayer *canvas = exam->current_canvas();
    ProblemState *state = exam->problem(0);
    if (canvas == nullptr || state == nullptr) {
        return;
    }

    const godot::RichTextLabel *completion = canvas->completion_label();
    if (expect(completion != nullptr, "the canvas carries a completion presentation")) {
        expect(
            completion->get_mouse_filter() == godot::Control::MOUSE_FILTER_IGNORE,
            "the completion presentation ignores pointer input");
        expect(
            completion->is_using_bbcode()
                && completion->get_text().contains("outline_size")
                && completion->get_text().contains("complete!"),
            "the completion presentation is outlined bbcode reading complete!");
        expect(
            completion->has_theme_font_size_override("normal_font_size")
                && completion->get_theme_font_size("normal_font_size") >= 60,
            "the completion presentation is set far larger than body text");
        expect(!canvas->completion_visible(), "an unsolved problem shows no completion");
    }

    // Selection: the same operation from both routes, and silence when nothing
    // actually changes.
    const std::size_t rows = exam->entry_row_count();
    canvas->select_entry(0);
    const std::size_t selections_after_first = hooks_.palette_selected;
    canvas->select_entry(0);
    expect(
        hooks_.palette_selected == selections_after_first,
        "reselecting the same entry emits no selection hook");
    if (rows > 1) {
        exam->entry_button(1)->emit_signal(godot::StringName("pressed"));
        expect(
            hooks_.palette_selected == selections_after_first + 1,
            "a pointer selection emits exactly one selection hook");
        exam->_gui_input(key_event(godot::KEY_TAB, false, false));
        expect(
            hooks_.palette_selected == selections_after_first + 2,
            "keyboard selection emits the same hook");
    }

    // Rotation: an entry with one distinct orientation is a no-op.
    std::optional<std::size_t> single;
    std::optional<std::size_t> multiple;
    const auto &entries = state->session().state().palette().entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].orientations().size() == 1 && !single.has_value()) {
            single = i;
        }
        if (entries[i].orientations().size() > 1 && !multiple.has_value()) {
            multiple = i;
        }
    }
    if (expect(single.has_value(), "the fixture offers a single-orientation entry")) {
        canvas->select_entry(single.value());
        const std::size_t before = hooks_.orientation_changed;
        canvas->cycle_orientation(true);
        expect(
            hooks_.orientation_changed == before,
            "cycling an entry with one distinct orientation emits nothing");
    }
    if (expect(multiple.has_value(), "the fixture offers a multi-orientation entry")) {
        canvas->select_entry(multiple.value());
        const std::size_t before = hooks_.orientation_changed;
        canvas->cycle_orientation(true);
        expect(
            hooks_.orientation_changed == before + 1,
            "an actual rotation emits exactly one orientation hook");
    }

    // Pointer motion which leaves the ghost alone is silent.
    if (!canvas->proposals().empty()) {
        const godot::Vector2 target = centroid(*canvas, canvas->proposals()[0].placement);
        canvas->set_pointer(target);
        const std::size_t before = hooks_.active_proposal_changed;
        canvas->set_pointer(target);
        expect(
            hooks_.active_proposal_changed == before,
            "pointer motion which leaves the same active proposal emits nothing");
    }

    // Empty undo and a removal with nothing under the pointer are silent.
    const std::size_t undos = hooks_.undo_succeeded;
    expect(!canvas->undo(), "undo on an untouched problem is refused");
    expect(hooks_.undo_succeeded == undos, "a refused undo emits nothing");
    const std::size_t removals = hooks_.removal_succeeded;
    expect(
        !canvas->remove_at_local(godot::Vector2(-4000.0f, -4000.0f)),
        "removing where nothing is placed is refused");
    expect(hooks_.removal_succeeded == removals, "a refused removal emits nothing");

    // Solve the fixture through ordinary proposals, then take it apart again.
    const Point origin { Coordinate::from_raw(0), Coordinate::from_raw(0) };
    const Point domino { Coordinate::from_raw(2 * Coordinate::SCALE), Coordinate::from_raw(0) };
    const auto accept_at = [&](Point p_translation) {
        for (const LevelPlayer::Proposal &proposal : canvas->proposals()) {
            if (proposal.placement.translation() != p_translation) {
                continue;
            }
            canvas->set_pointer(centroid(*canvas, proposal.placement));
            return canvas->accept_active_proposal();
        }
        return false;
    };
    canvas->select_entry(0);
    const std::size_t placements = hooks_.placement_succeeded;
    expect(accept_at(origin), "the fixture accepts its first ordinary proposal");
    expect(
        hooks_.placement_succeeded == placements + 1,
        "a successful placement emits exactly one placement hook");
    canvas->select_entry(1);
    canvas->cycle_orientation(true);
    expect(accept_at(domino), "the fixture solves through ordinary proposals");
    expect(canvas->completion_visible(), "completion appears exactly when the state is solved");
    expect(hooks_.victory_reached == 1, "solving emits victory exactly once");
    expect(exam->status_line() != nullptr, "the exam owns the fixed control line");

    const std::size_t undone = hooks_.undo_succeeded;
    expect(canvas->undo(), "undo takes the solved state apart again");
    expect(hooks_.undo_succeeded == undone + 1, "a successful undo emits exactly one hook");
    expect(!canvas->completion_visible(), "undo hides completion again");
    expect(hooks_.victory_reached == 1, "leaving the solved state emits no further victory");
    canvas->select_entry(1);
    canvas->cycle_orientation(true);
    expect(accept_at(domino), "the fixture can be solved a second time");
    expect(hooks_.victory_reached == 2, "reaching solved again emits victory again");
    expect(canvas->completion_visible(), "completion returns with the solved state");

    // Navigation hooks, and the silence of a remount.
    expect(hooks_.problem_changed == 0, "no problem change was reported before one happened");
    expect(exam->navigate(1), "the fixture exam navigates forward");
    expect(
        hooks_.problem_changed == 0,
        "requesting a transition reports no problem change on its own");
    finish_transition(*exam);
    expect(hooks_.problem_changed == 1, "a completed handoff reports exactly one problem change");
    expect(
        problem_changes_.size() == 2 && problem_changes_[0] == 1 && problem_changes_[1] == 1,
        "the problem change reports the installed index and its derived number");
    expect(!exam->navigate(1), "navigating past the end of the fixture exam is refused");
    expect(hooks_.problem_changed == 1, "a refused navigation reports no problem change");

    const HookCounts before_remount = hooks_;
    expect(exam->navigate(-1), "the fixture exam navigates back");
    finish_transition(*exam);
    expect(
        exam->current_canvas() != nullptr && exam->current_canvas()->completion_visible(),
        "a remounted solved problem shows its completion again");
    expect(
        hooks_.victory_reached == before_remount.victory_reached
            && hooks_.placement_succeeded == before_remount.placement_succeeded
            && hooks_.removal_succeeded == before_remount.removal_succeeded
            && hooks_.undo_succeeded == before_remount.undo_succeeded
            && hooks_.palette_selected == before_remount.palette_selected
            && hooks_.orientation_changed == before_remount.orientation_changed
            && hooks_.active_proposal_changed == before_remount.active_proposal_changed,
        "remounting a problem invents no gameplay or victory hook");
    expect(
        hooks_.problem_changed == before_remount.problem_changed + 1,
        "the return reported exactly one further problem change");
    exam->queue_free();
}

void ExamIntegrationRunner::_ready() {
    check_manifest_and_startup();
    check_invalid_manifests();
    check_persistent_state();
    check_shell_and_transition();
    check_presentation_and_hooks();

    if (failures_ == 0) {
        godot::UtilityFunctions::print(
            "[tiles] exam integration: ",
            static_cast<std::int64_t>(checks_),
            " checks passed");
    } else {
        godot::UtilityFunctions::push_error(
            "[tiles] exam integration: ",
            static_cast<std::int64_t>(failures_),
            " of ",
            static_cast<std::int64_t>(checks_),
            " checks failed");
    }
    get_tree()->quit(failures_ == 0 ? 0 : 1);
}

} // namespace tiles::game
