#pragma once

#include "core/Placement.h"
#include "engine/Commands.h"
#include "engine/Session.h"
#include "game/ProblemState.h"

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace tiles::game {

// One problem's visible canvas: its number, its exact target, its exact current
// arrangement, its active ghost, and its completion.
//
// It owns no play state. It borrows one persistent ProblemState and applies
// every mutation to that problem's real Session, so destroying the node leaves
// the problem exactly as the player left it. Proposals, the active proposal, the
// pointer, the projection, and hit-test results are transient presentation and
// are rebuilt from the persistent state whenever the canvas is bound.
//
// It selects among and applies only commands the engine has already proven
// legal. Screen coordinates rank and hit-test rendered values only; they never
// create model geometry.
//
// The canvas moves during problem navigation and is created and destroyed around
// it. It therefore handles no input of its own and owns no palette row or status
// line: the fixed shell routes operations to whichever canvas is current.
class LevelPlayer : public godot::Control {
    GDCLASS(LevelPlayer, godot::Control)

protected:
    static void _bind_methods();

public:
    using Selection = PaletteSelection;

    using ProposalCommand = std::variant<
        engine::PlaceCommand,
        engine::MateFullEdgesCommand,
        engine::MateVerticesCommand>;

    struct Proposal final {
        ProposalCommand command;
        Placement placement;
    };

    void _ready() override;
    void _draw() override;
    void _notification(int p_what);

    // The color the exact target is filled with. The fixed shell borrows it so
    // the palette's selected-row highlight and the region are one decision
    // rather than two literals which can drift apart.
    static godot::Color region_fill_color();

    // Present one persistent problem. Everything transient is rebuilt here, and
    // nothing about the bound problem is mutated: binding is not a play event and
    // emits no semantic hook.
    void bind(
        ProblemState &p_state,
        std::int64_t p_problem_number,
        ProblemStateObserver *p_observer);
    bool bound() const;
    std::int64_t problem_number() const;
    const ProblemState *state() const;

    void select_entry(std::size_t p_entry);
    void cycle_entry(bool p_forward);
    void cycle_orientation(bool p_forward);
    bool accept_active_proposal();
    bool remove_at_local(godot::Vector2 p_local);
    bool undo();
    void set_pointer(godot::Vector2 p_local);

    const engine::Session *session() const;
    std::optional<Selection> selection() const;
    const std::vector<Proposal> &proposals() const;
    std::optional<std::size_t> active_proposal() const;
    bool completion_visible() const;
    // The fixed status line's current text for this problem: its transient
    // refusal, its completion, or the ordinary control hint.
    godot::String status_text() const;
    const godot::Label *number_label() const;
    const godot::RichTextLabel *completion_label() const;
    godot::Vector2 project(Point p_point) const;
    godot::Rect2 canvas_rect() const;

private:
    // Which exact proposal the ghost is drawing, if any. Compared across an
    // operation so a hook reports a real change of ghost rather than an index
    // which happens to have been reused by a rebuilt proposal list.
    struct GhostIdentity final {
        bool present = false;
        Point translation { Coordinate::from_raw(0), Coordinate::from_raw(0) };
    };

    void rebuild_proposals();
    bool update_active_proposal();
    void refresh_after_mutation();
    void refresh_completion();
    void notify_shell();
    GhostIdentity ghost_identity() const;
    void emit_ghost_change(const GhostIdentity &p_before);
    void emit_victory_if_reached();
    const engine::PaletteEntry *selected_entry() const;
    const OrientedPrototile *selected_variant() const;
    std::optional<std::size_t> placement_at_local(godot::Vector2 p_local) const;
    void update_canvas_rect();
    void update_projection();
    godot::Vector2 project(double p_x, double p_y) const;
    void draw_polygon(const Polygon &p_polygon, godot::Color p_fill, float p_outline);
    void draw_region();
    void draw_arrangement();
    void draw_ghost();

    ProblemState *state_ = nullptr;
    ProblemStateObserver *observer_ = nullptr;
    std::int64_t problem_number_ = 0;
    bool solved_ = false;
    godot::String refusal_;
    std::vector<Proposal> proposals_;
    std::optional<std::size_t> active_proposal_;
    std::optional<godot::Vector2> pointer_;
    godot::Rect2 canvas_rect_;
    godot::Vector2 projection_origin_;
    double pixels_per_unit_ = 1.0;
    godot::Label *number_label_ = nullptr;
    godot::RichTextLabel *completion_label_ = nullptr;
};

} // namespace tiles::game
