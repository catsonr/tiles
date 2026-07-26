#pragma once

#include "content/PrototileCatalog.h"
#include "game/ProblemState.h"
#include "game/resources/LevelPersistence.h"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace tiles::game {

class LevelPlayer;
class PrototilePreview;

// The application: one ordered manifest of authored levels, one persistent exact
// problem state for each of them, and the one visible shell they are played
// through.
//
// The manifest is authored content. Its size is the first displayed problem
// number and every later number counts down from it, so nothing here may assume
// how many problems the exam contains: adding, removing, or reordering entries
// is a change to the manifest data and to nothing else.
//
// The shell is fixed. The palette panel and the bottom control line never move
// and are never children of the animated layer. One clipped region holds the
// moving problem canvases: exactly one in steady state, and exactly two while a
// vertical problem transition runs.
//
// Ownership runs one way. The Exam owns every problem state at a stable address
// for its whole lifetime; a canvas borrows the one it presents and may be
// created and destroyed freely without disturbing it.
class Exam : public godot::Control {
    GDCLASS(Exam, godot::Control)

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _process(double p_delta) override;
    void _gui_input(const godot::Ref<godot::InputEvent> &p_event) override;
    void _notification(int p_what);

    void set_manifest(const godot::PackedStringArray &p_manifest);
    godot::PackedStringArray get_manifest() const;

    // Every manifest entry loaded, compiled, and mounted. A manifest which is
    // empty, repeats a path, or names content which does not compile leaves this
    // false and exposes no partially loaded exam.
    bool ready_to_play() const;
    std::size_t problem_count() const;
    std::size_t current_index() const;
    // The descending number one manifest index is displayed as. Derived from the
    // manifest size alone.
    std::int64_t problem_number(std::size_t p_index) const;
    ProblemState *problem(std::size_t p_index);

    LevelPlayer *current_canvas() const;
    // The destination canvas of the running transition, if one is running.
    LevelPlayer *incoming_canvas() const;
    bool transitioning() const;

    // Request the adjacent problem in the given direction: +1 for the next
    // problem, -1 for the previous one. A request past either endpoint, a
    // request while a transition is already running, and a request before the
    // exam is ready are all non-mutating refusals.
    bool navigate(int p_direction);

    // Advance the running transition by one step of presentation time and report
    // whether it is still running. This is the production step: ordinary play
    // calls it once per frame with the frame delta, and a test may call it once
    // with the whole duration to reach exactly the same completed handoff.
    bool advance_transition(double p_delta);

    std::size_t entry_row_count() const;
    const PrototilePreview *entry_preview(std::size_t p_entry) const;
    godot::Button *entry_button(std::size_t p_entry) const;
    bool entry_supply_visible(std::size_t p_entry) const;
    godot::String entry_supply_text(std::size_t p_entry) const;
    // The row background behind one palette entry. Only the selected row is
    // tinted; every other row is fully transparent.
    godot::Color entry_row_highlight(std::size_t p_entry) const;

    godot::Control *problem_viewport() const;
    godot::Control *palette_panel() const;
    godot::Control *status_line() const;

private:
    struct EntryControl final {
        godot::PanelContainer *row = nullptr;
        godot::Ref<godot::StyleBoxFlat> highlight;
        godot::Button *button = nullptr;
        PrototilePreview *preview = nullptr;
        godot::Label *supply = nullptr;
    };

    struct Transition final {
        LevelPlayer *source = nullptr;
        LevelPlayer *destination = nullptr;
        std::size_t destination_index = 0;
        int direction = 1;
        double elapsed = 0.0;
        float travel = 0.0f;
    };

    // The fixed shell as its bound canvas sees it.
    struct ShellObserver final : ProblemStateObserver {
        Exam *exam = nullptr;
        void on_problem_presentation_changed() override;
    };

    bool bind_scene();
    bool load_problems();
    void report_load_failure(const godot::String &p_path, const LoadLevelError &p_error) const;
    void update_shell_layout();
    LevelPlayer *make_canvas(std::size_t p_index, float p_offset);
    void complete_transition();
    godot::Rect2 viewport_rect() const;
    void build_palette();
    void clear_palette();
    void refresh_shell();
    bool request_scroll_navigation(godot::Vector2 p_position, double p_amount);
    godot::Vector2 to_canvas_local(godot::Vector2 p_position) const;

    void on_palette_pressed(std::int64_t p_entry);
    void on_canvas_palette_selected(std::int64_t p_entry);
    void on_canvas_orientation_changed(
        std::int64_t p_entry, std::int64_t p_orientation, bool p_forward);
    void on_canvas_placement_succeeded(std::int64_t p_entry);
    void on_canvas_removal_succeeded();
    void on_canvas_undo_succeeded();
    void on_canvas_active_proposal_changed(bool p_present, std::int64_t p_proposal);
    void on_canvas_victory_reached();

    godot::PackedStringArray manifest_;
    std::optional<content::PrototileCatalog> catalog_;
    // Stable addresses for the lifetime of the exam: a canvas borrows one of
    // these, and the vector may not relocate what it is pointing at.
    std::vector<std::unique_ptr<ProblemState>> problems_;
    std::size_t current_index_ = 0;
    bool ready_ = false;
    ShellObserver observer_;

    godot::Ref<godot::PackedScene> canvas_scene_;
    godot::Control *viewport_ = nullptr;
    godot::Control *palette_ = nullptr;
    godot::Control *palette_rows_ = nullptr;
    godot::Label *status_label_ = nullptr;
    LevelPlayer *current_canvas_ = nullptr;
    std::optional<Transition> transition_;
    double scroll_cooldown_ = 0.0;
    std::vector<EntryControl> entry_controls_;
};

} // namespace tiles::game
