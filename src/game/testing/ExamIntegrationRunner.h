#pragma once

#include "core/geometry/Point.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tiles::game {

class Exam;
class LevelPlayer;

// The headless proof that the assembled exam is the one the application ships:
// the authored manifest, one persistent exact problem per entry, the fixed
// shell, the two-canvas vertical handoff, and the semantic hooks.
//
// Every check runs against the real exam scene, the real canvas scene, the real
// loading path, and the real player operations. Nothing here reaches around them
// into a test-only mode.
class ExamIntegrationRunner : public godot::Node {
    GDCLASS(ExamIntegrationRunner, godot::Node)

protected:
    static void _bind_methods();

public:
    void _ready() override;

    // Signal sinks. Every semantic hook the exam or a canvas emits is counted
    // here through an ordinary Godot connection.
    void on_palette_selected(std::int64_t p_entry);
    void on_orientation_changed(std::int64_t p_entry, std::int64_t p_orientation, bool p_forward);
    void on_placement_succeeded(std::int64_t p_entry);
    void on_removal_succeeded();
    void on_undo_succeeded();
    void on_active_proposal_changed(bool p_present, std::int64_t p_proposal);
    void on_victory_reached();
    void on_problem_changed(std::int64_t p_index, std::int64_t p_number);

private:
    struct HookCounts final {
        std::size_t palette_selected = 0;
        std::size_t orientation_changed = 0;
        std::size_t placement_succeeded = 0;
        std::size_t removal_succeeded = 0;
        std::size_t undo_succeeded = 0;
        std::size_t active_proposal_changed = 0;
        std::size_t victory_reached = 0;
        std::size_t problem_changed = 0;
    };

    bool expect(bool p_condition, const char *p_description);

    // One exam built from the real scene. An explicit manifest replaces the
    // authored one before the node enters the tree, so a small manifest reaches
    // exactly the production loading path.
    Exam *make_exam(const godot::PackedStringArray &p_manifest);
    void connect_hooks(Exam &p_exam);
    void finish_transition(Exam &p_exam);
    bool place_somewhere(LevelPlayer &p_canvas);

    void check_manifest_and_startup();
    void check_invalid_manifests();
    void check_persistent_state();
    void check_shell_and_transition();
    void check_presentation_and_hooks();

    HookCounts hooks_;
    std::vector<std::int64_t> problem_changes_;
    std::size_t checks_ = 0;
    std::size_t failures_ = 0;
};

} // namespace tiles::game
