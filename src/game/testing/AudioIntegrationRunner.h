#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <cstddef>

namespace tiles::game {

class AudioDirector;
class Exam;
class LevelPlayer;

// The headless proof that the shipped exam scene carries exactly one persistent
// audio director, that the background voice is started once and never disturbed,
// and that nothing the director does can reach exact play.
//
// Every check runs against the real exam scene, the real director, the real
// authored background resource, the real semantic connection, and the same
// delta-driven step `_process` calls. Nothing here reaches around any of them
// into a test-only clock, seed, signal, or asset.
class AudioIntegrationRunner : public godot::Node {
    GDCLASS(AudioIntegrationRunner, godot::Node)

protected:
    static void _bind_methods();

public:
    void _ready() override;

private:
    bool expect(bool p_condition, const char *p_description);

    // One exam built from the real scene, with the real director inside it.
    Exam *make_exam(const godot::PackedStringArray &p_manifest);
    // The same scene with its director removed before it ever becomes ready:
    // the control case for "audio cannot affect exact play".
    Exam *make_silent_exam(const godot::PackedStringArray &p_manifest);
    AudioDirector *director_of(Exam &p_exam, std::size_t *r_count);
    void finish_transition(Exam &p_exam);
    // Solve the two-tile fixture through ordinary proposals, which is what makes
    // the exam emit a real victory.
    bool solve_fixture(LevelPlayer &p_canvas);
    // Replace only the placement one undo took back, so a solved fixture can be
    // unsolved and solved again as many times as a check needs.
    bool place_last_tile(LevelPlayer &p_canvas);

    void check_scene_ownership();
    void check_background_start();
    void check_ambient_envelope();
    void check_victory_duck();
    void check_play_is_unaffected();

    std::size_t checks_ = 0;
    std::size_t failures_ = 0;
};

} // namespace tiles::game
